/*
 * avb_disable - disable AVB2.0 verification/verity on all vbmeta partitions
 * of xiaomi monet (and re-sign them so the device still boots).
 *
 * Why this tool exists:
 *   TWRP's built-in "Disable AVB2.0" action (PartitionManager::Disable_AVB2)
 *   only touches the top-level "vbmeta" partition, only sets the
 *   HASHTREE_DISABLED bit, and never re-signs the vbmeta struct - which
 *   breaks the SHA256_RSA4096 signature and can make the device fail to
 *   boot.  This tool instead:
 *     - walks ALL five vbmeta partitions (vbmeta, vbmeta_system,
 *       vbmeta_vendor, vbmeta_product, vbmeta_odm)
 *     - sets VERIFICATION_DISABLED | HASHTREE_DISABLED on the top-level
 *       vbmeta (the only place those bits are valid; libavb rejects
 *       non-zero flags on chained vbmeta images)
 *     - re-signs the modified image with the device's stock AOSP test key
 *       (the vendor ships vbmeta signed with this key), so verification
 *       still passes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#define AVB_MAGIC "AVB0"
#define AVB_MAGIC_LEN 4
#define AVB_HEADER_SIZE 256

/* AVB_VBMETA_IMAGE_FLAGS_* */
#define AVB_FLAGS_HASHTREE_DISABLED (1U << 0)
#define AVB_FLAGS_VERIFICATION_DISABLED (1U << 1)

/* Offset of the 32-bit flags field inside AvbVBMetaImageHeader. */
#define VBMETA_FLAGS_OFFSET 120

#define KEY_PATH "/system/etc/avb_testkey_rsa4096.pem"

/* All physical vbmeta partitions on monet. */
static const char* kPartitions[] = {
    "vbmeta",
    "vbmeta_system",
    "vbmeta_vendor",
    "vbmeta_product",
    "vbmeta_odm",
};

static uint64_t rd_be64(const unsigned char* b) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
    return v;
}

static uint32_t rd_be32(const unsigned char* b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static void wr_be32(unsigned char* b, uint32_t v) {
    b[0] = (v >> 24) & 0xff;
    b[1] = (v >> 16) & 0xff;
    b[2] = (v >> 8) & 0xff;
    b[3] = v & 0xff;
}

/* Build the PKCS#1 v1.5 EMSA encoding for SHA-256 / RSA-4096:
 *   00 01 FF...FF(458) 00 <DigestInfo(SHA256)> <32-byte digest>
 * which is exactly 512 bytes.  Matches avbtool's SHA256_RSA4096 padding. */
static int make_em(const unsigned char* digest, unsigned char* out_em) {
    int p = 0;
    out_em[p++] = 0x00;
    out_em[p++] = 0x01;
    for (int i = 0; i < 458; i++) out_em[p++] = 0xff;
    out_em[p++] = 0x00;
    static const unsigned char di[] = {
        0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
        0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
        0x00, 0x04, 0x20,
    };
    memcpy(out_em + p, di, sizeof(di));
    p += (int)sizeof(di);
    if (p != 480) return 0;
    memcpy(out_em + p, digest, 32);
    return 1;
}

/* Re-sign |hdr| (256 bytes) + |aux| (aux_size bytes): compute the new
 * SHA-256 and write hash+signature into |auth| (auth_size bytes). */
static int resign_vbmeta(const unsigned char* hdr, const unsigned char* aux,
                         size_t aux_size, unsigned char* auth,
                         size_t auth_size, RSA* rsa) {
    unsigned char digest[32];
    unsigned char em[512];
    unsigned char sig[512];
    unsigned char* auth2;
    uint64_t hash_off, hash_size, sig_off, sig_size;
    int sig_len;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) return 0;
    if (EVP_DigestUpdate(ctx, hdr, AVB_HEADER_SIZE) != 1) return 0;
    if (EVP_DigestUpdate(ctx, aux, aux_size) != 1) return 0;
    if (EVP_DigestFinal_ex(ctx, digest, NULL) != 1) return 0;
    EVP_MD_CTX_free(ctx);

    if (!make_em(digest, em)) return 0;

    sig_len = RSA_private_encrypt((int)sizeof(em), em, sig, rsa, RSA_NO_PADDING);
    if (sig_len != (int)sizeof(sig)) {
        fprintf(stderr, "RSA_private_encrypt failed (%d)\n", sig_len);
        return 0;
    }

    /* Locate hash and signature slots inside the auth block. */
    hash_off = rd_be64(hdr + 32);
    hash_size = rd_be64(hdr + 40);
    sig_off = rd_be64(hdr + 48);
    sig_size = rd_be64(hdr + 56);
    if (hash_off + hash_size > auth_size || sig_off + sig_size > auth_size) {
        fprintf(stderr, "auth block layout out of range\n");
        return 0;
    }
    if (hash_size != sizeof(digest) || sig_size != sizeof(sig)) {
        fprintf(stderr, "unexpected hash/sig sizes (%llu/%llu)\n",
                (unsigned long long)hash_size, (unsigned long long)sig_size);
        return 0;
    }

    auth2 = malloc(auth_size);
    if (!auth2) return 0;
    memcpy(auth2, auth, auth_size);
    memcpy(auth2 + hash_off, digest, sizeof(digest));
    memcpy(auth2 + sig_off, sig, sizeof(sig));
    memcpy(auth, auth2, auth_size);
    free(auth2);
    return 1;
}

static int process_partition(const char* name, int is_top_level, RSA* rsa) {
    char path[256];
    const char* devdir = getenv("AVB_DISABLE_DEV_DIR");
    unsigned char* buf = NULL;
    unsigned char* auth;
    const unsigned char* aux;
    uint64_t auth_size, aux_size;
    uint32_t flags, new_flags;
    off_t sz;
    int fd = -1;
    int ok = 0;

    if (!devdir) devdir = "/dev/block/by-name";
    snprintf(path, sizeof(path), "%s/%s", devdir, name);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot open %s: %s\n", name, path, strerror(errno));
        return 0;
    }

    sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz <= 0) {
        fprintf(stderr, "%s: cannot get size\n", name);
        goto out;
    }

    buf = malloc((size_t)sz);
    if (!buf) goto out;
    if (read(fd, buf, (size_t)sz) != sz) {
        fprintf(stderr, "%s: short read\n", name);
        goto out;
    }

    if (memcmp(buf, AVB_MAGIC, AVB_MAGIC_LEN) != 0) {
        /* Partition exists but holds no vbmeta image (stock blank vbmeta_*). */
        printf("%s: skip (no vbmeta image, leaving untouched)\n", name);
        ok = 1;
        goto out;
    }

    auth_size = rd_be64(buf + 12);
    aux_size = rd_be64(buf + 20);
    flags = rd_be32(buf + VBMETA_FLAGS_OFFSET);

    if (!is_top_level) {
        /* libavb rejects non-zero flags on chained vbmeta images; once the
         * top-level vbmeta carries VERIFICATION_DISABLED the whole chain is
         * short-circuited anyway, so there is nothing to change here. */
        if (flags != 0) {
            fprintf(stderr,
                    "%s: chained vbmeta has non-zero flags 0x%x (must stay 0); "
                    "leaving untouched\n",
                    name, flags);
        } else {
            printf("%s: chained vbmeta flags=0, no change needed "
                   "(top-level disable short-circuits verification)\n", name);
        }
        ok = 1;
        goto out;
    }

    if (256 + auth_size + aux_size > (uint64_t)sz) {
        fprintf(stderr, "%s: image larger than partition\n", name);
        goto out;
    }

    auth = buf + AVB_HEADER_SIZE;
    aux = buf + AVB_HEADER_SIZE + auth_size;

    new_flags = flags | AVB_FLAGS_HASHTREE_DISABLED | AVB_FLAGS_VERIFICATION_DISABLED;
    if (flags != new_flags) {
        unsigned char hdr[AVB_HEADER_SIZE];
        memcpy(hdr, buf, AVB_HEADER_SIZE);
        wr_be32(hdr + VBMETA_FLAGS_OFFSET, new_flags);

        if (!resign_vbmeta(hdr, aux, (size_t)aux_size, auth, (size_t)auth_size,
                           rsa)) {
            fprintf(stderr, "%s: resign failed\n", name);
            goto out;
        }
        memcpy(buf, hdr, AVB_HEADER_SIZE);
        printf("%s: flags 0x%x -> 0x%x, re-signed\n", name, flags, new_flags);
    } else {
        /* Flags already fully disabled, but the signature may have been
         * invalidated by an earlier flag-only patch - re-sign anyway. */
        if (!resign_vbmeta(buf, aux, (size_t)aux_size, auth, (size_t)auth_size,
                           rsa)) {
            fprintf(stderr, "%s: resign failed\n", name);
            goto out;
        }
        printf("%s: flags already 0x%x, re-signed\n", name, flags);
    }

    if (pwrite(fd, buf, AVB_HEADER_SIZE + auth_size, 0) !=
        (ssize_t)(AVB_HEADER_SIZE + auth_size)) {
        fprintf(stderr, "%s: write failed: %s\n", name, strerror(errno));
        goto out;
    }
    fsync(fd);
    ok = 1;

out:
    if (buf) free(buf);
    if (fd >= 0) close(fd);
    return ok;
}

int main(int argc, char** argv) {
    RSA* rsa = NULL;
    FILE* kf;
    int all_ok = 1;
    int i;
    int top_processed = 0;

    (void)argc;
    (void)argv;

    const char* key_path = getenv("AVB_DISABLE_KEY");
    if (!key_path) key_path = KEY_PATH;

    kf = fopen(key_path, "r");
    if (!kf) {
        fprintf(stderr, "cannot open key %s: %s\n", key_path, strerror(errno));
        return 1;
    }
    rsa = PEM_read_RSAPrivateKey(kf, NULL, NULL, NULL);
    fclose(kf);
    if (!rsa) {
        fprintf(stderr, "cannot parse key %s\n", key_path);
        return 1;
    }
    if (RSA_size(rsa) != 512) {
        fprintf(stderr, "key is not RSA-4096 (size=%d)\n", RSA_size(rsa));
        RSA_free(rsa);
        return 1;
    }

    for (i = 0; i < (int)(sizeof(kPartitions) / sizeof(kPartitions[0])); i++) {
        int is_top = (strcmp(kPartitions[i], "vbmeta") == 0);
        if (is_top) top_processed = 1;
        if (!process_partition(kPartitions[i], is_top, rsa)) all_ok = 0;
    }

    RSA_free(rsa);

    if (!top_processed) {
        fprintf(stderr, "internal error: no top-level vbmeta in list\n");
        return 1;
    }

    if (all_ok) {
        printf("AVB2.0 disabled on all vbmeta partitions (re-signed).\n");
        return 0;
    }
    return 1;
}
