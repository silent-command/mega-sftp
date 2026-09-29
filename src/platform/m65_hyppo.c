#include "m65_hyppo.h"
#include "mega65/memory.h"

volatile unsigned char hyppo_a, hyppo_x, hyppo_y, hyppo_z;
volatile unsigned char hyppo_ra, hyppo_rx, hyppo_ry, hyppo_rz, hyppo_rc;

unsigned char hyppo_call(unsigned char a, unsigned char x, unsigned char y, unsigned char z)
{
  hyppo_a = a; hyppo_x = x; hyppo_y = y; hyppo_z = z;
  m65_hyppo_trap();
  return hyppo_rc;
}

unsigned char hyppo_version(unsigned char *hy_major, unsigned char *hy_minor,
                            unsigned char *hdos_major, unsigned char *hdos_minor)
{
  unsigned char ok = hyppo_call(0x00, 0, 0, 0);
  *hy_major = hyppo_ra; *hy_minor = hyppo_rx; *hdos_major = hyppo_ry; *hdos_minor = hyppo_rz;
  return ok;
}

unsigned char hyppo_error(void)
{
  hyppo_call(0x38, 0, 0, 0);
  return hyppo_ra;
}

unsigned char hyppo_setname(const char *name)
{
  unsigned char i;
  for (i = 0; i < 31 && name[i]; i++)
    POKE(HYPPO_NAME_PAGE + i, name[i]);
  POKE(HYPPO_NAME_PAGE + i, 0);
  return hyppo_call(0x2E, 0, (unsigned char)(HYPPO_NAME_PAGE >> 8), 0);
}

unsigned char hyppo_attach(unsigned char drive, const char *image, unsigned char *how)
{
  if (!hyppo_setname(image)) { *how = hyppo_error(); return 0; }
  if (hyppo_call(0x4A, drive & 1, 0, 0)) { *how = 13; return 1; }   /* hyppo_attach, 1.3 */
  if (hyppo_call(drive ? 0x46 : 0x40, 0, 0, 0)) { *how = 12; return 1; } /* d81attach0/1, 1.2 */
  *how = hyppo_error();
  return 0;
}

unsigned char hyppo_detach(unsigned char drive)
{
  if (hyppo_call(0x4A, (unsigned char)(0x80 | (drive & 1)), 0, 0)) return 1;
  return hyppo_call(0x42, 0, 0, 0);                                   /* d81detach, 1.2: both */
}
