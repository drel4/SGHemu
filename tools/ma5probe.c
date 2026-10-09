/* Standalone native-file-path probe. All DLL calls and loading happen on one
   dedicated thread. No downloads, voice bank, firmware or DLL redistribution. */
#define COBJMACROS
#include <windows.h>
#include <bcrypt.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "file.h"
#include "loopback.h"
#if !defined(_M_IX86)
#error MA-5 requires x86
#endif
typedef int (__cdecl *FnInit)(DWORD, DWORD, BYTE *);
typedef void (__cdecl *FnEmuTerm)(void);
typedef int (__cdecl *FnWork)(int, BYTE *, int);
typedef int (__cdecl *Fn0)(void);
typedef int (__cdecl *Fn1)(int);
typedef int (__cdecl *Fn3)(int, int, int);
typedef int (__cdecl *Fn4)(int, int, int, int);
typedef int (__cdecl *Fn5)(int, int, int, int, int);
typedef int (__cdecl *FnControl)(int, int, int, int *, int);
typedef int (__cdecl *FnLoad)(int, BYTE *, DWORD, int, void *, int);
typedef struct {
    FnInit emu_init; FnEmuTerm emu_term; FnWork init; Fn0 term;
    Fn1 create, del; FnLoad load; Fn4 open, start, device;
    FnControl control; Fn3 standby, stop, close, unload; Fn5 seek;
} Api;
typedef struct { const char *folder, *mmf; unsigned seconds, repeats; int result; } Probe;
static const char sw_hash[] = "9bd7a8e24f9fb6430b66e3a11d9f55b98549caf0ffeb42a005da851b316eac7b";
static const char hw_hash[] = "f2702720d01730b3555d042cd22edd5ff58066b59da8f11241236170f26c4732";
static int check_hash(const char *path, const char *expected)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t digest[32], *bytes = NULL;
    size_t n;
    char hex[65];
    unsigned i;
    int ok = 0;
    if (!file_read(path, &bytes, &n) || n > ULONG_MAX) goto done;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0 ||
        BCryptHashData(hash, bytes, (ULONG)n, 0) < 0 ||
        BCryptFinishHash(hash, digest, 32, 0) < 0) goto done;
    for (i = 0; i < 32; ++i) sprintf(hex + 2 * i, "%02x", digest[i]);
    printf("SHA256 %s %s\n", path, hex);
    ok = !strcmp(hex, expected);
    if (!ok) fprintf(stderr, "Refusing unpinned DLL: %s\n", path);
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    free(bytes); return ok;
}
static int version(const char *path, unsigned major, unsigned minor)
{
    DWORD dummy = 0, n = GetFileVersionInfoSizeA(path, &dummy);
    BYTE *data;
    VS_FIXEDFILEINFO *info;
    UINT size;
    int ok = 0;
    if (!n) return 0;
    data = malloc(n);
    if (!data) return 0;
    if (GetFileVersionInfoA(path, 0, n, data) && VerQueryValueA(data, "\\", (LPVOID *)&info, &size) && size >= sizeof(*info)) {
        printf("File version %u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        ok = HIWORD(info->dwFileVersionMS) == major && LOWORD(info->dwFileVersionMS) == minor && info->dwFileVersionLS == 0;
    }
    free(data); return ok;
}
/* Frame-pointer-independent ESP snapshot around each call, including compiler
   caller cleanup. /RTC1 provides an independent MSVC stack-integrity check. */
#define CALL(expression, expected) do { \
    uintptr_t esp_before, esp_after; int call_result; \
    __asm { mov esp_before, esp } \
    call_result = (expression); \
    __asm { mov esp_after, esp } \
    if (esp_before != esp_after) { fprintf(stderr, "Stack drift at %s: %ld\n", #expression, (long)(esp_after - esp_before)); goto cleanup; } \
    if (call_result != (expected)) { fprintf(stderr, "%s returned %d, expected %d\n", #expression, call_result, (expected)); goto cleanup; } \
} while (0)
#define RESOLVE(member, symbol) do { FARPROC proc = GetProcAddress(sw, symbol); \
    if (!proc) { fprintf(stderr, "Missing export %s\n", symbol); goto cleanup; } \
    _Static_assert(sizeof(api.member) == sizeof(proc), "function pointer size"); \
    memcpy(&api.member, &proc, sizeof(proc)); \
} while (0)
static unsigned __stdcall worker(void *arg)
{
    Probe *p = arg;
    Api api = {0};
    char folder[MAX_PATH], sw_path[MAX_PATH], hw_path[MAX_PATH];
    HMODULE sw = NULL, hw = NULL;
    uint8_t *mmf = NULL, *work = NULL, *magic = NULL;
    size_t size, i;
    int emu = 0, initialized = 0, created = 0, loaded = 0, opened = 0, started = 0;
    int volume = 100, key = 0, tempo = 100, teardown_ok = 1;
    DWORD length;
    p->result = 1;
    length = GetFullPathNameA(p->folder, MAX_PATH, folder, NULL);
    if (!length || length >= MAX_PATH || strlen(folder) + 20 >= MAX_PATH) goto cleanup;
    sprintf(sw_path, "%s\\M5_EmuSmw5.dll", folder); sprintf(hw_path, "%s\\M5_EmuHw.dll", folder);
    if (!check_hash(sw_path, sw_hash) || !check_hash(hw_path, hw_hash) || !version(sw_path, 2, 2) || !version(hw_path, 1, 6)) goto cleanup;
    if (!file_read(p->mmf, &mmf, &size) || size < 8 || size > UINT32_MAX || memcmp(mmf, "MMMD", 4) || (uint64_t)be32(mmf + 4) + 8 != size) {
        fprintf(stderr, "Need a complete, exactly sized MMMD/SMAF file\n"); goto cleanup;
    }
    /* Absolute paths and LOAD_WITH_ALTERED_SEARCH_PATH work on Windows 7.
       Preload the pinned hardware module before loading the software module. */
    SetDllDirectoryA("");
    hw = LoadLibraryExA(hw_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    sw = hw ? LoadLibraryExA(sw_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : NULL;
    if (!sw) { fprintf(stderr, "DLL load failed: Win32 error %lu\n", GetLastError()); goto cleanup; }
    RESOLVE(emu_init, "MaSound_EmuInitialize"); RESOLVE(emu_term, "MaSound_EmuTerminate");
    RESOLVE(init, "MaSound_Initialize"); RESOLVE(term, "MaSound_Terminate"); RESOLVE(device, "MaSound_DeviceControl");
    RESOLVE(create, "MaSound_Create"); RESOLVE(del, "MaSound_Delete"); RESOLVE(load, "MaSound_Load");
    RESOLVE(open, "MaSound_Open"); RESOLVE(control, "MaSound_Control"); RESOLVE(standby, "MaSound_Standby");
    RESOLVE(seek, "MaSound_Seek"); RESOLVE(start, "MaSound_Start"); RESOLVE(stop, "MaSound_Stop");
    RESOLVE(close, "MaSound_Close"); RESOLVE(unload, "MaSound_Unload");
    work = calloc(1024, 1); if (!work) goto cleanup;
    for (i = 0; i < 1024; ++i) if (((uintptr_t)(work + i) & 255) == 0x81) { magic = work + i; break; }
    if (!magic) goto cleanup;
    CALL(api.emu_init(48000, 2, magic), 0); emu = 1;
    CALL(api.init(0, work, 0), 0); initialized = 1;
    CALL(api.device(0x0d, 0, 0, 0), 0);
    CALL(api.device(0x05, 2, 0, 0), 0); CALL(api.device(0x06, 0, 0, 0), 0);
    CALL(api.device(0x08, 2, 0, 0), 0); CALL(api.device(0x09, 0, 0, 0), 0);
    CALL(api.create(1), 1); created = 1;
    CALL(api.load(1, mmf, (DWORD)size, 1, NULL, 0), 1); loaded = 1;
    CALL(api.open(1, 1, 0, 0), 0); opened = 1;
    CALL(api.standby(1, 1, 0), 0);
    CALL(api.control(1, 1, 0, &volume, 0), 0); CALL(api.control(1, 1, 2, &key, 0), 0); CALL(api.control(1, 1, 1, &tempo, 0), 0);
    CALL(api.seek(1, 1, 0, 0, 0), 0);
    CALL(api.start(1, 1, 1, 0), 0); started = 1;
    puts("Playback started in file mode; no separate voice bank loaded.");
    for (i = 0; i < p->repeats; ++i) CALL(api.control(1, 1, 0, &volume, 0), 0);
    printf("Stack-integrity test: %u __cdecl control calls, no ESP drift.\n", p->repeats);
    fflush(stdout);
    if (!record_loopback(p->seconds)) Sleep(p->seconds * 1000);
    p->result = 0;
cleanup:
    /* These return conventions are not confirmed: log every result and retain
       all resources until process exit if teardown fails. */
    if (started) { int result = api.stop(1, 1, 0); printf("Stop=%d\n", result); if (result) teardown_ok = 0; }
    if (opened) { int result = api.close(1, 1, 0); printf("Close=%d\n", result); if (result) teardown_ok = 0; }
    if (loaded) { int result = api.unload(1, 1, 0); printf("Unload=%d\n", result); if (result) teardown_ok = 0; }
    if (created) { int result = api.del(1); printf("Delete=%d\n", result); if (result) teardown_ok = 0; }
    if (initialized) { int result = api.term(); printf("Terminate=%d\n", result); if (result) teardown_ok = 0; }
    if (emu) api.emu_term();
    if (!teardown_ok) { fprintf(stderr, "Teardown failure: retaining buffers/modules until process exit\n"); p->result = 1; return 1; }
    free(mmf); free(work); if (sw) FreeLibrary(sw); if (hw) FreeLibrary(hw);
    return (unsigned)p->result;
}
int main(int argc, char **argv)
{
    Probe p = {0};
    uintptr_t thread;
    if (argc < 3 || argc > 5) {
        puts("ma5probe DLL_FOLDER FILE.mmf [seconds=10] [control_repeats=1000000]\n"
             "Import the pinned DLL pair manually; nothing is downloaded automatically."); return 2;
    }
    p.folder = argv[1]; p.mmf = argv[2]; p.seconds = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 10;
    p.repeats = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 10) : 1000000;
    if (!p.seconds || p.seconds > 3600 || !p.repeats || p.repeats > 10000000) return 2;
    thread = _beginthreadex(NULL, 0, worker, &p, 0, NULL);
    if (!thread) return 1;
    WaitForSingleObject((HANDLE)thread, INFINITE); CloseHandle((HANDLE)thread); return p.result;
}
