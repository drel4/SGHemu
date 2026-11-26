/*
 * POSIX implementation of the host layer declared in port.h.
 * Used by the Linux build.  The Win32 counterpart is port_win32.c.
 */

#define _GNU_SOURCE
#include "port.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct PlatDir {
    DIR *dir;
};

int plat_local_time(PlatDateTime *out)
{
    time_t now = time(NULL);
    struct tm value;

    if (now == (time_t)-1 || !localtime_r(&now, &value))
        return 0;
    out->year = value.tm_year + 1900;
    out->month = value.tm_mon + 1;
    out->day = value.tm_mday;
    out->hour = value.tm_hour;
    out->minute = value.tm_min;
    out->second = value.tm_sec;
    return 1;
}

int plat_set_high_priority(int enabled)
{
    /* A lower nice value means higher priority and may require privileges. */
    return setpriority(PRIO_PROCESS, 0, enabled ? -10 : 0) == 0;
}

PlatDir *plat_dir_open(const char *path)
{
    PlatDir *dir = calloc(1, sizeof(*dir));

    if (!dir)
        return NULL;
    dir->dir = opendir(path);
    if (!dir->dir) {
        free(dir);
        return NULL;
    }
    return dir;
}

int plat_dir_next(PlatDir *dir, PlatDirEntry *entry)
{
    struct dirent *item;

    while ((item = readdir(dir->dir)) != NULL) {
        struct stat info;

        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
            continue;
        if (fstatat(dirfd(dir->dir), item->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 ||
            (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)))
            continue;

        snprintf(entry->name, sizeof(entry->name), "%s", item->d_name);
        entry->is_directory = S_ISDIR(info.st_mode);
        entry->size = (uint64_t)info.st_size;
        return 1;
    }
    return 0;
}

void plat_dir_close(PlatDir *dir)
{
    if (!dir)
        return;
    if (dir->dir)
        closedir(dir->dir);
    free(dir);
}

int plat_file_status(const char *path)
{
    struct stat info;

    if (lstat(path, &info) == 0)
        return 1;
    return (errno == ENOENT || errno == ENOTDIR) ? 0 : -1;
}

int plat_is_directory(const char *path)
{
    struct stat info;

    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

int plat_same_file(const char *a, const char *b)
{
    struct stat left, right;

    return stat(a, &left) == 0 && stat(b, &right) == 0 &&
           left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

int plat_mkdir(const char *path, int exist_ok)
{
    if (mkdir(path, 0777) == 0)
        return 1;
    return exist_ok && errno == EEXIST;
}

int plat_rename_replace(const char *from, const char *to)
{
    /* POSIX rename() replaces the destination atomically. */
    return rename(from, to) == 0;
}

int plat_remove(const char *path)
{
    return unlink(path) == 0;
}

int plat_hard_link(const char *target, const char *link_path)
{
    return link(target, link_path) == 0;
}

size_t plat_absolute_path(const char *path, char *out, size_t size)
{
    char *resolved = realpath(path, NULL);
    size_t length;

    if (!resolved)
        return 0;
    length = strlen(resolved);
    if (length < size)
        memcpy(out, resolved, length + 1);
    free(resolved);
    return length < size ? length : 0;
}

FILE *plat_create_new(const char *path)
{
    /* C11 "x": create exclusively, fail if the file already exists. */
    return fopen(path, "wbx");
}

int plat_flush(FILE *file)
{
    return fflush(file) == 0 && fsync(fileno(file)) == 0;
}

void *plat_lock_file(const char *path)
{
    int *descriptor = malloc(sizeof(*descriptor));

    if (!descriptor)
        return NULL;
    *descriptor = open(path, O_RDWR | O_CREAT, 0666);
    if (*descriptor < 0) {
        free(descriptor);
        return NULL;
    }
    if (flock(*descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(*descriptor);
        free(descriptor);
        return NULL;
    }
    return descriptor;
}

void plat_unlock_file(void *lock)
{
    int *descriptor = lock;

    if (!descriptor)
        return;
    flock(*descriptor, LOCK_UN);
    close(*descriptor);
    free(descriptor);
}

int plat_stricmp(const char *a, const char *b)
{
    return strcasecmp(a, b);
}
