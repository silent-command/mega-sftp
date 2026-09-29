/* Hyppo (the MEGA65 hypervisor) services the client uses: naming a file
 * on the SD card, attaching a disk image to a drive, versions and error
 * codes. See the MEGA65 Book, appendix N. */
#ifndef M65_HYPPO_H
#define M65_HYPPO_H

extern volatile unsigned char hyppo_a, hyppo_x, hyppo_y, hyppo_z;
extern volatile unsigned char hyppo_ra, hyppo_rx, hyppo_ry, hyppo_rz, hyppo_rc;
void m65_hyppo_trap(void);

/* Returns 1 on success (the service set the carry). Results in hyppo_r*. */
unsigned char hyppo_call(unsigned char a, unsigned char x, unsigned char y, unsigned char z);

unsigned char hyppo_version(unsigned char *hy_major, unsigned char *hy_minor,
                            unsigned char *hdos_major, unsigned char *hdos_minor);
unsigned char hyppo_error(void);                 /* after a failed service */

/* The name must be ASCII, up to 31 characters; Hyppo wants it at a page
 * boundary, so it is copied to HYPPO_NAME_PAGE first. $1500 here, not
 * the FTP client's $1700, which is the crypto bank's trampoline. */
#define HYPPO_NAME_PAGE 0x1500
unsigned char hyppo_setname(const char *name);

/* Attaches the image named by hyppo_setname to F011 drive 0 (unit 8) or
 * 1 (unit 9). Tries the Hyppo 1.3 service first and the 1.2 pair second,
 * so it works on either; returns 1 and sets *how to 13 or 12, or returns
 * 0 with the error code in *how. */
unsigned char hyppo_attach(unsigned char drive, const char *image, unsigned char *how);
unsigned char hyppo_detach(unsigned char drive);

#endif
