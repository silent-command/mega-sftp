/* Bookmarks: SFTPMARKS on the disk the client booted from, the SSH
 * client's SSHMARKS with the directory added. A line "SFTPM1", then
 * five lines per entry: host, port, the method (p or i), user, and the
 * directory to open (empty for the login directory). No passwords. The
 * text stays in bank 1 above the font copy and is read field by field
 * into the caller's buffers. */
#ifndef MARKS_H
#define MARKS_H

#define MARKS_MAX 8
#define MARKS_NONE 0xff

extern unsigned char marks_count;
/* Scratch for one entry's fields, shared with the host screen's list. */
extern char marks_h[64], marks_p[6], marks_m[2], marks_u[32], marks_d[80];

void marks_load(unsigned char drive);
unsigned char marks_get(unsigned char i, char *host, char *port_s, char *method, char *user, char *dir);
unsigned char marks_find(const char *host, const char *port_s, const char *method, const char *user, const char *dir);
unsigned char marks_add(const char *host, const char *port_s, const char *method, const char *user, const char *dir);
unsigned char marks_remove(unsigned char i);

#endif
