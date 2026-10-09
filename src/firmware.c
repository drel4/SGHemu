#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "firmware.h"
typedef struct { FirmwareMatch match; char hash[2][65]; int found[2]; } Profile;
typedef struct { Profile profiles[FIRMWARE_MAX_MATCHES]; unsigned count, scanned; } Scan;
int firmware_sha256(const char *path, char digest[65])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    FILE *f = NULL;
    unsigned char buffer[65536], value[32];
    size_t n;
    unsigned i;
    int ok = 0;
    f = fopen(path, "rb");
    if (!f || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) < 0) goto done;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0)
        if (BCryptHashData(hash, buffer, (ULONG)n, 0) < 0) goto done;
    if (ferror(f) || BCryptFinishHash(hash, value, sizeof(value), 0) < 0) goto done;
    for (i = 0; i < 32; ++i) sprintf(digest + i * 2, "%02x", value[i]);
    ok = 1;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (f) fclose(f);
    return ok;
}
static int join(char out[1024], const char *folder, const char *name)
{
    if (strlen(folder) + strlen(name) + 2 > 1024) return 0;
    snprintf(out, 1024, "%s/%s", folder, name); return 1;
}
static int profile_read(Profile *p, const char *directory, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[2048];
    int ok = 1;
    if (!f) return 0;
    memset(p, 0, sizeof(*p));
    while (fgets(line, sizeof(line), f)) {
        char *eq, *end;
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        eq = strchr(line, '='); if (!eq) { ok = 0; break; } *eq++ = 0;
        end = strpbrk(eq, "\r\n"); if (end) *end = 0;
        if (!strcmp(line, "name")) { if (strlen(eq) >= sizeof(p->match.name)) { ok = 0; break; } strcpy(p->match.name, eq); }
        else if (!strcmp(line, "model")) { if (!join(p->match.model, directory, eq)) { ok = 0; break; } }
        else if (!strcmp(line, "sha256_1") || !strcmp(line, "sha256_2")) {
            unsigned slot = line[7] == '1' ? 0 : 1;
            if (strlen(eq) != 64 || strspn(eq, "0123456789abcdefABCDEF") != 64) { ok = 0; break; }
            strcpy(p->hash[slot], eq);
        } else { ok = 0; break; }
    }
    if (ferror(f)) ok = 0;
    fclose(f);
    return ok && *p->match.name && *p->match.model && *p->hash[0] && *p->hash[1];
}
static void profiles(Scan *s, const char *directory, unsigned depth)
{
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char pattern[1024], path[1024];
    if (!join(pattern, directory, "*")) return;
    handle = FindFirstFileA(pattern, &data); if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (data.cFileName[0] == '.' || !join(path, directory, data.cFileName)) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth && strcmp(data.cFileName, "roms")) profiles(s, path, depth - 1);
        } else {
            const char *ext = strrchr(data.cFileName, '.');
            if (ext && !_stricmp(ext, ".firmware") && s->count < FIRMWARE_MAX_MATCHES &&
                profile_read(&s->profiles[s->count], directory, path)) ++s->count;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
}
static void images(Scan *s, const char *directory, unsigned depth)
{
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char pattern[1024], path[1024], hash[65];
    if (!join(pattern, directory, "*")) return;
    handle = FindFirstFileA(pattern, &data); if (handle == INVALID_HANDLE_VALUE) return;
    do {
        unsigned i, slot;
        if (data.cFileName[0] == '.' || !join(path, directory, data.cFileName)) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth) images(s, path, depth - 1); continue; }
        if (data.nFileSizeHigh || data.nFileSizeLow > 32u * 1024 * 1024 || ++s->scanned > 512) continue;
        /* Hash regardless of basename/extension: renamed ROMs still match. */
        if (!firmware_sha256(path, hash)) continue;
        for (i = 0; i < s->count; ++i) for (slot = 0; slot < 2; ++slot)
            if (!s->profiles[i].found[slot] && !_stricmp(s->profiles[i].hash[slot], hash)) {
                strcpy(s->profiles[i].match.rom[slot], path); s->profiles[i].found[slot] = 1;
            }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
}
unsigned firmware_scan(const char *folder, const char *models_root, FirmwareMatch *matches, unsigned capacity)
{
    Scan *s = calloc(1, sizeof(*s));
    unsigned i, count = 0;
    char absolute[1024];
    DWORD length;
    if (!s) return 0;
    length = GetFullPathNameA(folder, sizeof(absolute), absolute, NULL);
    if (!length || length >= sizeof(absolute)) { free(s); return 0; }
    profiles(s, models_root, 2); images(s, absolute, 3);
    for (i = 0; i < s->count && count < capacity; ++i)
        if (s->profiles[i].found[0] && s->profiles[i].found[1]) matches[count++] = s->profiles[i].match;
    free(s); return count;
}
