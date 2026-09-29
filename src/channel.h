/* The connection protocol (RFC 4254), one session channel: open, the
 * sftp subsystem, data both ways with window accounting, EOF, close.
 * Bytes from the server go to ch_on_data as they arrive; its stderr
 * (extended data) is counted against the window and dropped, since it
 * would corrupt the SFTP stream. The SSH client's channel.c with the
 * subsystem in place of the pty and the shell. */
#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>

#define CH_LOCAL_WINDOW 16384u        /* what we let the server send before an adjust: everything is consumed as it lands */
#define CH_LOCAL_MAXPKT 1024u         /* larger than the transport's receive buffer allows, so the server's data fits */

extern void (*ch_on_data)(const uint8_t *p, uint16_t n);

/* Opens the session and asks for the named subsystem ("sftp"). 0 on
 * failure with ssh_error set; ssh_reason may say why. */
unsigned char ch_open_subsystem(const char *name);

/* Sends n bytes on the channel; waits for window as needed. */
unsigned char ch_send(const uint8_t *p, uint16_t n);

/* Handles whatever the server sent: data to ch_on_data, window
 * adjustments, EOF, close. Returns 0 when the channel or the connection
 * is gone (ssh_error tells which; SSH_OK with ch_closed means a normal
 * end). */
unsigned char ch_poll(void);
extern unsigned char ch_closed;
extern int ch_exit_status;            /* -1 until the server reports one */

void ch_close(void);

#endif
