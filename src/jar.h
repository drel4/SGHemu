#ifndef SGH_JAR_H
#define SGH_JAR_H
#include <stddef.h>
#include <stdint.h>
/* Host-side package preparation only. Installation must run the guest VM. */
typedef struct {
    uint8_t *archive;
    size_t size;
    char *jad;
    char name[256], vendor[256], version[64], class_name[256];
    char error[160];
} JarPackage;
int jar_open(JarPackage *jar, const char *path);
void jar_release(JarPackage *jar);
#endif
