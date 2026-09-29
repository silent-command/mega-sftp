/* The SFTP protocol, version 3 (draft-ietf-secsh-filexfer-02, which is
 * what OpenSSH speaks), over the SSH session channel's "sftp"
 * subsystem. One request is outstanding at a time and one handle is
 * open at a time, a directory or a file.
 *
 * Replies are parsed as they stream in, a field at a time, and never
 * held whole: a directory listing from OpenSSH is up to a hundred names
 * in one reply, some 15 KB, and the channel delivers it a kilobyte at a
 * time. Each name goes to sftp_on_name and each run of file bytes to
 * sftp_on_data as it arrives. */
#ifndef SFTP_H
#define SFTP_H

#include <stdint.h>

#define SFTP_PATH_MAX 255           /* a path or name longer than this is cut, and the call fails */

#define SFTP_KIND_FILE 0
#define SFTP_KIND_DIR 1
#define SFTP_KIND_LINK 2
#define SFTP_KIND_OTHER 3           /* a device, a socket, a pipe */

#define SFTP_E_NONE 0
#define SFTP_E_SSH 1                /* the connection failed: ssh_error says how */
#define SFTP_E_STATUS 2             /* the server said no: sftp_code and sftp_message */
#define SFTP_E_PROTOCOL 3           /* a reply this client cannot use */
#define SFTP_E_TIMEOUT 4            /* thirty seconds without a byte */
#define SFTP_E_TOOLONG 5            /* a path longer than SFTP_PATH_MAX */

/* The server's status codes that callers act on. */
#define SFTP_FX_OK 0
#define SFTP_FX_EOF 1
#define SFTP_FX_NO_SUCH_FILE 2
#define SFTP_FX_PERMISSION_DENIED 3
#define SFTP_FX_FAILURE 4

extern unsigned char sftp_error;
extern uint32_t sftp_code;          /* the last STATUS reply's code */
extern char sftp_message[64];       /* its message, cut to fit */

/* Opens the subsystem and agrees version 3. */
unsigned char sftp_start(void);

/* The server's canonical form of `path` ("." is the login directory)
 * into `out`, which holds SFTP_PATH_MAX + 1. */
unsigned char sftp_realpath(const char *path, char *out);

/* A directory: open it, then read until readdir returns 0 (the end) or
 * -1 (an error). Every name but "." and ".." goes to sftp_on_name;
 * `name` is cut to SFTP_PATH_MAX, and `size` is 0xFFFFFFFF past 4 GB. */
extern void (*sftp_on_name)(const char *name, uint8_t kind, uint32_t size);
unsigned char sftp_opendir(const char *path);
signed char sftp_readdir(void);

/* A file: open it for reading, or for writing (created, or emptied if
 * it is there). Reads deliver to sftp_on_data and set *got; 0 bytes is
 * the end of the file. */
extern void (*sftp_on_data)(const uint8_t *p, uint16_t n);
unsigned char sftp_open_read(const char *path);
unsigned char sftp_open_write(const char *path);
unsigned char sftp_read(uint32_t offset, uint16_t len, uint16_t *got);
unsigned char sftp_write(uint32_t offset, const uint8_t *p, uint16_t n);

/* Closes the open directory or file. */
unsigned char sftp_close(void);

/* What `path` is, following a link; the size when it is a file. */
unsigned char sftp_stat(const char *path, uint8_t *kind, uint32_t *size);

unsigned char sftp_remove(const char *path);
unsigned char sftp_mkdir(const char *path);
unsigned char sftp_rmdir(const char *path);
unsigned char sftp_rename(const char *from, const char *to);

/* Words for the last failure: the server's message for a STATUS. */
const char *sftp_error_text(void);

/* Called once a frame while a reply is awaited: a spinner, a key check. */
extern void (*sftp_idle)(void);

#endif
