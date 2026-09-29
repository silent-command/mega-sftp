/* The current directory's listing, in bank 1 at $1C000-$1DFFF, the 8 KB
 * the crypto bank leaves below its top window: up to DL_MAX entries of
 * a kind, a size and a name, kept in order as they arrive -- directories
 * first, then the rest, each alphabetically without regard to case. A
 * server lists in whatever order its file system keeps, so the order is
 * the client's own. The program's memory holds only the index and one
 * entry at a time (dl_get). */
#ifndef DIRLIST_H
#define DIRLIST_H

#include <stdint.h>

#define DL_MAX 110
#define DL_NAME_MAX 63      /* a longer name is counted in dl_toolong and not listed */

#define DL_FILE 0
#define DL_DIR 1
#define DL_LINK 2
#define DL_OTHER 3

struct dl_entry {
  uint8_t kind;
  uint32_t size;
  char name[DL_NAME_MAX + 1];
};

extern unsigned char dl_count;
extern unsigned char dl_overflow;   /* entries past DL_MAX were dropped */
extern unsigned char dl_toolong;    /* names too long to keep */

void dl_clear(void);
/* sftp_on_name's shape: one name from the server into the list. */
void dl_add(const char *name, uint8_t kind, uint32_t size);
/* Entry idx, in display order, into a window that stays valid until the next call. */
struct dl_entry *dl_get(unsigned char idx);

#endif
