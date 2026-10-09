#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "nvram.h"
#include "firmware.h"
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "NVM test line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void config(const char *extra)
{
    FILE *f = fopen("build/nv-fixture.cfg", "w"); CHECK(f != NULL);
    fputs("rom=0x1000000,nv-fixture.bin,0x10000\nflash=0x1000000,0xffff,0xffff\nreset=0x1000000\naudio=silent\n", f);
    fputs(extra, f); CHECK(fclose(f) == 0);
}
int main(void)
{
    Machine m = {0};
    FILE *f;
    uint8_t code[32] = {0xfe,0xff,0xff,0xea};
    char before[65], after[65];
    f = fopen("build/nv-fixture.bin", "wb"); CHECK(f != NULL);
    CHECK(fwrite(code, 1, sizeof(code), f) == sizeof(code)); CHECK(fclose(f) == 0);
    CHECK(firmware_sha256("build/nv-fixture.bin", before));
    config("nvram=0x1000020,0xffe0\n");
    CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    CHECK(m.nvram_data && m.nvram_size == 0xffe0 && m.nvram_data[0] == 255);
    m.nvram_data[0] = 0x12; m.nvram_data[m.nvram_size - 1] = 0x34;
    CHECK(nvram_save(&m, "build/nv-fixture.snv"));
    memset(m.nvram_data, 0x56, m.nvram_size);
    CHECK(nvram_load(&m, "build/nv-fixture.snv"));
    CHECK(m.nvram_data[0] == 0x12 && m.nvram_data[m.nvram_size - 1] == 0x34 && m.nvram_data[1] == 255);
    CHECK(!nvram_save(&m, "build/nv-fixture.bin"));
    (void)DeleteFileA("build/nv-rom-alias.bin");
    CHECK(CreateHardLinkA("build/nv-rom-alias.bin", "build/nv-fixture.bin", NULL));
    CHECK(!nvram_save(&m, "build/nv-rom-alias.bin")); CHECK(DeleteFileA("build/nv-rom-alias.bin"));
    CHECK(firmware_sha256("build/nv-fixture.bin", after) && !strcmp(before, after));
    CHECK(machine_step(&m, 0)); CHECK(!nvram_load(&m, "build/nv-fixture.snv"));
    CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    f = fopen("build/nv-fixture.snv", "r+b"); CHECK(f != NULL);
    CHECK(fseek(f, 344, SEEK_SET) == 0 && fputc(0x99, f) != EOF); CHECK(fclose(f) == 0);
    CHECK(!nvram_load(&m, "build/nv-fixture.snv")); CHECK(m.nvram_data[0] == 255 && m.nvram_data[m.nvram_size - 1] == 255);
    CHECK(nvram_save(&m, "build/nv-fixture.snv")); /* Atomic replacement of an existing snapshot. */
    f = fopen("build/nv-fixture.snv", "ab"); CHECK(f != NULL); CHECK(fputc(1, f) != EOF); CHECK(fclose(f) == 0);
    CHECK(!nvram_load(&m, "build/nv-fixture.snv")); CHECK(m.nvram_data[0] == 255);
    CHECK(nvram_save(&m, "build/nv-fixture.snv"));
    code[4] = 1;
    f = fopen("build/nv-fixture.bin", "wb"); CHECK(f != NULL);
    CHECK(fwrite(code, 1, sizeof(code), f) == sizeof(code)); CHECK(fclose(f) == 0);
    CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    CHECK(!nvram_load(&m, "build/nv-fixture.snv")); CHECK(m.nvram_data[0] == 255);
    f = fopen("build/nv-fixture.snv", "wb"); CHECK(f != NULL); CHECK(fputs("SGHNVM1", f) >= 0); CHECK(fclose(f) == 0);
    CHECK(!nvram_load(&m, "build/nv-fixture.snv"));
    f = fopen("build/nv-fixture.snv.tmp", "wb"); CHECK(f != NULL); CHECK(fputs("leave untouched", f) >= 0); CHECK(fclose(f) == 0);
    CHECK(!nvram_save(&m, "build/nv-fixture.snv")); CHECK(DeleteFileA("build/nv-fixture.snv.tmp"));
    config("nvram=0x100001e,0xffe2\n"); CHECK(!machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    config("nvram=0x1000020,0x10000\n"); CHECK(!machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    config("nvram=0x1000020,0xffe0\nnvram=0x1000020,0xffe0\n"); CHECK(!machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    config(""); CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL)); CHECK(!nvram_save(&m, "build/nv-fixture.snv"));
    /* Automatic persistence: real round trips, lock exclusion, failure preservation,
       version isolation and incomplete first-boot provisioning. */
    {
        NvramStore store = {0}, second = {0};
        char stored[1024], changed[1024], tmp[1100];
        uint64_t instructions;
        config("nvram=0x1000020,0xffe0\n");
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto"));
        strcpy(stored, store.path);
        nvram_store_release(&store); (void)DeleteFileA(stored);
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); CHECK(!store.saved);
        CHECK(!nvram_store_open(&second, &m, "build/nv-auto")); CHECK(!second.active && !second.lock);
        m.nvram_data[0] = 0x42;
        CHECK(nvram_store_flush(&store, &m)); CHECK(store.saved);
        snprintf(tmp, sizeof(tmp), "%s.tmp", store.path);
        f = fopen(tmp, "wb"); CHECK(f != NULL); CHECK(fclose(f) == 0);
        CHECK(nvram_store_flush(&store, &m)); /* Unchanged storage does not rewrite. */
        m.nvram_data[0] = 0x43; CHECK(!nvram_store_flush(&store, &m));
        CHECK(DeleteFileA(tmp)); nvram_store_release(&store);
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); CHECK(m.nvram_data[0] == 0x42);
        m.nvram_data[0] = 0x44; CHECK(nvram_store_flush(&store, &m)); nvram_store_release(&store);
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); CHECK(m.nvram_data[0] == 0x44);
        nvram_store_release(&store);
        f = fopen(stored, "r+b"); CHECK(f != NULL);
        CHECK(fseek(f, 344, SEEK_SET) == 0 && fputc(0, f) != EOF); CHECK(fclose(f) == 0);
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(!nvram_store_open(&store, &m, "build/nv-auto")); CHECK(!store.active && !store.lock);
        CHECK(m.nvram_data[0] == 255); CHECK(DeleteFileA(stored));
        code[5] ^= 1; f = fopen("build/nv-fixture.bin", "wb"); CHECK(f != NULL);
        CHECK(fwrite(code, 1, sizeof(code), f) == sizeof(code)); CHECK(fclose(f) == 0);
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); strcpy(changed, store.path);
        CHECK(strcmp(changed, stored) != 0); nvram_store_release(&store); (void)DeleteFileA(changed);
        /* Tiny Thumb factory functions return their documented success values. */
        code[8] = 0; code[9] = 0x20; code[10] = 0x70; code[11] = 0x47;
        code[12] = 1; code[13] = 0x20; code[14] = 0x70; code[15] = 0x47;
        f = fopen("build/nv-fixture.bin", "wb"); CHECK(f != NULL);
        CHECK(fwrite(code, 1, sizeof(code), f) == sizeof(code)); CHECK(fclose(f) == 0);
        config("nvram=0x1000020,0xffe0\nnv_provision=0x1000004,0x1000008,0x100000c\n");
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); strcpy(stored, store.path);
        nvram_store_release(&store); (void)DeleteFileA(stored);
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); CHECK(store.provision);
        CHECK(nvram_store_flush(&store, &m)); CHECK(GetFileAttributesA(stored) == INVALID_FILE_ATTRIBUTES);
        m.cpu.cpsr |= ARM_T; m.cpu.r[15] = m.nv_trigger; m.cpu.r[4] = 0x87654321;
        CHECK(nvram_store_step(&store, &m) == 1 && store.stage == 1);
        CHECK(machine_step(&m, 0)); CHECK(machine_step(&m, 0));
        CHECK(nvram_store_step(&store, &m) == 1 && store.stage == 2);
        CHECK(nvram_store_flush(&store, &m)); CHECK(GetFileAttributesA(stored) == INVALID_FILE_ATTRIBUTES);
        CHECK(machine_step(&m, 0)); CHECK(machine_step(&m, 0)); instructions = m.cpu.instructions;
        CHECK(nvram_store_step(&store, &m) == 2 && store.stage == 3 && store.saved);
        CHECK(m.cpu.r[4] == 0x87654321 && m.cpu.instructions == instructions && m.cpu.r[15] == m.nv_trigger);
        nvram_store_release(&store);
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto")); CHECK(!store.provision);
        store.defaults_boot = 1;
        CHECK(nvram_store_step(&store, &m) == 1 && store.defaults_boot);
        CHECK(irom_add(&m.irom,0x8077c,"memcpy")); m.irom.services[0].calls = 1;
        CHECK(nvram_store_step(&store, &m) == 1 && store.defaults_boot);
        CHECK(irom_add(&m.irom,0x81fe0,"version")); m.irom.services[1].calls = 1;
        CHECK(nvram_store_step(&store, &m) == 2 && !store.defaults_boot);
        CHECK(nvram_store_step(&store, &m) == 1); /* Exactly one settled-default reset. */
        nvram_store_release(&store); CHECK(DeleteFileA(stored));
        /* A failed guest formatter cannot commit a first-boot file. */
        CHECK(machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        CHECK(nvram_store_open(&store, &m, "build/nv-auto"));
        m.cpu.cpsr |= ARM_T; m.cpu.r[15] = m.nv_trigger;
        CHECK(nvram_store_step(&store, &m) == 1); m.cpu.r[15] = m.nv_trigger; m.cpu.r[0] = 1;
        CHECK(!nvram_store_step(&store, &m)); CHECK(nvram_store_flush(&store, &m));
        CHECK(GetFileAttributesA(stored) == INVALID_FILE_ATTRIBUTES); nvram_store_release(&store);
        config("nvram=0x1000020,0xffe0\nnv_provision=0x1000005,0x1000008,0x100000c\n");
        CHECK(!machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
        config("nvram=0x1000020,0xffe0\nnv_provision=0x1000004,0x1000040,0x100000c\n");
        CHECK(!machine_load(&m, "build/nv-fixture.cfg", NULL, NULL));
    }
    machine_destroy(&m);
    printf("NVM snapshot validation: %u checks passed.\n", checks); return 0;
}
