#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "nvram.h"
#include "firmware.h"
#include "file.h"
#include <stdlib.h>
#include <string.h>
#define IDENTITY_SIZE 312
#define HEADER_SIZE 344
static void put32(uint8_t *p, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static int fail(Machine *m, const char *reason)
{
    snprintf(m->error, sizeof(m->error), "NVM snapshot: %s", reason); return 0;
}
static int digest(const uint8_t *data, uint32_t size, uint8_t out[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) >= 0 &&
        BCryptHashData(hash, (PUCHAR)data, size, 0) >= 0 && BCryptFinishHash(hash, out, 32, 0) >= 0) ok = 1;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
static int header(Machine *m, uint8_t out[HEADER_SIZE])
{
    unsigned i;
    if (!m->loaded || !m->nvram_data || !m->nvram_size || m->nvram_size > 32u * 1024 * 1024)
        return fail(m, "model has no valid configured flash tail");
    memset(out, 0, HEADER_SIZE); memcpy(out, "SGHNVM1", 7);
    put32(out + 8, m->nvram_base); put32(out + 12, m->nvram_size); put32(out + 16, m->rom_count);
    for (i = 0; i < m->rom_count; ++i) {
        uint8_t *entry = out + 24 + i * 72;
        char hash[65];
        if (!firmware_sha256(m->roms[i].path, hash)) return fail(m, "cannot hash source firmware");
        put32(entry, m->roms[i].base); put32(entry + 4, (uint32_t)m->roms[i].size);
        memcpy(entry + 8, hash, 64);
    }
    return 1;
}
int nvram_load(Machine *m, const char *path)
{
    uint8_t expected[HEADER_SIZE], actual[HEADER_SIZE], hash[32], *data;
    FILE *f;
    int ok;
    if (m->cpu.instructions || m->cpu.cycles) return fail(m, "load requires a freshly reset machine");
    if (!header(m, expected)) return 0;
    f = fopen(path, "rb"); if (!f) return fail(m, "cannot open input file");
    if (fread(actual, 1, HEADER_SIZE, f) != HEADER_SIZE || memcmp(actual, expected, IDENTITY_SIZE)) {
        fclose(f); return fail(m, "header, firmware hashes or memory map mismatch");
    }
    data = malloc(m->nvram_size);
    if (!data) { fclose(f); return fail(m, "allocation failed"); }
    ok = fread(data, 1, m->nvram_size, f) == m->nvram_size && fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) ok = 0;
    if (ok) ok = digest(data, m->nvram_size, hash) && !memcmp(hash, actual + IDENTITY_SIZE, 32);
    if (ok) memcpy(m->nvram_data, data, m->nvram_size); /* Validate everything before mutation. */
    free(data);
    return ok ? 1 : fail(m, "truncated, oversized or damaged payload");
}
static int same_file(const char *a, const char *b)
{
    BY_HANDLE_FILE_INFORMATION ia, ib;
    HANDLE ha = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE hb = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    int same = ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib) &&
        ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
    if (ha != INVALID_HANDLE_VALUE) CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE) CloseHandle(hb);
    return same;
}
int nvram_save(Machine *m, const char *path)
{
    uint8_t h[HEADER_SIZE];
    char temporary[1100];
    HANDLE f;
    DWORD wrote;
    unsigned i;
    int ok;
    if (!header(m, h)) return 0;
    for (i = 0; i < m->rom_count; ++i) if (same_file(path, m->roms[i].path)) return fail(m, "refusing to replace a source ROM");
    if (!*path || strlen(path) > 1024 || path[strlen(path) - 1] == ' ' || path[strlen(path) - 1] == '.')
        return fail(m, "invalid output path");
    if (!digest(m->nvram_data, m->nvram_size, h + IDENTITY_SIZE)) return fail(m, "payload hashing failed");
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    f = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return fail(m, "cannot create temporary output (existing .tmp is left untouched)");
    ok = WriteFile(f, h, HEADER_SIZE, &wrote, NULL) && wrote == HEADER_SIZE &&
        WriteFile(f, m->nvram_data, m->nvram_size, &wrote, NULL) && wrote == m->nvram_size && FlushFileBuffers(f);
    if (!CloseHandle(f)) ok = 0;
    if (ok) ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) { DeleteFileA(temporary); return fail(m, "output write or atomic replacement failed"); }
    return 1;
}
void nvram_store_release(NvramStore *s)
{
    if (s->lock) CloseHandle((HANDLE)s->lock);
    memset(s, 0, sizeof(*s));
}
int nvram_store_open(NvramStore *s, Machine *m, const char *directory)
{
    uint8_t h[HEADER_SIZE], key[32];
    char hex[65], lock_path[1100], model_directory[1024], legacy[1024];
    int model_local=directory==NULL;
    DWORD attrs, error;
    unsigned i;
    if (s->active || s->lock) return fail(m, "storage session already open");
    if (!m->nvram_data) return 1;
    if (m->cpu.instructions || m->cpu.cycles) return fail(m, "storage open requires fresh reset");
    if (!header(m, h) || !digest(h, IDENTITY_SIZE, key)) return fail(m, "storage identity hashing failed");
    if(model_local) {
        char *slash,*backslash;
        strcpy(model_directory,m->config_path);
        slash=strrchr(model_directory,'/');backslash=strrchr(model_directory,'\\');
        if(backslash&&(!slash||backslash>slash))slash=backslash;
        if(slash)slash[1]=0;else *model_directory=0;
        if(strlen(model_directory)>890)return fail(m,"model storage path is too long");
        strcat(model_directory,"state");directory=model_directory;
    }
    if (!directory || !*directory || strlen(directory) > 900) return fail(m, "invalid storage directory");
    if (!CreateDirectoryA(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return fail(m, "cannot create storage directory");
    attrs = GetFileAttributesA(directory);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return fail(m, "storage directory is not a directory");
    for (i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", key[i]);
    snprintf(s->path, sizeof(s->path), "%s/%s.snv", directory, hex);
    snprintf(lock_path, sizeof(lock_path), "%s.lock", s->path);
    s->lock = CreateFileA(lock_path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->lock == INVALID_HANDLE_VALUE) { s->lock = NULL; return fail(m, "storage is locked by another emulator or not writable"); }
    attrs = GetFileAttributesA(s->path); error = GetLastError();
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!nvram_load(m, s->path)) { nvram_store_release(s); return 0; }
        s->saved = 1;
    } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        nvram_store_release(s); return fail(m, "cannot inspect saved storage");
    }
    if(model_local&&!s->saved) {
        snprintf(legacy,sizeof(legacy),"state/%s.snv",hex);
        attrs=GetFileAttributesA(legacy);error=GetLastError();
        if(attrs!=INVALID_FILE_ATTRIBUTES) {
            /* Validation includes ROM hashes/map and payload checksum. Keep
               the original intact; new writes go only to model-local storage. */
            if(!nvram_load(m,legacy)||!nvram_save(m,s->path)){nvram_store_release(s);return 0;}
            s->saved=1;
        } else if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND) {
            nvram_store_release(s);return fail(m,"cannot inspect legacy storage");
        }
    }
    if (!digest(m->nvram_data, m->nvram_size, s->saved_hash)) {
        nvram_store_release(s); return fail(m, "storage hashing failed");
    }
    s->active = 1;
    s->provision = !s->saved && m->nv_trigger != 0;
    if (s->provision) for (i = 0; i < m->nvram_size; ++i) if (m->nvram_data[i] != 255) {
        nvram_store_release(s); return fail(m, "first-boot provisioning requires erased storage");
    }
    return 1;
}
int nvram_store_flush(NvramStore *s, Machine *m)
{
    uint8_t hash[32];
    if (!s->active || (s->provision && s->stage != 3)) return 1; /* Never commit partial provisioning. */
    if (!digest(m->nvram_data, m->nvram_size, hash)) return fail(m, "storage hashing failed");
    if (s->saved && !memcmp(hash, s->saved_hash, 32)) return 1;
    if (!nvram_save(m, s->path)) return 0;
    memcpy(s->saved_hash, hash, 32); s->saved = 1;
    return 1;
}
int nvram_store_import(NvramStore *s, Machine *m, const char *path)
{
    if (!s->active) return fail(m, "no active storage session");
    if (!nvram_load(m, path)) return 0;
    s->provision = 0; s->stage = 0;
    return nvram_store_flush(s, m);
}
int nvram_store_step(NvramStore *s, Machine *m)
{
    Arm7 *c = &m->cpu;
    unsigned i;
    if (s->active && s->defaults_boot) for (i = 0; i < m->irom.count; ++i)
        if (m->irom.services[i].kind == IROM_VERSION && m->irom.services[i].calls) {
            /* Decoded C100 startup reaches the version query after its UI records
               finish loading. Persist defaults, then boot once from settled NVM. */
            if (!nvram_store_flush(s, m)) return 0;
            s->defaults_boot = 0; return 2;
        }
    if (!s->active || !s->provision || s->stage == 3) return 1;
    if (s->stage && c->instructions - s->caller.instructions > UINT64_C(100000000))
        return fail(m, "guest provisioning instruction limit exceeded");
    if (c->r[15] != m->nv_trigger) return 1;
    if (!(c->cpsr & ARM_T)) return fail(m, "provisioning expected Thumb caller");
    if (s->stage == 0) {
        s->caller = *c; s->stage = 1;
        c->r[0] = 0; c->r[14] = m->nv_trigger | 1u; c->r[15] = m->nv_format;
    } else if (s->stage == 1) {
        if (c->r[0] != 0) return fail(m, "guest factory formatting failed");
        s->stage = 2; c->r[0] = 0; c->r[14] = m->nv_trigger | 1u; c->r[15] = m->nv_marker;
    } else {
        uint64_t instructions = c->instructions, cycles = c->cycles,clock_cycles=c->clock_cycles;
        if (c->r[0] != 1) return fail(m, "guest initialization-marker write failed");
        *c = s->caller; c->instructions = instructions; c->cycles = cycles;c->clock_cycles=clock_cycles;
        s->stage = 3;
        if (!nvram_store_flush(s, m)) return 0;
        return 2; /* Service formatting precedes ordinary phone startup: cold reset required. */
    }
    return 1;
}
