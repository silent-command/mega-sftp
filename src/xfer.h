/* Moving files between the server and a drive: the drive chooser, a
 * download to a disk, an upload from one. The FTP client's module over
 * the SFTP calls. Prompts use the info and status rows; the caller
 * redraws them afterwards. */
#ifndef XFER_H
#define XFER_H

#include "dirlist.h"

/* Where downloads go: F011 drive 0 (unit 8) or 1 (unit 9). */
extern unsigned char xfer_drive;
/* The image attached to unit 9 by name, or empty. */
extern char xfer_image[32];

/* The D key. Returns 1 if the choice changed. */
unsigned char xfer_choose_drive(void);

/* Text for the info row: "save to unit 9 (NAME.D81)". */
void xfer_drive_text(char *out);

/* Fetches `e`, whose remote path the caller builds, onto the chosen
 * drive. Returns 1 when a file was written; the status row says what
 * happened either way. */
unsigned char xfer_get(const struct dl_entry *e, const char *remote);

/* Sends a file from unit 8 or 9 (picked from that disk's directory) to
 * the directory `dir` on the server. Uses the listing rows for the
 * picker; the caller redraws its page afterwards. Returns 1 when the
 * upload completed. */
unsigned char xfer_put(const char *dir);

#endif
