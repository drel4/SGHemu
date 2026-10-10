#ifndef SGH_USERFS_H
#define SGH_USERFS_H
#include "machine.h"
#define USERFS_MAX_FILES 100
typedef struct {
    char name[13];
    uint16_t id;
    uint8_t type;
    uint32_t size;
    uint8_t metadata[28];
} UserFile;
typedef struct {
    UserFile files[USERFS_MAX_FILES];
    unsigned count,slots;
    uint64_t listed_bytes;
    char error[160];
} UserDirectory;
/* Inspector of the guest's live NLT. Never executes guest callbacks, reads
   MMIO, changes CPU/bus fault state, or modifies the flash/directory. */
int userfs_directory(const Machine *machine,UserDirectory *directory);
int userfs_export_directory(const UserDirectory *directory,const char *path);
#endif
