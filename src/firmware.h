#ifndef SGH_FIRMWARE_H
#define SGH_FIRMWARE_H
#define FIRMWARE_MAX_MATCHES 32
typedef struct { char name[160], model[1024], rom[2][1024]; } FirmwareMatch;
/* Scans supplied folder + subfolders; exact SHA-256 matches, no binary mutation. */
unsigned firmware_scan(const char *folder, const char *models_root, FirmwareMatch *matches, unsigned capacity);
int firmware_sha256(const char *path, char digest[65]);
#endif
