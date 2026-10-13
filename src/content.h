#ifndef SGH_CONTENT_H
#define SGH_CONTENT_H
#include "machine.h"
/* User-content import through the guest's own file writer.
   The handset's media menus list what the NLT flash filesystem holds under the
   slot type byte (6 = downloaded sound, 7 = picture; see c100-filesystem.md and
   c100-content-import.md). No factory command creates such an entry: the only
   writer is the firmware's flash_aWriteByName(name, type, data, length, ...).
   This module arms that writer without a host-side filesystem: at a verified
   post-initialization PC it calls the guest allocator, checks the name through
   the guest's own search, and hands the file to the guest writer. The guest
   builds every stored byte; the host supplies only the name and the contents. */
#define CONTENT_NAME_MAX 12
typedef struct {
    int active, stage, done, failed;
    uint32_t trigger, alloc, search, write; /* Guest entries the model declared. */
    uint32_t base, data, name_ptr, scratch, out; /* Carved from one allocation. */
    uint32_t stack; /* Borrowed from the guest task stack for stacked call arguments. */
    uint32_t size;
    unsigned type; /* 6 sound writer, 7 bitmap writer, 10 native Java installer. */
    char *jad; /* Java descriptor; separate guest allocation, freed by firmware. */
    uint32_t jad_ptr, url_ptr;
    unsigned attempt;
    char stem[CONTENT_NAME_MAX]; /* Sanitized file name before any collision suffix. */
    char name[CONTENT_NAME_MAX];
    uint8_t *file;
    Arm7 caller;
    char error[160];
} ContentImport;
/* Read `path` and take the guest entries from the model. Returns 0 with a
   message in im->error when the model cannot import or the file is unusable. */
int content_import_open(ContentImport *im, Machine *m, const char *path);
/* Arm a fresh-boot launch of the firmware's native installed-MIDlet menu. */
int content_java_menu(ContentImport *im,Machine *m);
void content_import_release(ContentImport *im);
/* Run the multi-call guest sequence. Call once per instruction, before
   machine_step. Returns 1 to continue, 0 on failure, or 2 once the guest writer
   accepted the job; the flash task commits it as the boot continues. */
int content_import_step(ContentImport *im, Machine *m);
#endif
