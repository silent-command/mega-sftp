/* MEGA65 SFTP client: the SSH client's connection and login, the FTP
 * client's browser on top, and the SFTP protocol between them. Connect,
 * check the host key against KNOWNHOSTS, log in with a password or the
 * Ed25519 identity, open the file service, list the login directory and
 * move through the server's file system; RETURN enters a directory or
 * fetches a file, P sends one. REQUIREMENTS.md 4. */
#include <string.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "m65_screen.h"
#include "m65_boot.h"
#include "m65_exit.h"
#include "ckit.h"
#include "ui.h"
#include "rnd.h"
#include "hosts.h"
#include "ident.h"
#include "netutil.h"
#include "transport.h"
#include "channel.h"
#include "sftp.h"
#include "dirlist.h"
#include "xfer.h"

#define SFTPC_VERSION "0.1.2"

#define ROW_FIRST 2
#define ROWS_PER_PAGE ((unsigned char)(scr_rows - 5))
#define ROW_INFO ((unsigned char)(scr_rows - 3))
#define NAME_COLS 58

static char host[64];
static char port_s[6];
static char user[32];
static char pass[32];
static char auth_s[2];
static unsigned char ip[4];
static unsigned int port;
/* the current directory at $1400 and the joined path sharing the
 * parser's name page (lowmap.h; sftp.c says why the share is safe) */
#define path ((char *)0x1400)
#define joined ((char *)0x1C40)
static unsigned char selected, top;
#define line ui_scratch

static void status_cb(const char *what) { ui_status(what, 0); }
static void spin_cb(void) { ui_spin(); }

/* ---- the host key screen (the SSH client's) ---------------------------- */

static const char hexd[] = "0123456789abcdef";

static unsigned char ask_hostkey(const uint8_t key[32], unsigned char changed)
{
  static uint8_t fp[32];
  unsigned i, k;
  ck_sha256_init(2); ck_sha256_update(2, key, 32); ck_sha256_final(2, fp);
  ui_clear_rows(1, UI_ROW_LAST_BODY);
  ui_line(2, changed ? "WARNING: this host's key has CHANGED since it was last seen." : "This host is not in KNOWNHOSTS yet.", 0);
  ui_line(3, "Its Ed25519 key, as SHA-256 of the raw key bytes:", 0);
  strcpy(line, "  ");
  for (i = 0; i < 16; i++) { line[2 + 3 * i] = hexd[fp[i] >> 4]; line[3 + 3 * i] = hexd[fp[i] & 15]; line[4 + 3 * i] = ' '; }
  line[2 + 3 * 16] = 0; ui_line(5, line, 0);
  strcpy(line, "  ");
  for (i = 16; i < 32; i++) { k = i - 16; line[2 + 3 * k] = hexd[fp[i] >> 4]; line[3 + 3 * k] = hexd[fp[i] & 15]; line[4 + 3 * k] = ' '; }
  line[2 + 3 * 16] = 0; ui_line(6, line, 0);
  ui_line(8, changed ? "A changed key can mean a new server, or an attacker in between."
                     : "Compare it with what the server's owner published.", 0);
  ui_status(changed ? "Y to connect anyway (the key is NOT saved), anything else to refuse" : "Y to trust and remember this host, anything else to refuse", 0);
  ui_flush_keys();
  k = ui_wait_key();
  rnd_stir((uint8_t)k);
  ui_clear_rows(1, UI_ROW_LAST_BODY);
  return (k == 'y' || k == 'Y');
}

/* ---- the identity screen (the SSH client's) ---------------------------- */

static void show_identity(void)
{
  char *text = ui_scratch;
  ui_clear_rows(1, UI_ROW_LAST_BODY);
  if (ident_present) {
    ident_public_b64(text);
    ui_line(2, "Your identity, as OpenSSH writes it, on two lines:", 0);
    ui_line(4, "ssh-ed25519", 0);
    ui_line(5, text, 0);
    ident_fingerprint_b64(text);
    ui_line(7, "SHA256:", text);
    ui_line(9, "Put it in ~/.ssh/authorized_keys on a server to log in.", 0);
    ui_status("R replaces it for good, any other key goes back", 0);
  } else {
    ui_line(2, "There is no identity on the disk yet.", 0);
    ui_status("G generates one (about ten seconds), any other key goes back", 0);
  }
}

static unsigned char identity_screen(void)
{
  unsigned char k;
  for (;;) {
    show_identity();
    ui_flush_keys();
    k = ui_wait_key();
    rnd_stir((uint8_t)k);
    if (ident_present ? (k == 'r' || k == 'R') : (k == 'g' || k == 'G')) {
      ui_status("generating the identity, about ten seconds...", 0);
      if (!ident_generate()) ui_status("the identity could not be written to the disk", 0);
      continue;
    }
    ui_clear_rows(1, UI_ROW_LAST_BODY);
    return ident_present;
  }
}

/* ---- drawing ----------------------------------------------------------- */

static void draw_title(void)
{
  strcpy(line, "sftp://");
  strncat(line, user, 20);
  strcat(line, "@");
  strncat(line, host, 30);
  if (port != 22) { strcat(line, ":"); strcat(line, port_s); }
  strncat(line, path, 78 - strlen(line));
  ui_line(UI_ROW_TITLE, line, 0);
}

static void draw_keys(void)
{
  ui_line(UI_ROW_KEYS, "RETURN open/get   U up   P put   D drive   R relist   MEGA-F/B color   HELP host", 0);
}

static void draw_entry(unsigned char idx, unsigned char row)
{
  struct dl_entry *e = dl_get(idx);
  unsigned char n = 0;
  const char *p;

  switch (e->kind) {
  case DL_DIR: p = "[DIR] "; break;
  case DL_LINK: p = "[LNK] "; break;
  case DL_OTHER: p = "[???] "; break;
  default: p = "      "; break;
  }
  while (*p) line[n++] = *p++;
  for (p = e->name; *p && n < 6 + NAME_COLS; p++) line[n++] = (char)((unsigned char)*p < 0x7f ? *p : '?');
  while (n < 6 + NAME_COLS + 2) line[n++] = ' ';
  line[n] = 0;
  if (e->kind == DL_FILE) ui_put_size(line + n, e->size);
  if (idx == selected) ui_line_rev(row, line, 0);
  else ui_line(row, line, 0);
}

static void draw_info(void)
{
  char num[8];
  ui_put_ulong(num, dl_count);
  strcpy(line, num);
  strcat(line, dl_count == 1 ? " entry" : " entries");
  if (dl_overflow) strcat(line, " (cut: too many)");
  if (dl_toolong) strcat(line, " (some names too long)");
  strcat(line, "   ");
  xfer_drive_text(line + strlen(line));
  ui_line(ROW_INFO, line, 0);
}

static void draw_page(void)
{
  unsigned char r, idx;
  draw_title();
  for (r = 0; r < ROWS_PER_PAGE; r++) {
    idx = (unsigned char)(top + r);
    if (idx < dl_count) draw_entry(idx, (unsigned char)(ROW_FIRST + r));
    else ui_line((unsigned char)(ROW_FIRST + r), 0, 0);
  }
  draw_info();
  draw_keys();
}

static void move_to(unsigned char idx)
{
  unsigned char old = selected;
  if (idx >= dl_count) return;
  selected = idx;
  if (idx < top || idx >= top + ROWS_PER_PAGE) {
    top = (unsigned char)(idx - idx % ROWS_PER_PAGE);
    draw_page();
    return;
  }
  draw_entry(old, (unsigned char)(ROW_FIRST + old - top));
  draw_entry(selected, (unsigned char)(ROW_FIRST + selected - top));
}

/* ---- paths -------------------------------------------------------------- */

/* `name` under the current directory, into `out` (SFTP_PATH_MAX + 1).
 * 0, with a word, when it does not fit. */
static unsigned char path_join(char *out, const char *name)
{
  unsigned int base = strlen(path), n = strlen(name);
  if (base + 1 + n > SFTP_PATH_MAX) { ui_status("the path is too long", 0); return 0; }
  memcpy(out, path, base);
  if (base && out[base - 1] != '/') out[base++] = '/';
  strcpy(out + base, name);
  return 1;
}

static void path_up(void)
{
  char *last = strrchr(path, '/');
  if (!last || last == path) { path[0] = '/'; path[1] = 0; return; }
  *last = 0;
}

/* ---- listing ------------------------------------------------------------ */

static unsigned char fetch_listing(void)
{
  signed char r;
  ui_status("listing...", 0);
  dl_clear();
  sftp_on_name = dl_add;
  if (!sftp_opendir(path)) { ui_spin_clear(); ui_status("open: ", sftp_error_text()); return 0; }
  while ((r = sftp_readdir()) > 0) ;
  sftp_close();
  ui_spin_clear();
  if (r < 0) { ui_status("list: ", sftp_error_text()); return 0; }
  selected = top = 0;
  draw_page();
  ui_status(0, 0);
  return 1;
}

static void enter_directory(const char *name)
{
  unsigned int was = strlen(path);
  if (!path_join(joined, name)) return;
  strcpy(path, joined);
  if (!fetch_listing()) { path[was] = 0; fetch_listing(); }
}

/* ---- session ------------------------------------------------------------ */

static unsigned char open_session(void)
{
  if (!ssh_connect(ip, port, host)) { ui_status("connect: ", ssh_error_text()); return 0; }
  if (!(auth_s[0] == 'i' ? ssh_login_key(user, ident_seed, ident_pk) : ssh_login(user, pass))) {
    ui_status("login: ", ssh_error_text());
    ssh_disconnect();
    return 0;
  }
  ui_status("opening the file service...", 0);
  if (!sftp_start()) { ui_status("sftp: ", sftp_error_text()); ssh_disconnect(); return 0; }
  ui_status("finding the login directory...", 0);
  if (!sftp_realpath(".", path)) { ui_status("realpath: ", sftp_error_text()); ssh_disconnect(); return 0; }
  return 1;
}

/* ---- the host screen ---------------------------------------------------- */

static unsigned char session_setup(void)
{
  unsigned char i;
  const char *err;
  unsigned int v = 0;
  ui_clear_rows(1, UI_ROW_LAST_BODY);
  ui_line(0, "MEGA65 SFTP Client", 0);
  ui_line(UI_ROW_KEYS, "RETURN accepts a line   RUN/STOP goes back   F1 identity", 0);
  ui_status("curve25519, ed25519, chacha20-poly1305; hosts remembered in KNOWNHOSTS", 0);
  for (;;) {
    i = ui_read_line(3, "Host: ", host, sizeof host - 1, 0);
    if (!i) return 2;
    if (i == 2) { identity_screen(); ui_line(0, "MEGA65 SFTP Client", 0); continue; }
    if (host[0]) break;
  }
  if (!ui_read_line(4, "Port: ", port_s, sizeof port_s - 1, 0)) return 0;
  for (;;) {
    auth_s[0] = ident_auth; auth_s[1] = 0;
    if (!ui_read_line(5, "Auth (p = password, i = identity): ", auth_s, 1, 0)) return 0;
    if (auth_s[0] == 'P') auth_s[0] = 'p';
    if (auth_s[0] == 'I') auth_s[0] = 'i';
    if (auth_s[0] == 'p' || auth_s[0] == 'i') break;
  }
  if (auth_s[0] != ident_auth) ident_set_auth(auth_s[0]);
  if (!ui_read_line(6, "User: ", user, sizeof user - 1, 0) || !user[0]) return 0;
  for (i = 0; port_s[i] >= '0' && port_s[i] <= '9'; i++) v = v * 10 + (unsigned int)(port_s[i] - '0');
  port = v ? v : 22;
  pass[0] = 0;
  if (auth_s[0] == 'p') {
    if (!ui_read_line(7, "Password: ", pass, sizeof pass - 1, 1)) return 0;
  } else if (!ident_present) {
    if (!identity_screen()) return 0;
    ui_line(0, "MEGA65 SFTP Client", 0);
  }
  rnd_stir((uint8_t)PEEK(0xd012));
  ui_status("resolving host...", 0);
  if (!net_resolve(host, ip, &err)) { ui_status("resolve: ", err); return 0; }
  ui_clear_rows(1, UI_ROW_LAST_BODY);
  if (!open_session()) return 0;
  memset(pass, 0, sizeof pass);
  return fetch_listing() ? 1 : 0;
}

/* ---- the browser -------------------------------------------------------- */

static void end_session(void)
{
  ssh_disconnect();
}

static unsigned char browse(void)
{
  unsigned char k, mods;
  struct dl_entry *e;

  for (;;) {
    k = ui_key_mods(&mods);
    if (!k) { meganet_poll(); if (!ch_poll()) { ui_status("connection: ", ssh_error_text()); end_session(); return 1; } continue; }
    rnd_stir(k);
    if (k >= 0xc1 && k <= 0xda && (mods & MOD_MEGA)) k = (unsigned char)(k & 0x7f);
    switch (k) {
    case KEY_DOWN: move_to((unsigned char)(selected + 1)); break;
    case KEY_UP: if (selected) move_to((unsigned char)(selected - 1)); break;
    case KEY_RIGHT:
      if (top + ROWS_PER_PAGE < dl_count) move_to((unsigned char)(top + ROWS_PER_PAGE));
      break;
    case KEY_LEFT:
      if (top) move_to((unsigned char)(top - ROWS_PER_PAGE));
      else move_to(0);
      break;
    case KEY_HOME: move_to(0); break;
    case KEY_RETURN:
      /* a link is tried as a directory first, as the FTP client tries
       * CWD; one that refuses is fetched as a file, its size unknown */
      if (!dl_count) break;
      e = dl_get(selected);
      if (e->kind == DL_DIR || e->kind == DL_LINK) {
        unsigned int was = strlen(path);
        enter_directory(e->name);
        if (path[was] || e->kind == DL_DIR) break;
        e = dl_get(selected);
      }
      if (path_join(joined, e->name)) { xfer_get(e, joined); draw_info(); draw_keys(); }
      break;
    case 'u': case 'U': case KEY_DEL: path_up(); fetch_listing(); break;
    case 'r': case 'R': fetch_listing(); break;
    case 'p': case 'P':
      if (xfer_put(path)) fetch_listing();
      else draw_page();                             /* the picker used the listing rows */
      break;
    case 'd': case 'D': xfer_choose_drive(); draw_info(); break;
    case 'f': case 'F':                             /* MEGA held, as every client binds the colors (2026-09-29) */
      if (!(mods & MOD_MEGA)) break;
      m65_screen_cycle_text_colour();
      draw_page();
      break;
    case 'b': case 'B':
      if (!(mods & MOD_MEGA)) break;
      m65_screen_cycle_background();
      break;
    case KEY_HELP: case 'h': case 'H':
      end_session();
      return 1;
    case KEY_STOP:                                  /* up a directory; at the root, the host screen */
      if (path[0] == '/' && path[1] == 0) { end_session(); return 1; }
      path_up(); fetch_listing();
      break;
    default: break;
    }
  }
}

int main(void)
{
  const char *err;
  unsigned char k;

  mega65_io_enable();
  m65_own_vectors();
  m65_screen_init();
  scr_rows = m65_screen_rows();
  ui_line(0, "MEGA65 SFTP Client - version " SFTPC_VERSION, 0);
  ui_status("starting the network...", 0);
  if (!net_up(&err)) { ui_status("network: ", err); for (;;) ; }
  ui_status("loading the crypto bank...", 0);
  if (!ck_boot(&err)) { ui_status("crypto: ", err); for (;;) ; }
  ui_status("gathering randomness...", 0);
  rnd_init();
  hosts_load(boot_drive);
  ident_load(boot_drive);
  strcpy(port_s, "22");
  ssh_status = status_cb;
  ssh_ask_hostkey = ask_hostkey;
  sftp_idle = spin_cb;

  for (;;) {
    k = session_setup();
    if (k == 2) break;
    if (!k) {
      ssh_disconnect();
      ui_line(UI_ROW_KEYS, "any key to try again   RUN/STOP to quit", 0);
      if (ui_wait_key() == KEY_STOP) break;
      continue;
    }
    if (!browse()) break;
  }
  m65_exit_to_basic();
  return 0;
}
