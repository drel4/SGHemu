/*
 * Win32 implementation of the host layer declared in port.h.
 * Used by the Windows build.  The POSIX counterpart is port_posix.c.
 */

#define WIN32_LEAN_AND_MEAN
#include "port.h"

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

struct PlatDir {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    int first;
};

int plat_local_time(PlatDateTime *out)
{
    SYSTEMTIME now;

    GetLocalTime(&now);
    out->year = now.wYear;
    out->month = now.wMonth;
    out->day = now.wDay;
    out->hour = now.wHour;
    out->minute = now.wMinute;
    out->second = now.wSecond;
    return 1;
}

int plat_set_high_priority(int enabled)
{
    return SetPriorityClass(GetCurrentProcess(),
                            enabled ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS) != 0;
}

PlatDir *plat_dir_open(const char *path)
{
    PlatDir *dir;
    char pattern[1024];

    if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int)sizeof(pattern))
        return NULL;
    dir = calloc(1, sizeof(*dir));
    if (!dir)
        return NULL;
    dir->handle = FindFirstFileA(pattern, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

int plat_dir_next(PlatDir *dir, PlatDirEntry *entry)
{
    do {
        if (dir->first)
            dir->first = 0;
        else if (!FindNextFileA(dir->handle, &dir->data))
            return 0;
    } while ((dir->data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
             !strcmp(dir->data.cFileName, ".") || !strcmp(dir->data.cFileName, ".."));

    snprintf(entry->name, sizeof(entry->name), "%s", dir->data.cFileName);
    entry->is_directory = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entry->size = ((uint64_t)dir->data.nFileSizeHigh << 32) | dir->data.nFileSizeLow;
    return 1;
}

void plat_dir_close(PlatDir *dir)
{
    if (!dir)
        return;
    FindClose(dir->handle);
    free(dir);
}

int plat_file_status(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    DWORD error;

    if (attributes != INVALID_FILE_ATTRIBUTES)
        return 1;
    error = GetLastError();
    return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? 0 : -1;
}

int plat_is_directory(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int plat_same_file(const char *a, const char *b)
{
    BY_HANDLE_FILE_INFORMATION left, right;
    HANDLE first = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
    HANDLE second = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING, 0, NULL);
    int same = first != INVALID_HANDLE_VALUE && second != INVALID_HANDLE_VALUE &&
               GetFileInformationByHandle(first, &left) &&
               GetFileInformationByHandle(second, &right) &&
               left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
               left.nFileIndexHigh == right.nFileIndexHigh &&
               left.nFileIndexLow == right.nFileIndexLow;

    if (first != INVALID_HANDLE_VALUE)
        CloseHandle(first);
    if (second != INVALID_HANDLE_VALUE)
        CloseHandle(second);
    return same;
}

int plat_mkdir(const char *path, int exist_ok)
{
    if (CreateDirectoryA(path, NULL))
        return 1;
    return exist_ok && GetLastError() == ERROR_ALREADY_EXISTS;
}

int plat_rename_replace(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

int plat_remove(const char *path)
{
    return DeleteFileA(path) != 0;
}

int plat_hard_link(const char *target, const char *link_path)
{
    return CreateHardLinkA(link_path, target, NULL) != 0;
}

size_t plat_absolute_path(const char *path, char *out, size_t size)
{
    DWORD length = GetFullPathNameA(path, (DWORD)size, out, NULL);
    return length < size ? length : 0;
}

FILE *plat_create_new(const char *path)
{
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    int descriptor;
    FILE *file;

    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    /* The C runtime takes ownership of the handle when the descriptor opens. */
    descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(handle);
        DeleteFileA(path);
        return NULL;
    }
    file = _fdopen(descriptor, "wb");
    if (!file) {
        _close(descriptor);
        DeleteFileA(path);
    }
    return file;
}

int plat_flush(FILE *file)
{
    return fflush(file) == 0 && _commit(_fileno(file)) == 0;
}

void *plat_lock_file(const char *path)
{
    /* No sharing mode: the file is exclusively locked while it is open. */
    HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);

    return handle == INVALID_HANDLE_VALUE ? NULL : handle;
}

void plat_unlock_file(void *lock)
{
    if (lock)
        CloseHandle((HANDLE)lock);
}

int plat_stricmp(const char *a, const char *b)
{
    return _stricmp(a, b);
}
