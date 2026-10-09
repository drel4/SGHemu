#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "firmware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "Firmware test line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void write_file(const char *path, const char *value)
{
    FILE *f = fopen(path, "wb"); CHECK(f); CHECK(fwrite(value, 1, strlen(value), f) == strlen(value)); CHECK(!fclose(f));
}
int main(void)
{
    FirmwareMatch matches[2];
    char hash[65], second[65], profile[512];
    CreateDirectoryA("build/import-test", NULL); CreateDirectoryA("build/profile-test", NULL);
    write_file("build/import-test/renamed-one.dat", "abc");
    CHECK(firmware_sha256("build/import-test/renamed-one.dat", hash));
    CHECK(!strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    write_file("build/import-test/renamed-two.dat", "second"); CHECK(firmware_sha256("build/import-test/renamed-two.dat", second));
    snprintf(profile, sizeof(profile), "name=Fixture\nmodel=fixture.cfg\nsha256_1=%s\nsha256_2=%s\n", hash, second);
    write_file("build/profile-test/fixture.firmware", profile);
    CHECK(firmware_scan("build/import-test", "build/profile-test", matches, 2) == 1);
    CHECK(strstr(matches[0].rom[0], "renamed-one.dat") && strstr(matches[0].rom[1], "renamed-two.dat"));
    CHECK(strstr(matches[0].model, "profile-test/fixture.cfg"));
    write_file("build/import-test/renamed-two.dat", "mismatch"); CHECK(firmware_scan("build/import-test", "build/profile-test", matches, 2) == 0);
    CHECK(DeleteFileA("build/import-test/renamed-two.dat")); CHECK(firmware_scan("build/import-test", "build/profile-test", matches, 2) == 0);
    CHECK(firmware_scan("build/import-test", "build/profile-test", matches, 0) == 0);
    CHECK(DeleteFileA("build/import-test/renamed-one.dat")); CHECK(DeleteFileA("build/profile-test/fixture.firmware"));
    printf("Firmware folder/hash detection: %u checks passed.\n", checks); return 0;
}
