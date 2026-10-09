#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const char *name; const uint8_t *bytes; size_t length; } Signature;
static const Signature signatures[] = {
    {"SMAF/MMMD", (const uint8_t *)"MMMD", 4},
    {"ZIP", (const uint8_t *)"PK\003\004", 4},
    {"gzip", (const uint8_t *)"\037\213\010", 3},
    {"xz", (const uint8_t *)"\375" "7zXZ\0", 6},
    {"bzip2", (const uint8_t *)"BZh", 3},
    {"ELF", (const uint8_t *)"\177ELF", 4},
    {"PE/DOS candidate", (const uint8_t *)"MZ", 2},
    {"PNG", (const uint8_t *)"\211PNG\r\n\032\n", 8},
    {"JPEG candidate", (const uint8_t *)"\377\330\377", 3},
    {"LZ4 frame", (const uint8_t *)"\004\042\115\030", 4}
};
static int32_t branch_delta(uint32_t w)
{
    uint32_t x = (w & 0xffffffu) << 2;
    return (int32_t)(x | ((x & 0x2000000u) ? 0xfc000000u : 0));
}
static void vectors(const uint8_t *d, size_t n, uint32_t offset, uint32_t base)
{
    unsigned i;
    static const char *names[] = {"reset", "undefined", "SWI", "prefetch-abort", "data-abort", "reserved", "IRQ", "FIQ"};
    if (offset > n || n - offset < 32) return;
    printf("Vectors at file+0x%x, executing at 0x%08x:\n", offset, base);
    for (i = 0; i < 8; ++i) {
        uint32_t w = le32(d + offset + 4 * i), pc = base + 4 * i;
        printf("  %-15s %08x: %08x", names[i], pc, w);
        if ((w & 0xff000000u) == 0xea000000u) {
            uint32_t target = pc + 8 + (uint32_t)branch_delta(w);
            printf(" B 0x%08x", target);
        } else if ((w & 0xfffff000u) == 0xe59ff000u || (w & 0xfffff000u) == 0xe51ff000u) {
            int32_t delta = (int32_t)(w & 0xfffu);
            int64_t pos;
            if (!(w & (1u << 23))) delta = -delta;
            pos = (int64_t)offset + 4 * i + 8 + delta;
            if (pos >= 0 && (uint64_t)pos + 4 <= n) printf(" LDR pc -> 0x%08x", le32(d + (size_t)pos));
            else printf(" LDR pc (literal outside file)");
        } else printf(" not a recognized branch vector");
        putchar('\n');
    }
}
int main(int argc, char **argv)
{
    uint8_t *d;
    size_t n, i, j;
    uint32_t base = 0;
    int strings = 0;
    if (argc < 2) { puts("fwinspect FILE [--strings] [--base ADDRESS] [--extract-mmf OFFSET OUTPUT]"); return 2; }
    if (!file_read(argv[1], &d, &n)) return 1;
    for (i = 2; i < (size_t)argc; ++i) {
        if (!strcmp(argv[i], "--strings")) strings = 1;
        else if (!strcmp(argv[i], "--base") && i + 1 < (size_t)argc) base = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--extract-mmf") && i + 2 < (size_t)argc) {
            size_t at = (size_t)strtoul(argv[++i], NULL, 0), size;
            FILE *f;
            const char *out = argv[++i];
            if (at > n || n - at < 8 || memcmp(d + at, "MMMD", 4)) { free(d); return 2; }
            size = (size_t)be32(d + at + 4);
            if (size > n - at - 8) { free(d); return 2; }
            size += 8;
            f = fopen(out, "wb");
            if (!f) { free(d); return 1; }
            j = fwrite(d + at, 1, size, f);
            if (fclose(f) || j != size) { free(d); return 1; }
            printf("Extracted %zu bytes to %s\n", size, out); free(d); return 0;
        } else { fprintf(stderr, "Unknown/incomplete option %s\n", argv[i]); free(d); return 2; }
    }
    printf("%s: %zu bytes\nFirst 256 bytes:\n", argv[1], n);
    for (i = 0; i < n && i < 256; ++i) { if (!(i % 16)) printf("%08zx: ", i); printf("%02x%c", d[i], i % 16 == 15 ? '\n' : ' '); }
    putchar('\n');
    if (n >= 0x48 && n <= UINT32_MAX && le32(d + 0x40) == (uint32_t)n &&
        (le32(d + 0x44) & 0xffffu) == 0xcbcbu) {
        printf("Samsung split-image header candidate: length@40=%08x, marker@44=%08x.\n",
            le32(d + 0x40), le32(d + 0x44));
        puts("This identifies structure, not a guest base address or compression algorithm.");
    }
    vectors(d, n, 0, base);
    if (n >= 0x74 && (le32(d) & 0xff000000u) == 0xea000000u) {
        uint32_t target = 8 + (uint32_t)branch_delta(le32(d));
        uint32_t local = 0x54 + 8 + (uint32_t)branch_delta(le32(d + 0x54));
        if ((le32(d + 0x54) & 0xff000000u) == 0xea000000u && target >= local) {
            printf("Hypothesis: vectors at zero alias, flash base 0x%08x (reset B target %08x minus local bootstrap %x).\n", target - local, target, local);
            puts("Confirm with absolute literals and boot flow; this heuristic is not proof.");
        }
    }
    puts("Signature hits (embedded signatures do not imply a compressed container):");
    for (i = 0; i < n; ++i) {
        for (j = 0; j < sizeof(signatures) / sizeof(signatures[0]); ++j) {
            const Signature *s = &signatures[j];
            if (n - i >= s->length && !memcmp(d + i, s->bytes, s->length)) {
                printf("%08zx %s", i, s->name);
                if (j == 0 && n - i >= 8) {
                    uint32_t size = be32(d + i + 4);
                    printf(" size=%llu %s", (unsigned long long)size + 8, size <= n - i - 8 ? "bounded" : "TRUNCATED/false-positive");
                }
                putchar('\n');
            }
        }
        if (n - i >= 2 && (d[i] & 15) == 8 && (d[i] >> 4) <= 7 && (((unsigned)d[i] << 8 | d[i + 1]) % 31) == 0 && i == 0)
            puts("00000000 zlib header candidate (not validated decompression)");
    }
    if (strings) {
        puts("ASCII strings (>=6 printable bytes):");
        for (i = 0; i < n;) {
            size_t start = i;
            while (i < n && d[i] >= 32 && d[i] <= 126) ++i;
            if (i - start >= 6) printf("%08zx %.*s\n", start, (int)(i - start), (const char *)d + start);
            if (i < n) ++i;
        }
    }
    free(d); return 0;
}
