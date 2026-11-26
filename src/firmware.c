#include "port.h"
#include "firmware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FirmwareMatch match;
    char hash[2][65];
    int found[2];
} Profile;

typedef struct {
    Profile profiles[FIRMWARE_MAX_MATCHES];
    unsigned count, scanned;
} Scan;

int firmware_sha256(const char *path, char digest[65])
{
    PlatSha256 ctx;
    uint8_t buffer[65536], value[32];
    size_t n;
    unsigned i;
    int ok;
    FILE *f = fopen(path, "rb");

    if (!f)
        return 0;
    plat_sha256_init(&ctx);
    while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0)
        plat_sha256_update(&ctx, buffer, n);
    ok = !ferror(f);
    fclose(f);
    if (!ok)
        return 0;
    plat_sha256_final(&ctx, value);
    for (i = 0; i < 32; ++i)
        sprintf(digest + i * 2, "%02x", value[i]);
    return 1;
}

static int join(char out[1024], const char *folder, const char *name)
{
    int length = snprintf(out, 1024, "%s/%s", folder, name);
    return length >= 0 && length < 1024;
}

/* A .firmware descriptor gives two expected hashes and a model path relative
   to the descriptor's directory. Each non-comment line is key=value. */
static int profile_read(Profile *profile, const char *directory, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[2048];
    int ok = 0;

    if (!f) return 0;
    memset(profile, 0, sizeof(*profile));
    while (fgets(line, sizeof(line), f)) {
        char *value, *end;
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        value = strchr(line, '=');
        if (!value) goto done;
        *value = 0;
        ++value;
        end = strpbrk(value, "\r\n");
        if (end) *end = 0;

        if (!strcmp(line, "name")) {
            if (strlen(value) >= sizeof(profile->match.name)) goto done;
            strcpy(profile->match.name, value);
        } else if (!strcmp(line, "model")) {
            if (!join(profile->match.model, directory, value)) goto done;
        } else if (!strcmp(line, "sha256_1") || !strcmp(line, "sha256_2")) {
            unsigned slot = !strcmp(line, "sha256_1") ? 0 : 1;
            if (strlen(value) != 64 || strspn(value, "0123456789abcdefABCDEF") != 64)
                goto done;
            strcpy(profile->hash[slot], value);
        } else {
            goto done;
        }
    }
    ok = !ferror(f) && profile->match.name[0] && profile->match.model[0] &&
         profile->hash[0][0] && profile->hash[1][0];
done:
    fclose(f);
    return ok;
}

static void scan_profiles(Scan *scan, const char *directory, unsigned depth)
{
    PlatDir *dir = plat_dir_open(directory);
    PlatDirEntry entry;
    char path[1024];

    if (!dir)
        return;
    while (plat_dir_next(dir, &entry)) {
        if (entry.name[0] == '.' || !join(path, directory, entry.name))
            continue;
        if (entry.is_directory) {
            if (depth && strcmp(entry.name, "roms"))
                scan_profiles(scan, path, depth - 1);
        } else {
            const char *ext = strrchr(entry.name, '.');
            if (ext && !plat_stricmp(ext, ".firmware") && scan->count < FIRMWARE_MAX_MATCHES &&
                profile_read(&scan->profiles[scan->count], directory, path))
                ++scan->count;
        }
    }
    plat_dir_close(dir);
}

static void match_images(Scan *scan, const char *directory, unsigned depth)
{
    PlatDir *dir = plat_dir_open(directory);
    PlatDirEntry entry;
    char path[1024], hash[65];

    if (!dir)
        return;
    while (plat_dir_next(dir, &entry)) {
        unsigned i, slot;
        if (entry.name[0] == '.' || !join(path, directory, entry.name))
            continue;
        if (entry.is_directory) {
            if (depth)
                match_images(scan, path, depth - 1);
            continue;
        }
        /* Bound how much data we read from a user-selected folder. */
        if (entry.size > 32u * 1024 * 1024 || ++scan->scanned > 512)
            continue;
        /* Hash regardless of basename/extension: renamed ROMs still match. */
        if (!firmware_sha256(path, hash))
            continue;
        for (i = 0; i < scan->count; ++i) {
            Profile *profile = &scan->profiles[i];
            for (slot = 0; slot < 2; ++slot) {
                if (profile->found[slot] || plat_stricmp(profile->hash[slot], hash))
                    continue;
                strcpy(profile->match.rom[slot], path);
                profile->found[slot] = 1;
            }
        }
    }
    plat_dir_close(dir);
}

unsigned firmware_scan(const char *folder, const char *models_root,
                       FirmwareMatch *matches, unsigned capacity)
{
    Scan *scan = calloc(1, sizeof(*scan));
    unsigned i, count = 0;
    char absolute[1024];

    if (!scan)
        return 0;
    if (!plat_absolute_path(folder, absolute, sizeof(absolute))) {
        free(scan);
        return 0;
    }
    scan_profiles(scan, models_root, 2);
    match_images(scan, absolute, 3);
    /* An incomplete set cannot boot, even if one image matched. */
    for (i = 0; i < scan->count && count < capacity; ++i) {
        Profile *profile = &scan->profiles[i];
        if (profile->found[0] && profile->found[1])
            matches[count++] = profile->match;
    }
    free(scan);
    return count;
}
