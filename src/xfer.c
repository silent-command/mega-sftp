/* The FTP client's xfer.c on the SFTP calls: downloads stream through
 * sftp_on_data into the CBM DOS writer, uploads read the disk a block
 * at a time and go out as WRITE requests. One transfer at a time, and
 * the SSH connection is the transport, so there is no second socket
 * and no passive mode to arrange. */
#include <string.h>
#include "mega65/memory.h"
#include "m65_cbmdos.h"
#include "m65_hyppo.h"
#include "transport.h"
#include "sftp.h"
#include "ui.h"
#include "xfer.h"

#define ROW_PROMPT ((unsigned char)(scr_rows - 3))
#define READ_LEN 1024               /* one DATA reply: past the channel packet, under the window */

unsigned char xfer_drive;
char xfer_image[32];

#define answer ((char *)0x15D8)      /* 32 bytes, lowmap.h */
#define ANSWER_CAP 32                /* never sizeof on a fixed address */
static char text[81];
static unsigned char buf[256];      /* an upload's block: cbmdos_read_next fills at most 254 */
static char from_s[2] = "8";        /* the upload's source drive, remembered */

static const char *cbmdos_text(unsigned char err)
{
  switch (err) {
  case CBMDOS_ERR_IO: return "disk read or write failed (no disk in that unit?)";
  case CBMDOS_ERR_FULL: return "disk full";
  case CBMDOS_ERR_DIRFULL: return "directory full";
  case CBMDOS_ERR_EXISTS: return "a file of that name is already there";
  case CBMDOS_ERR_PROT: return "disk is write protected";
  default: return "disk error";
  }
}

static void upper(char *s)
{
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

void xfer_drive_text(char *out)
{
  strcpy(out, xfer_drive ? "save to unit 9" : "save to unit 8");
  if (xfer_drive && xfer_image[0]) { strcat(out, " ("); strcat(out, xfer_image); strcat(out, ")"); }
}

unsigned char xfer_choose_drive(void)
{
  unsigned char how;

  ui_line(ROW_PROMPT, "Save to: 8 or 9 for that unit, or the name of a .D81 on the SD card to attach to unit 9", 0);
  if (xfer_drive && xfer_image[0]) strcpy(answer, xfer_image);
  else strcpy(answer, xfer_drive ? "9" : "8");
  if (!ui_read_line(UI_ROW_STATUS, "Drive: ", answer, ANSWER_CAP - 1, 0) || !answer[0]) {
    ui_status("drive unchanged", 0);
    return 0;
  }
  if (!strcmp(answer, "8")) { xfer_drive = 0; ui_status("saving to unit 8", 0); return 1; }
  if (!strcmp(answer, "9")) { xfer_drive = 1; ui_status("saving to unit 9", 0); return 1; }

  upper(answer);
  if (!strstr(answer, ".D81")) strcat(answer, ".D81");
  ui_status("attaching ", answer);
  if (!hyppo_attach(1, answer, &how)) {
    ui_put_ulong(text, how);
    ui_status("attach failed; is that name on the SD card? Hyppo error ", text);
    return 0;
  }
  xfer_drive = 1;
  strcpy(xfer_image, answer);
  strcpy(text, answer);
  strcat(text, " attached to unit 9; saving there");
  ui_status(text, 0);
  return 1;
}

/* A CBM name from the server's: upper case, sixteen characters, the
 * characters a directory entry cannot hold replaced. */
static void suggest_name(const char *from, char *to)
{
  unsigned char n = 0;
  char c;
  while ((c = *from++) != 0 && n < 16) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' '))
      c = '-';
    to[n++] = c;
  }
  to[n] = 0;
  if (!n) strcpy(to, "DOWNLOAD");
}

static unsigned char ends_with_prg(const char *s)
{
  unsigned char n = (unsigned char)strlen(s);
  return n >= 4 && !strcmp(s + n - 4, ".PRG");
}

static void progress(const char *verb, unsigned long got, unsigned long of)
{
  char num[12];
  strcpy(text, verb);
  ui_put_ulong(num, got);
  strcat(text, num);
  if (of) { strcat(text, " of "); ui_put_ulong(num, of); strcat(text, num); }
  strcat(text, " bytes   (RUN/STOP cancels)");
  ui_status(text, 0);
}

/* The download's landing: every DATA reply's bytes into the open file.
 * A disk error is remembered rather than acted on mid-reply, since the
 * reply must be consumed whole to keep the stream in step. */
static unsigned char disk_err;
static unsigned long got_bytes;

static void save_data(const uint8_t *p, uint16_t n)
{
  /* a pointer walk, not a 16-bit counter: the loop miscompile of ssh
   * 5.6 hit the counted form here (tools/orphan_rmw.py caught it) */
  const uint8_t *end = p + n;
  if (disk_err) return;
  got_bytes += n;
  while (p < end) {
    unsigned char e = cbmdos_put(*p++);
    if (e != CBMDOS_OK) { disk_err = e; return; }
  }
}

unsigned char xfer_get(const struct dl_entry *e, const char *remote)
{
  static char name[17];
  unsigned char type, err, k, cancelled = 0;
  uint16_t n;
  unsigned long shown = 0, size = e->size;

  if (e->kind == DL_DIR) { ui_status("that is a directory; RETURN opens it", 0); return 0; }

  xfer_drive_text(text);
  strcat(text, "; D changes it. Name on the disk (16 characters):");
  ui_line(ROW_PROMPT, text, 0);
  suggest_name(e->name, name);
  if (!ui_read_line(UI_ROW_STATUS, "Save as: ", name, 16, 0) || !name[0]) { ui_status("cancelled", 0); return 0; }
  upper(name);

  type = ends_with_prg(name) ? CBMDOS_TYPE_PRG : CBMDOS_TYPE_SEQ;
  ui_line(ROW_PROMPT, "File type: S = SEQ (text, data)   P = PRG (a program)   RETURN keeps the default", 0);
  ui_status(type == CBMDOS_TYPE_PRG ? "Type [PRG]: " : "Type [SEQ]: ", 0);
  ui_flush_keys();
  k = ui_wait_key();
  if (k == KEY_STOP) { ui_status("cancelled", 0); return 0; }
  if (k == 'p' || k == 'P') type = CBMDOS_TYPE_PRG;
  if (k == 's' || k == 'S') type = CBMDOS_TYPE_SEQ;

  ui_status("opening the file on the disk...", 0);
  err = cbmdos_create_as(name, xfer_drive, type);
  if (err == CBMDOS_ERR_EXISTS) {
    ui_line(ROW_PROMPT, "That name is already on the disk.  O = overwrite it   anything else cancels", 0);
    ui_status("Overwrite? ", 0);
    ui_flush_keys();
    k = ui_wait_key();
    if (k != 'o' && k != 'O') { ui_status("cancelled", 0); return 0; }
    err = cbmdos_delete(name, xfer_drive);
    if (err == CBMDOS_OK) err = cbmdos_create_as(name, xfer_drive, type);
  }
  if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); return 0; }

  ui_status("asking the server...", 0);
  if (!sftp_open_read(remote)) {
    cbmdos_close(); cbmdos_delete(name, xfer_drive);
    ui_status("get: ", sftp_error_text());
    return 0;
  }

  disk_err = 0; got_bytes = 0;
  sftp_on_data = save_data;
  progress("getting: ", 0, size);
  ui_flush_keys();
  for (;;) {
    if (!sftp_read(got_bytes, READ_LEN, &n)) {
      sftp_close(); cbmdos_close(); cbmdos_delete(name, xfer_drive);
      ui_spin_clear();
      ui_status("get: ", sftp_error_text());
      return 0;
    }
    if (disk_err) break;
    if (!n) break;                                  /* the end of the file */
    if (got_bytes - shown >= 2048) { shown = got_bytes; progress("getting: ", got_bytes, size); }
    ui_spin();
    if (ui_key() == KEY_STOP) { cancelled = 1; break; }
  }
  sftp_on_data = 0;
  sftp_close();
  if (disk_err || cancelled) {
    cbmdos_close(); cbmdos_delete(name, xfer_drive);
    ui_spin_clear();
    if (disk_err) ui_status("disk: ", cbmdos_text(disk_err));
    else ui_status("cancelled; the partial file was removed", 0);
    return 0;
  }
  ui_status("closing the file...", 0);
  err = cbmdos_close();
  ui_spin_clear();
  if (err != CBMDOS_OK) { cbmdos_delete(name, xfer_drive); ui_status("disk: ", cbmdos_text(err)); return 0; }
  {
    char num[12];
    strcpy(text, "saved ");
    strcat(text, name);
    strcat(text, xfer_drive ? " on unit 9: " : " on unit 8: ");
    ui_put_ulong(num, got_bytes); strcat(text, num); strcat(text, " bytes, ");
    ui_put_ulong(num, cbmdos_blocks()); strcat(text, num); strcat(text, " blocks");
    ui_status(text, 0);
  }
  return 1;
}

/* ---- uploads ----------------------------------------------------------- */

unsigned char xfer_put(const char *dir)
{
  static char local[17];
#define remote ((char *)0x1C40)      /* the shared name page (lowmap.h): built, copied into the request, then only shown */
  unsigned char drive, err, ok = 1;
  unsigned int n;
  unsigned long sent = 0, shown = 0;

  ui_line(ROW_PROMPT, "Send from unit 8 or 9: the drive, then the file's name on that disk", 0);
  if (!ui_read_line(UI_ROW_STATUS, "From: ", from_s, sizeof from_s - 1, 0) || !from_s[0]) { ui_status("cancelled", 0); return 0; }
  if (strcmp(from_s, "8") && strcmp(from_s, "9")) { ui_status("8 or 9", 0); return 0; }
  drive = (unsigned char)(from_s[0] == '9');
  local[0] = 0;
  if (!ui_read_line(UI_ROW_STATUS, "File: ", local, 16, 0) || !local[0]) { ui_status("cancelled", 0); return 0; }
  { char *c; for (c = local; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32); }

  /* the remote name under `dir`: the picked name, editable */
  {
    unsigned char base;
    strcpy(remote, dir);
    base = (unsigned char)strlen(remote);
    if (base && remote[base - 1] != '/') { remote[base++] = '/'; remote[base] = 0; }
    if (base + 17 > SFTP_PATH_MAX + 1) { ui_status("the directory's path is too long", 0); return 0; }   /* never sizeof: remote is a fixed address (lowmap.h) */
    strcpy(remote + base, local);
    ui_line(ROW_PROMPT, "The name to give it on the server:", 0);
    if (!ui_read_line(UI_ROW_STATUS, "Send as: ", remote + base, 16, 0) || !remote[base]) { ui_status("cancelled", 0); return 0; }
  }

  err = cbmdos_open_read(local, drive);
  if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); return 0; }

  ui_status("asking the server...", 0);
  if (!sftp_open_write(remote)) {
    cbmdos_close_read();
    ui_status("put: ", sftp_error_text());
    return 0;
  }

  progress("sending: ", 0, 0);
  ui_flush_keys();
  for (;;) {
    n = cbmdos_read_next(buf, &err);
    if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); ok = 0; break; }
    if (!n) break;
    if (!sftp_write(sent, buf, (uint16_t)n)) { ui_status("put: ", sftp_error_text()); ok = 0; break; }
    sent += n;
    if (sent - shown >= 2048) { shown = sent; progress("sending: ", sent, 0); }
    ui_spin();
    if (ui_key() == KEY_STOP) { ui_status("cancelled; the partial file is on the server", 0); ok = 0; break; }
  }
  cbmdos_close_read();
  if (!sftp_close()) { if (ok) { ui_status("put: ", sftp_error_text()); ok = 0; } }
  ui_spin_clear();
  if (!ok) return 0;
  {
    char num[12];
    strcpy(text, "sent ");
    strcat(text, remote);
    strcat(text, ": ");
    ui_put_ulong(num, sent); strcat(text, num); strcat(text, " bytes");
    ui_status(text, 0);
  }
  return 1;
}
