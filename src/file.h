#ifndef SGH_FILE_H
#define SGH_FILE_H
#include <stddef.h>
#include <stdint.h>
int file_read(const char *path, uint8_t **data, size_t *size);
uint32_t le32(const uint8_t *p);
uint32_t be32(const uint8_t *p);
#endif
