#ifndef SGH_PORT_H
#define SGH_PORT_H

/* Host services shared by the frontends and emulator. Make links exactly
   one implementation: port_win32.c on Windows or port_posix.c on Linux. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int year, month, day, hour, minute, second;
} PlatDateTime;

/* Local wall-clock time. Returns 1 on success. */
int plat_local_time(PlatDateTime *out);

/* Ask the OS for a higher scheduling priority. Returns 1 on success. */
int plat_set_high_priority(int enabled);

/* Iterate through regular files and subdirectories, skipping links, "." and "..". */
typedef struct PlatDir PlatDir;

typedef struct {
    char name[260];
    int is_directory;
    uint64_t size;
} PlatDirEntry;

PlatDir *plat_dir_open(const char *path);
int plat_dir_next(PlatDir *dir, PlatDirEntry *entry);
void plat_dir_close(PlatDir *dir);

/* 1 = exists, 0 = not found, -1 = cannot inspect (permissions, I/O error). */
int plat_file_status(const char *path);
int plat_is_directory(const char *path);
/* Compare file identity so hard links and alternate paths still match. */
int plat_same_file(const char *a, const char *b);

/* Create a directory. If exist_ok is set, an existing path is accepted. */
int plat_mkdir(const char *path, int exist_ok);

/* Atomically replace `to` with `from` (same filesystem). */
int plat_rename_replace(const char *from, const char *to);
int plat_remove(const char *path);

/* Create `link_path` as a hard link to `target`. Returns 1 on success. */
int plat_hard_link(const char *target, const char *link_path);

/* Absolute path of `path`. Returns the length, or 0 if it cannot be built. */
size_t plat_absolute_path(const char *path, char *out, size_t size);

/* Create a file that must not exist yet. Returns NULL on any failure. */
FILE *plat_create_new(const char *path);

/* Flush the stdio buffer, then ask the OS to flush the file to disk. */
int plat_flush(FILE *file);

/* Try to take an exclusive lock without waiting. The lock stays held until
   plat_unlock_file(); the presence of the lock file alone means nothing. */
void *plat_lock_file(const char *path);
void plat_unlock_file(void *lock);

/* Case-insensitive string compare, used for file extensions. */
int plat_stricmp(const char *a, const char *b);

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} PlatSha256;

/* Initialize once, update with each chunk of input, then finalize the digest. */
void plat_sha256_init(PlatSha256 *ctx);
void plat_sha256_update(PlatSha256 *ctx, const void *data, size_t size);
void plat_sha256_final(PlatSha256 *ctx, uint8_t digest[32]);

#endif /* SGH_PORT_H */
