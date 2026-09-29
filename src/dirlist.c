#include <string.h>
#include "mega65/memory.h"
#include "dirlist.h"

#define DL_FAR 0x1C000UL
#define DL_SLOT 72                  /* sizeof (struct dl_entry) is 69; a round stride */

unsigned char dl_count, dl_overflow, dl_toolong;
static unsigned char order[DL_MAX]; /* display position -> slot */
static struct dl_entry win;

void dl_clear(void) { dl_count = dl_overflow = dl_toolong = 0; }

static unsigned long slot_at(unsigned char slot) { return DL_FAR + (unsigned long)slot * DL_SLOT; }

static char fold(char c) { return (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }

/* Negative when the new entry (kind, name) sorts before slot's. */
static signed char compare(uint8_t kind, const char *name, unsigned char slot)
{
  unsigned long at = slot_at(slot);
  uint8_t their = lpeek(at);
  char a, b;
  unsigned char i;
  if ((kind == DL_DIR) != (their == DL_DIR)) return (signed char)(kind == DL_DIR ? -1 : 1);
  at += 5;                                          /* the name, past kind and size */
  for (i = 0; i <= DL_NAME_MAX; i++) {
    a = fold(name[i]); b = fold((char)lpeek(at + i));
    if (a != b) return (signed char)(a < b ? -1 : 1);
    if (!a) return 0;
  }
  return 0;
}

void dl_add(const char *name, uint8_t kind, uint32_t size)
{
  unsigned char lo = 0, hi, mid, slot, i;
  if (strlen(name) > DL_NAME_MAX) { if (dl_toolong < 255) dl_toolong++; return; }
  if (dl_count >= DL_MAX) { dl_overflow = 1; return; }
  slot = dl_count;
  win.kind = kind; win.size = size;
  strcpy(win.name, name);
  lcopy((unsigned long)(unsigned int)&win, slot_at(slot), sizeof win);
  hi = dl_count;
  while (lo < hi) {                                 /* the first position that sorts after it */
    mid = (unsigned char)((lo + hi) >> 1);
    if (compare(kind, name, order[mid]) < 0) hi = mid; else lo = (unsigned char)(mid + 1);
  }
  for (i = dl_count; i > lo; i--) order[i] = order[i - 1];
  order[lo] = slot;
  dl_count++;
}

struct dl_entry *dl_get(unsigned char idx)
{
  lcopy(slot_at(order[idx]), (unsigned long)(unsigned int)&win, sizeof win);
  return &win;
}
