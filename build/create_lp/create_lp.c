/*
 * create_lp - map the dynamic (logical) partitions of xiaomi monet from the
 * super partition's LP metadata.
 *
 * This runs in init.recovery.qcom.rc ("on fs"), before TWRP's GUI starts.
 * TWRP's own Setup_Super_Devices() path does not work on this device at that
 * point, so this tool creates the /dev/block/mapper/* dm-linear devices.
 *
 * Unlike the original implementation (a hard-coded partition table), the
 * partition layout is read dynamically from the super partition's
 * LpMetadata (geometry -> metadata header -> partition/extent tables).  This
 * keeps the mapping correct regardless of how the logical partitions are
 * packaged (ext4, erofs, ...) as long as the super metadata is authoritative.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef long long llong;
typedef unsigned long long ullong;

struct dm_ioctl {
    uint version[3];
    uint data_size;
    uint data_start;
    uint target_count;
    int open_count;
    uint flags;
    uint event_nr;
    uint padding;
    ullong dev;
    char name[128];
    char uuid[129];
    char data[7];
};

struct dm_target_spec {
    ullong sector_start;
    ullong length;
    int status;
    uint next;
    char target_type[16];
};

#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT+_IOC_SIZEBITS)
#define _IOC(dir,type,nr,size) (((dir)<<_IOC_DIRSHIFT)|((type)<<_IOC_TYPESHIFT)|((nr)<<_IOC_NRSHIFT)|((size)<<_IOC_SIZESHIFT))
#define _IOWR(type,nr,size) _IOC(3,(type),(nr),(size))
#define DM_IOCTL 0xfd
#define DM_DEV_CREATE_CMD 3
#define DM_DEV_SUSPEND_CMD 6
#define DM_TABLE_LOAD_CMD 9
#define DM_DEV_CREATE _IOWR(DM_IOCTL, DM_DEV_CREATE_CMD, sizeof(struct dm_ioctl))
#define DM_DEV_SUSPEND _IOWR(DM_IOCTL, DM_DEV_SUSPEND_CMD, sizeof(struct dm_ioctl))
#define DM_TABLE_LOAD _IOWR(DM_IOCTL, DM_TABLE_LOAD_CMD, sizeof(struct dm_ioctl))

/* arm64 syscalls */
#define SYS_openat 56
#define SYS_ioctl 29
#define SYS_read 63
#define SYS_write 64
#define SYS_close 57
#define SYS_lseek 62
#define SYS_mkdirat 83
#define SYS_unlinkat 35
#define SYS_symlinkat 58
#define SYS_exit_group 94
#define AT_FDCWD -100
#define O_RDWR 2
#define O_RDONLY 0
#define O_CLOEXEC 02000000

static long ksys(long n, long a, long b, long c, long d) {
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a;
    register long x1 asm("x1") = b;
    register long x2 asm("x2") = c;
    register long x3 asm("x3") = d;
    register long x4 asm("x4") = 0;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory");
    return x0;
}

static void strcpyn(char* dst, const char* src, long max) {
    long i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static void s2u(char* b, unsigned long v) {
    char t[24]; int i = 0; if (v == 0) { b[0] = '0'; b[1] = 0; return; }
    while (v) { t[i++] = '0' + (v % 10); v /= 10; }
    for (int j = 0; j < i; j++) b[j] = t[i - 1 - j];
    b[i] = 0;
}
static long slen(const char* s) { long n = 0; while (s[n]) n++; return n; }

/* ---- LP metadata parsing ---- */

#define LP_GEOMETRY_MAGIC 0x616c4467UL
#define LP_HEADER_MAGIC 0x414C5030UL
#define LP_METADATA_GEOMETRY_SIZE 4096
#define LP_SECTOR_SIZE 512

/* LpMetadataGeometry (packed, little-endian), 52 bytes at offset 4096 */
struct lp_geometry {
    uint32_t magic;
    uint32_t struct_size;
    unsigned char checksum[32];
    uint32_t metadata_max_size;
    uint32_t metadata_slot_count;
    uint32_t first_metadata_offset;   /* in 512-byte sectors */
};

/* LpMetadataTableDescriptor, 12 bytes */
struct lp_table_desc {
    uint32_t offset;
    uint32_t num_entries;
    uint32_t entry_size;
};

/* LpMetadataHeader (v1.0), 128 bytes */
struct lp_header {
    uint32_t magic;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t header_size;
    unsigned char header_checksum[32];
    uint32_t tables_size;
    unsigned char tables_checksum[32];
    struct lp_table_desc partitions;   /* offset 80 */
    struct lp_table_desc extents;      /* offset 92 */
    struct lp_table_desc groups;       /* offset 104 */
    struct lp_table_desc block_devices;/* offset 116 */
};

/* LpMetadataPartition, 52 bytes */
struct lp_partition_ent {
    char name[36];
    uint32_t attributes;
    uint32_t first_extent_index;
    uint32_t num_extents;
    uint32_t group_index;
};

/* LpMetadataExtent, 24 bytes */
struct lp_extent {
    uint64_t num_sectors;
    uint32_t target_type;
    uint64_t target_data;   /* physical sector for LP_TARGET_TYPE_LINEAR */
};

static uint32_t rd32(const unsigned char* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t rd16(const unsigned char* b) {
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
static uint64_t rd64(const unsigned char* b) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

struct lp_partition {
    char name[36];
    unsigned long long super_offset; /* 512B sectors */
    unsigned long long length;       /* 512B sectors */
};

/* Maximum logical partitions we care about (the system/vendor/etc. ones). */
#define MAX_LP_PARTITIONS 16

/* Read a region of the super partition into buf.  Returns bytes read or -1. */
static long super_read(long sfd, unsigned long long offset_bytes,
                       unsigned char* buf, long len) {
    long r = ksys(SYS_lseek, sfd, (long)offset_bytes, 0 /*SEEK_SET*/, 0);
    if (r < 0) return -1;
    long total = 0;
    while (total < len) {
        r = ksys(SYS_read, sfd, (long)(buf + total), len - total, 0);
        if (r <= 0) break;
        total += r;
    }
    return total;
}

/* Parse the LP geometry + metadata and fill |parts|.
 * Returns number of logical partitions found, or -1 on failure.
 * |super_path| is the block device path of the super partition. */
static long parse_lp_metadata(long sfd, struct lp_partition* parts,
                              long max_parts) {
    unsigned char geo_buf[LP_METADATA_GEOMETRY_SIZE];
    struct lp_geometry* geo;
    uint32_t first_meta;
    unsigned char* meta = NULL;
    long meta_size;
    struct lp_header* hdr;
    const unsigned char* tables;
    const unsigned char* ptab;
    const unsigned char* etab;
    uint32_t p_off, p_num, p_esz, e_off, e_num, e_esz;
    long nparts = 0;

    /* Geometry lives at offset 4096 (page 1). */
    if (super_read(sfd, 4096, geo_buf, sizeof(geo_buf)) != (long)sizeof(geo_buf))
        return -1;
    geo = (struct lp_geometry*)geo_buf;
    if (geo->magic != LP_GEOMETRY_MAGIC)
        return -1;
    first_meta = geo->first_metadata_offset;
    if (first_meta == 0)
        return -1;

    /* Allocate a buffer for one metadata slot. */
    meta_size = geo->metadata_max_size;
    if (meta_size == 0 || meta_size > 4 * 1024 * 1024)
        meta_size = 65536;   /* AOSP default */
    meta = (unsigned char*)0;  /* placeholder; we use a static buf below */

    /* We read the metadata region in one go (a slot plus slack) and scan for
     * the header magic.  Some devices (Xiaomi) put the header at a location
     * that does not match first_metadata_offset, so scan a few candidates. */
    {
        static unsigned char metabuf[4 * 1024 * 1024];
        unsigned long long meta_byte = (unsigned long long)first_meta * LP_SECTOR_SIZE;
        long got = super_read(sfd, meta_byte, metabuf, (long)meta_size);
        if (got < 128)
            return -1;

        /* Look for the header magic in the slot. */
        long found = -1;
        for (long i = 0; i + 128 <= got; i += 4) {
            if (rd32(metabuf + i) == LP_HEADER_MAGIC) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            /* Fallback: scan the first 4 MiB of super for the header. */
            got = super_read(sfd, 0, metabuf, (long)sizeof(metabuf));
            if (got < 128)
                return -1;
            for (long i = 0; i + 128 <= got; i += 4) {
                if (rd32(metabuf + i) == LP_HEADER_MAGIC) {
                    found = i;
                    break;
                }
            }
        }
        if (found < 0)
            return -1;

        hdr = (struct lp_header*)(metabuf + found);
        if (hdr->header_size < 128 || hdr->header_size > got - found)
            return -1;
        tables = (const unsigned char*)hdr + hdr->header_size;
        if ((long)(tables - metabuf) + hdr->tables_size > got)
            return -1;

        p_off = hdr->partitions.offset;
        p_num = hdr->partitions.num_entries;
        p_esz = hdr->partitions.entry_size;
        e_off = hdr->extents.offset;
        e_num = hdr->extents.num_entries;
        e_esz = hdr->extents.entry_size;
        if (p_num > max_parts)
            p_num = max_parts;
        if (p_num == 0 || p_esz < 52 || e_esz < 24)
            return -1;

        ptab = tables + p_off;
        etab = tables + e_off;

        for (uint32_t i = 0; i < p_num; i++) {
            const unsigned char* p = ptab + (unsigned long long)i * p_esz;
            struct lp_partition_ent ent;
            uint32_t fe, ne;
            const unsigned char* e;
            /* copy partition entry (safe if p_esz >= 52) */
            for (int b = 0; b < 52; b++) ((unsigned char*)&ent)[b] = p[b];
            fe = ent.first_extent_index;
            ne = ent.num_extents;
            if (ne == 0)
                continue;
            e = etab + (unsigned long long)fe * e_esz;
            if (e < metabuf || e + 24 > metabuf + got)
                continue;
            strcpyn(parts[nparts].name, ent.name, sizeof(parts[nparts].name));
            parts[nparts].super_offset = rd64(e + 12); /* target_data */
            parts[nparts].length = rd64(e + 0);        /* num_sectors */
            if (parts[nparts].length == 0)
                continue;
            nparts++;
        }
    }
    return nparts;
}

int main(void) {
    char iobuf[1024];
    char super_path[] = "/dev/block/bootdevice/by-name/super";
    char symlink_path[] = "/dev/block/mapper/";
    static struct lp_partition partitions[MAX_LP_PARTITIONS];
    long nparts = 0;
    long sfd;

    /* Open the super partition to read its LP metadata. */
    sfd = ksys(SYS_openat, AT_FDCWD, (long)super_path, O_RDONLY, 0);
    if (sfd >= 0) {
        nparts = parse_lp_metadata(sfd, partitions, MAX_LP_PARTITIONS);
        ksys(SYS_close, sfd, 0, 0, 0);
    }

    if (nparts <= 0) {
        /* Fallback: could not parse LP metadata - nothing to map. */
        return 1;
    }

    long dmfd = ksys(SYS_openat, AT_FDCWD, (long)"/dev/device-mapper", O_RDWR, 0);
    if (dmfd < 0) return 1;

    /* Create /dev/block/mapper dir if missing */
    ksys(SYS_mkdirat, AT_FDCWD, (long)"/dev/block/mapper", 0755, 0);

    /* Remove any existing logical mappings first (keep userdata) */
    for (long p = 0; p < nparts; p++) {
        struct dm_ioctl* rioc = (struct dm_ioctl*)iobuf;
        for (int i = 0; i < (int)sizeof(*rioc)/4; i++) ((uint*)rioc)[i] = 0;
        rioc->version[0] = 4; rioc->version[1] = 0; rioc->version[2] = 0;
        rioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(rioc->name, partitions[p].name, 128);
        ksys(SYS_ioctl, dmfd, 4 /*DM_DEV_REMOVE*/, (long)rioc, 0);
    }

    for (long p = 0; p < nparts; p++) {
        struct dm_ioctl* ioc = (struct dm_ioctl*)iobuf;
        struct dm_target_spec* spec = (struct dm_target_spec*)(iobuf + 320);
        char* params = (char*)(iobuf + 320 + sizeof(struct dm_target_spec));

        /* Try create - if it fails with EBUSY (already exists), skip. */
        for (int i = 0; i < (int)sizeof(*ioc)/4; i++) ((uint*)ioc)[i] = 0;
        ioc->version[0] = 4; ioc->version[1] = 0; ioc->version[2] = 0;
        ioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(ioc->name, partitions[p].name, 128);
        long r = ksys(SYS_ioctl, dmfd, DM_DEV_CREATE, (long)ioc, 0);
        if (r != 0) continue; /* already exists or failed - leave as is */

        /* TABLE_LOAD */
        for (int i = 0; i < (int)sizeof(*ioc)/4; i++) ((uint*)ioc)[i] = 0;
        ioc->version[0] = 4; ioc->version[1] = 0; ioc->version[2] = 0;
        ioc->data_size = sizeof(struct dm_ioctl) + 8 + sizeof(struct dm_target_spec) + 64;
        ioc->data_start = 320;
        ioc->target_count = 1;
        strcpyn(ioc->name, partitions[p].name, 128);

        spec->sector_start = 0;
        spec->length = partitions[p].length;
        spec->status = 0;
        spec->next = 0;
        strcpyn(spec->target_type, "linear", 16);

        char* d = params;
        for (int i = 0; super_path[i]; i++) d[i] = super_path[i];
        d += slen(super_path);
        d[0] = ' ';
        d++;
        char off[24];
        s2u(off, partitions[p].super_offset);
        for (int i = 0; off[i]; i++) d[i] = off[i];
        d += slen(off);
        d[0] = 0;

        r = ksys(SYS_ioctl, dmfd, DM_TABLE_LOAD, (long)ioc, 0);
        if (r != 0) continue;

        /* RESUME */
        for (int i = 0; i < (int)sizeof(*ioc)/4; i++) ((uint*)ioc)[i] = 0;
        ioc->version[0] = 4; ioc->version[1] = 0; ioc->version[2] = 0;
        ioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(ioc->name, partitions[p].name, 128);
        r = ksys(SYS_ioctl, dmfd, DM_DEV_SUSPEND, (long)ioc, 0);
        if (r != 0) continue;

        /* Create /dev/block/mapper/<name> symlink to the dm node.
           ioc->dev is new_encode_dev: (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12) */
        unsigned long minor = (unsigned long)((ioc->dev & 0xff) | ((ioc->dev >> 12) & 0xfff00));
        char target[64];
        for (int i = 0; i < 64; i++) target[i] = 0;
        strcpyn(target, "/dev/block/dm-", 64);
        {
            char num[16];
            for (int i = 0; i < 16; i++) num[i] = 0;
            s2u(num, minor);
            long tl = slen(target);
            for (int i = 0; num[i]; i++) target[tl + i] = num[i];
        }
        char link[80];
        for (int i = 0; i < 80; i++) link[i] = 0;
        strcpyn(link, symlink_path, 80);
        long ll = slen(link);
        for (int i = 0; partitions[p].name[i] && (ll + i) < 79; i++) link[ll + i] = partitions[p].name[i];
        ksys(SYS_unlinkat, AT_FDCWD, (long)link, 0, 0);
        /* symlinkat(target, newdirfd, linkpath) */
        ksys(SYS_symlinkat, (long)target, AT_FDCWD, (long)link, 0);
        ksys(SYS_write, 1, (long)"mapped ", 7, 0);
        ksys(SYS_write, 1, (long)partitions[p].name, slen(partitions[p].name), 0);
        ksys(SYS_write, 1, (long)"\n", 1, 0);
    }
    ksys(SYS_close, dmfd, 0, 0, 0);
    return 0;
}
