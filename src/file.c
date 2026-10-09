#include "file.h"
#include <stdio.h>
#include <stdlib.h>
int file_read(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;
    *data = NULL; *size = 0;
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return 0; }
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f); fprintf(stderr, "Empty or unreadable file: %s\n", path); return 0;
    }
    p = malloc((size_t)n);
    if (!p) { fclose(f); return 0; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return 0; }
    fclose(f); *data = p; *size = (size_t)n; return 1;
}
uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
