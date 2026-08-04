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
#define SYS_openat 56
#define SYS_ioctl 29
#define SYS_write 64
#define SYS_close 57
#define SYS_exit_group 94
#define AT_FDCWD -100
#define O_RDWR 2

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

struct lp_partition {
    const char* name;
    unsigned long long super_offset; /* 512B sectors */
    unsigned long long length;       /* 512B sectors */
};

static const struct lp_partition partitions[] = {
    {"system",     2048,    2040984},
    {"vendor",     2043904, 2368256},
    {"product",    4412416, 4069208},
    {"odm",        8481792, 64312},
    {"system_ext", 8546304, 1527128},
};
#define NUM_PARTITIONS (sizeof(partitions)/sizeof(partitions[0]))

int main(void) {
    char iobuf[1024];
    char super_path[] = "/dev/block/bootdevice/by-name/super";
    char symlink_path[] = "/dev/block/mapper/";

    long fd = ksys(SYS_openat, AT_FDCWD, (long)"/dev/device-mapper", O_RDWR, 0);
    if (fd < 0) return 1;

    /* Create /dev/block/mapper dir if missing */
    ksys(83 /*mkdirat*/, AT_FDCWD, (long)"/dev/block/mapper", 0755, 0);

    /* Remove any existing logical mappings first (keep userdata) */
    for (int p = 0; p < (int)NUM_PARTITIONS; p++) {
        struct dm_ioctl* rioc = (struct dm_ioctl*)iobuf;
        for (int i = 0; i < (int)sizeof(*rioc)/4; i++) ((uint*)rioc)[i] = 0;
        rioc->version[0] = 4; rioc->version[1] = 0; rioc->version[2] = 0;
        rioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(rioc->name, partitions[p].name, 128);
        ksys(SYS_ioctl, fd, 4 /*DM_DEV_REMOVE*/, (long)rioc, 0);
    }

    for (int p = 0; p < (int)NUM_PARTITIONS; p++) {
        struct dm_ioctl* ioc = (struct dm_ioctl*)iobuf;
        struct dm_target_spec* spec = (struct dm_target_spec*)(iobuf + 320);
        char* params = (char*)(iobuf + 320 + sizeof(struct dm_target_spec));
        char devname[16];

        /* Already mapped? Try DM_DEV_STATUS via a quick create check is complex;
           instead just try create - if it fails with EBUSY, skip. */
        for (int i = 0; i < (int)sizeof(*ioc)/4; i++) ((uint*)ioc)[i] = 0;
        ioc->version[0] = 4; ioc->version[1] = 0; ioc->version[2] = 0;
        ioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(ioc->name, partitions[p].name, 128);
        long r = ksys(SYS_ioctl, fd, DM_DEV_CREATE, (long)ioc, 0);
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

        r = ksys(SYS_ioctl, fd, DM_TABLE_LOAD, (long)ioc, 0);
        if (r != 0) continue;

        /* RESUME */
        for (int i = 0; i < (int)sizeof(*ioc)/4; i++) ((uint*)ioc)[i] = 0;
        ioc->version[0] = 4; ioc->version[1] = 0; ioc->version[2] = 0;
        ioc->data_size = sizeof(struct dm_ioctl);
        strcpyn(ioc->name, partitions[p].name, 128);
        r = ksys(SYS_ioctl, fd, DM_DEV_SUSPEND, (long)ioc, 0);
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
        ksys(57 /*unlink*/, AT_FDCWD, (long)link, 0, 0);
        /* symlinkat(target, newdirfd, linkpath) */
        ksys(58, (long)target, AT_FDCWD, (long)link, 0);
        ksys(SYS_write, 1, (long)"mapped ", 7, 0);
        ksys(SYS_write, 1, (long)partitions[p].name, slen(partitions[p].name), 0);
        ksys(SYS_write, 1, (long)"\n", 1, 0);
    }
    ksys(SYS_close, fd, 0, 0, 0);
    return 0;
}
