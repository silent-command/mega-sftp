/* SFTP version 3 over the session channel (sftp.h). Requests are built
 * in `rq` and sent with ch_send; replies arrive through ch_on_data and
 * are taken apart by a small state machine that reads one field at a
 * time: a number is collected into `acc`, a name into its own buffer,
 * file data is handed on as it comes, and whatever is not wanted is
 * skipped by count. Every field is bounded by the packet's own length,
 * so a reply this client does not understand is stepped over whole and
 * the next one starts cleanly. */
#include <string.h>
#include "mega65/memory.h"
#include "transport.h"
#include "channel.h"
#include "sftp.h"

#define FXP_INIT 1
#define FXP_VERSION 2
#define FXP_OPEN 3
#define FXP_CLOSE 4
#define FXP_READ 5
#define FXP_WRITE 6
#define FXP_OPENDIR 11
#define FXP_READDIR 12
#define FXP_REMOVE 13
#define FXP_MKDIR 14
#define FXP_RMDIR 15
#define FXP_REALPATH 16
#define FXP_STAT 17
#define FXP_RENAME 18
#define FXP_STATUS 101
#define FXP_HANDLE 102
#define FXP_DATA 103
#define FXP_NAME 104
#define FXP_ATTRS 105

#define FXF_READ 0x01
#define FXF_WRITE 0x02
#define FXF_CREAT 0x08
#define FXF_TRUNC 0x10

#define ATTR_SIZE 0x01
#define ATTR_UIDGID 0x02
#define ATTR_PERMISSIONS 0x04
#define ATTR_ACMODTIME 0x08
#define ATTR_EXTENDED 0x80

#define HANDLE_MAX 64               /* OpenSSH's are four bytes; the draft allows 256 */
#define TIMEOUT_FRAMES 1500         /* 30 s without a byte */

unsigned char sftp_error;
uint32_t sftp_code;
char sftp_message[64];
void (*sftp_on_name)(const char *name, uint8_t kind, uint32_t size);
void (*sftp_on_data)(const uint8_t *p, uint16_t n);
void (*sftp_idle)(void);

/* ---- requests ----------------------------------------------------------- */

/* The largest is a rename: two paths and 21 bytes of framing. On the
 * machine it is low RAM $1D40-$1F5D, above the name buffer (lowmap.h). */
#ifdef __mos__
#define rq ((uint8_t *)0x1D40)
#else
static uint8_t rq[2 * SFTP_PATH_MAX + 32];
#endif
static uint16_t rq_n;
static uint32_t next_id;

static void rq_u32(uint32_t v)
{
  rq[rq_n++] = (uint8_t)(v >> 24); rq[rq_n++] = (uint8_t)(v >> 16);
  rq[rq_n++] = (uint8_t)(v >> 8); rq[rq_n++] = (uint8_t)v;
}

static void rq_bytes(const uint8_t *p, uint16_t n)
{
  rq_u32(n);
  memcpy(rq + rq_n, p, n);
  rq_n = (uint16_t)(rq_n + n);
}

/* A path as a string; 0 if it is too long to send. */
static unsigned char rq_path(const char *s)
{
  uint16_t n = (uint16_t)strlen(s);
  if (n > SFTP_PATH_MAX) { sftp_error = SFTP_E_TOOLONG; return 0; }
  rq_bytes((const uint8_t *)s, n);
  return 1;
}

static uint8_t handle[HANDLE_MAX];
static uint8_t handle_n;

static void rq_begin(uint8_t type)
{
  rq_n = 4;
  rq[rq_n++] = type;
  rq_u32(++next_id);
}

/* ---- the reply parser --------------------------------------------------- */

enum {
  S_LEN, S_TYPE, S_ID, S_VERSION, S_REST,
  S_CODE, S_MSG_LEN, S_MSG,
  S_HANDLE_LEN, S_HANDLE,
  S_DATA_LEN, S_DATA,
  S_COUNT, S_NAME_LEN, S_NAME, S_LONG_LEN, S_LONG,
  S_FLAGS, S_SIZE, S_UIDGID, S_PERM, S_TIMES, S_XCOUNT, S_XLEN, S_XVAL
};
#define M_COLLECT 0
#define M_SKIP 1
#define M_DATA 2

static uint8_t st, mode, in_packet;
static uint32_t want, packet_left;
static uint8_t acc[8];
static uint8_t *col_p;              /* where a collected field goes, and how much of it is kept */
static uint16_t col_cap, col_n;
static uint32_t fed;                /* bytes taken in: the wait's sign of life */

static uint8_t r_type, r_done, r_bad;
static uint32_t r_id;
static uint32_t r_count;            /* names left in a NAME reply */
static uint32_t a_flags, a_xleft;
static uint8_t a_xhalf;             /* 0 the extension's name, 1 its value */
static uint32_t a_size, a_perm;
static uint8_t a_has_perm;
static uint16_t data_got;
/* On the machine this page is shared: the parser's name lands here on
 * NAME replies, and between them the client builds one joined path in
 * it (sftpc.c, xfer.c). The two uses never overlap: a path is copied
 * into the request before it is sent, and no NAME reply is in flight
 * while it is built (lowmap.h). */
#ifdef __mos__
#define e_name ((char *)0x1C40)
#else
static char e_name[SFTP_PATH_MAX + 1];
#endif
static uint8_t e_cut;
static uint8_t stat_kind;
static uint32_t stat_size;
static uint32_t peer_version;

static uint32_t be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* The next field: collected into `to` (acc when null), skipped, or passed on as data. */
static void field(uint8_t state, uint8_t m, uint32_t n, uint8_t *to, uint16_t cap)
{
  st = state; mode = m; want = n;
  col_p = to ? to : acc; col_cap = to ? cap : sizeof acc; col_n = 0;
}

static void rest(void) { field(S_REST, M_SKIP, packet_left, 0, 0); }

static uint8_t kind_of(void)
{
  if (!a_has_perm) return SFTP_KIND_FILE;
  switch (a_perm & 0xF000UL) {
  case 0x4000UL: return SFTP_KIND_DIR;
  case 0xA000UL: return SFTP_KIND_LINK;
  case 0x8000UL: return SFTP_KIND_FILE;
  default: return SFTP_KIND_OTHER;
  }
}

static void next_name(void)
{
  if (r_count) { r_count--; field(S_NAME_LEN, M_COLLECT, 4, 0, 0); }
  else rest();
}

/* Past the flags, the fields they announce, in the draft's order. */
static void next_attr(uint8_t after)
{
  if (after < S_SIZE && (a_flags & ATTR_SIZE)) { field(S_SIZE, M_COLLECT, 8, 0, 0); return; }
  if (after < S_UIDGID && (a_flags & ATTR_UIDGID)) { field(S_UIDGID, M_SKIP, 8, 0, 0); return; }
  if (after < S_PERM && (a_flags & ATTR_PERMISSIONS)) { field(S_PERM, M_COLLECT, 4, 0, 0); return; }
  if (after < S_TIMES && (a_flags & ATTR_ACMODTIME)) { field(S_TIMES, M_SKIP, 8, 0, 0); return; }
  if (after < S_XCOUNT && ((a_flags >> 24) & ATTR_EXTENDED)) { field(S_XCOUNT, M_COLLECT, 4, 0, 0); return; }
  /* the attributes are complete */
  if (r_type == FXP_NAME) {
    const char *n = e_name;
    if (!(n[0] == '.' && (!n[1] || (n[1] == '.' && !n[2]))) && sftp_on_name)
      sftp_on_name(n, kind_of(), a_size);
    next_name();
  } else {
    stat_kind = kind_of(); stat_size = a_size;
    rest();
  }
}

static void attrs_start(void)
{
  a_size = 0; a_perm = 0; a_has_perm = 0;
  field(S_FLAGS, M_COLLECT, 4, 0, 0);
}

/* A field is complete: act on it and choose the next. */
static void advance(void)
{
  uint32_t v = be32(acc);
  switch (st) {
  case S_LEN:
    packet_left = v; in_packet = 1;
    field(S_TYPE, M_COLLECT, 1, 0, 0);
    break;
  case S_TYPE:
    r_type = acc[0];
    if (r_type == FXP_VERSION) field(S_VERSION, M_COLLECT, 4, 0, 0);
    else field(S_ID, M_COLLECT, 4, 0, 0);
    break;
  case S_VERSION: peer_version = v; rest(); break;
  case S_ID:
    r_id = v;
    switch (r_type) {
    case FXP_STATUS: field(S_CODE, M_COLLECT, 4, 0, 0); break;
    case FXP_HANDLE: field(S_HANDLE_LEN, M_COLLECT, 4, 0, 0); break;
    case FXP_DATA: field(S_DATA_LEN, M_COLLECT, 4, 0, 0); break;
    case FXP_NAME: field(S_COUNT, M_COLLECT, 4, 0, 0); break;
    case FXP_ATTRS: attrs_start(); break;
    default: rest(); break;
    }
    break;
  case S_CODE: sftp_code = v; field(S_MSG_LEN, M_COLLECT, 4, 0, 0); break;
  case S_MSG_LEN: field(S_MSG, M_COLLECT, v, (uint8_t *)sftp_message, sizeof sftp_message - 1); break;
  case S_MSG: sftp_message[col_n] = 0; rest(); break;
  case S_HANDLE_LEN:
    if (v > HANDLE_MAX) { r_bad = 1; rest(); break; }
    field(S_HANDLE, M_COLLECT, v, handle, HANDLE_MAX);
    break;
  case S_HANDLE: handle_n = (uint8_t)col_n; rest(); break;
  case S_DATA_LEN: data_got = 0; field(S_DATA, M_DATA, v, 0, 0); break;
  case S_DATA: rest(); break;
  case S_COUNT: r_count = v; next_name(); break;
  case S_NAME_LEN:
    e_cut = (uint8_t)(v > SFTP_PATH_MAX);
    field(S_NAME, M_COLLECT, v, (uint8_t *)e_name, SFTP_PATH_MAX);
    break;
  case S_NAME: e_name[col_n] = 0; field(S_LONG_LEN, M_COLLECT, 4, 0, 0); break;
  case S_LONG_LEN: field(S_LONG, M_SKIP, v, 0, 0); break;
  case S_LONG: attrs_start(); break;
  case S_FLAGS: a_flags = v; next_attr(S_FLAGS); break;
  case S_SIZE: a_size = v ? 0xFFFFFFFFUL : be32(acc + 4); next_attr(S_SIZE); break;
  case S_UIDGID: next_attr(S_UIDGID); break;
  case S_PERM: a_perm = v; a_has_perm = 1; next_attr(S_PERM); break;
  case S_TIMES: next_attr(S_TIMES); break;
  case S_XCOUNT: a_xleft = v; a_xhalf = 0;
    if (a_xleft) field(S_XLEN, M_COLLECT, 4, 0, 0); else next_attr(S_XCOUNT);
    break;
  case S_XLEN: field(S_XVAL, M_SKIP, v, 0, 0); break;
  case S_XVAL:
    if (a_xhalf) { a_xhalf = 0; if (--a_xleft == 0) { next_attr(S_XCOUNT); break; } }
    else a_xhalf = 1;
    field(S_XLEN, M_COLLECT, 4, 0, 0);
    break;
  default: rest(); break;
  }
}

static void packet_end(void)
{
  if (st != S_REST) r_bad = 1;      /* the packet ended inside a field */
  r_done = 1; in_packet = 0;
  field(S_LEN, M_COLLECT, 4, 0, 0);
}

static void feed(const uint8_t *p, uint16_t n)
{
  uint16_t take, i;
  fed += n;
  while (n) {
    take = n;
    if (take > want) take = (uint16_t)want;
    if (in_packet && take > packet_left) take = (uint16_t)packet_left;
    if (mode == M_COLLECT) {
      for (i = 0; i < take; i++) if (col_n < col_cap) col_p[col_n++] = p[i];
    } else if (mode == M_DATA && take) {
      if (sftp_on_data) sftp_on_data(p, take);
      data_got = (uint16_t)(data_got + take);
    }
    p += take; n = (uint16_t)(n - take);
    want -= take;
    if (in_packet) packet_left -= take;
    while (!want && st != S_REST) {                /* a field with nothing left: finish it, maybe more */
      advance();
      if (in_packet && !packet_left && st != S_REST && want) break;   /* the next field claims more than the packet holds */
    }
    if (in_packet && !packet_left) packet_end();
  }
}

/* ---- waiting ------------------------------------------------------------- */

static unsigned char send_request(void)
{
  uint16_t len = (uint16_t)(rq_n - 4);
  rq[0] = 0; rq[1] = 0; rq[2] = (uint8_t)(len >> 8); rq[3] = (uint8_t)len;
  r_done = 0; r_bad = 0;
  if (!ch_send(rq, rq_n)) { sftp_error = SFTP_E_SSH; return 0; }
  return 1;
}

/* Until the reply to the last request is in; 0 on a dead connection or
 * thirty seconds without a byte. A reply to anything else is dropped. */
static unsigned char wait_reply(void)
{
  uint16_t quiet = 0;
  uint32_t seen = fed;
  uint8_t last = PEEK(0xd7fa);
  for (;;) {
    if (!ch_poll()) { sftp_error = SFTP_E_SSH; return 0; }
    if (r_done) {
      if (r_type == FXP_VERSION || r_id == next_id) {
        if (r_bad) { sftp_error = SFTP_E_PROTOCOL; return 0; }
        return 1;
      }
      r_done = 0; r_bad = 0;
    }
    if (fed != seen) { seen = fed; quiet = 0; }
    if (PEEK(0xd7fa) != last) {
      last = PEEK(0xd7fa);
      if (sftp_idle) sftp_idle();
      if (++quiet > TIMEOUT_FRAMES) { sftp_error = SFTP_E_TIMEOUT; return 0; }
    }
  }
}

static unsigned char transact(void)
{
  sftp_error = SFTP_E_NONE;
  return send_request() && wait_reply();
}

/* A reply that must be a STATUS of OK. */
static unsigned char status_ok(void)
{
  if (!transact()) return 0;
  if (r_type != FXP_STATUS) { sftp_error = SFTP_E_PROTOCOL; return 0; }
  if (sftp_code != SFTP_FX_OK) { sftp_error = SFTP_E_STATUS; return 0; }
  return 1;
}

/* A reply that should be `type`; a STATUS in its place is the server's no. */
static unsigned char expect(uint8_t type)
{
  if (!transact()) return 0;
  if (r_type == type) return 1;
  sftp_error = (r_type == FXP_STATUS) ? SFTP_E_STATUS : SFTP_E_PROTOCOL;
  return 0;
}

/* ---- the operations ------------------------------------------------------ */

unsigned char sftp_start(void)
{
  ch_on_data = feed;
  field(S_LEN, M_COLLECT, 4, 0, 0);
  in_packet = 0; handle_n = 0; peer_version = 0;
  if (!ch_open_subsystem("sftp")) { sftp_error = SFTP_E_SSH; return 0; }
  rq_n = 4; rq[rq_n++] = FXP_INIT; rq_u32(3);
  if (!send_request() || !wait_reply()) return 0;
  if (r_type != FXP_VERSION || peer_version < 3) { sftp_error = SFTP_E_PROTOCOL; return 0; }
  return 1;
}

static char *name_out;
static void keep_name(const char *name, uint8_t kind, uint32_t size)
{
  (void)kind; (void)size;
  strcpy(name_out, name);
}

unsigned char sftp_realpath(const char *path, char *out)
{
  void (*was)(const char *, uint8_t, uint32_t) = sftp_on_name;
  unsigned char ok;
  rq_begin(FXP_REALPATH);
  if (!rq_path(path)) return 0;
  name_out = out; out[0] = 0;
  sftp_on_name = keep_name;
  ok = expect(FXP_NAME);
  sftp_on_name = was;
  if (ok && (e_cut || !out[0])) { sftp_error = SFTP_E_TOOLONG; ok = 0; }
  return ok;
}

static void rq_handle(void) { rq_bytes(handle, handle_n); }

static unsigned char got_handle(void)
{
  if (!expect(FXP_HANDLE)) return 0;
  if (!handle_n) { sftp_error = SFTP_E_PROTOCOL; return 0; }
  return 1;
}

unsigned char sftp_opendir(const char *path)
{
  rq_begin(FXP_OPENDIR);
  if (!rq_path(path)) return 0;
  return got_handle();
}

signed char sftp_readdir(void)
{
  rq_begin(FXP_READDIR); rq_handle();
  if (!transact()) return -1;
  if (r_type == FXP_NAME) return 1;
  if (r_type == FXP_STATUS && sftp_code == SFTP_FX_EOF) return 0;
  sftp_error = (r_type == FXP_STATUS) ? SFTP_E_STATUS : SFTP_E_PROTOCOL;
  return -1;
}

static unsigned char open_file(const char *path, uint32_t pflags)
{
  rq_begin(FXP_OPEN);
  if (!rq_path(path)) return 0;
  rq_u32(pflags);
  if (pflags & FXF_WRITE) { rq_u32(ATTR_PERMISSIONS); rq_u32(0644); }   /* rw-r--r--, as a new file on the server usually is */
  else rq_u32(0);
  return got_handle();
}

unsigned char sftp_open_read(const char *path) { return open_file(path, FXF_READ); }
unsigned char sftp_open_write(const char *path) { return open_file(path, FXF_WRITE | FXF_CREAT | FXF_TRUNC); }

static void rq_offset(uint32_t off) { rq_u32(0); rq_u32(off); }

unsigned char sftp_read(uint32_t offset, uint16_t len, uint16_t *got)
{
  rq_begin(FXP_READ); rq_handle(); rq_offset(offset); rq_u32(len);
  *got = 0;
  if (!transact()) return 0;
  if (r_type == FXP_DATA) { *got = data_got; return 1; }
  if (r_type == FXP_STATUS && sftp_code == SFTP_FX_EOF) return 1;
  sftp_error = (r_type == FXP_STATUS) ? SFTP_E_STATUS : SFTP_E_PROTOCOL;
  return 0;
}

/* The header goes from `rq` and the bytes straight from the caller's
 * buffer: the packet's length covers both, so they are one packet on
 * the stream however the channel cuts them. */
unsigned char sftp_write(uint32_t offset, const uint8_t *p, uint16_t n)
{
  uint16_t len;
  rq_begin(FXP_WRITE); rq_handle(); rq_offset(offset); rq_u32(n);
  len = (uint16_t)(rq_n - 4 + n);
  rq[0] = 0; rq[1] = 0; rq[2] = (uint8_t)(len >> 8); rq[3] = (uint8_t)len;
  r_done = 0; r_bad = 0; sftp_error = SFTP_E_NONE;
  if (!ch_send(rq, rq_n) || (n && !ch_send(p, n))) { sftp_error = SFTP_E_SSH; return 0; }
  if (!wait_reply()) return 0;
  if (r_type != FXP_STATUS) { sftp_error = SFTP_E_PROTOCOL; return 0; }
  if (sftp_code != SFTP_FX_OK) { sftp_error = SFTP_E_STATUS; return 0; }
  return 1;
}

unsigned char sftp_close(void)
{
  unsigned char ok;
  if (!handle_n) return 1;
  rq_begin(FXP_CLOSE); rq_handle();
  ok = status_ok();
  handle_n = 0;
  return ok;
}

unsigned char sftp_stat(const char *path, uint8_t *kind, uint32_t *size)
{
  rq_begin(FXP_STAT);
  if (!rq_path(path)) return 0;
  if (!expect(FXP_ATTRS)) return 0;
  *kind = stat_kind; *size = stat_size;
  return 1;
}

static unsigned char path_op(uint8_t type, const char *path)
{
  rq_begin(type);
  if (!rq_path(path)) return 0;
  if (type == FXP_MKDIR) rq_u32(0);               /* no attributes: the server's default mode */
  return status_ok();
}

unsigned char sftp_remove(const char *path) { return path_op(FXP_REMOVE, path); }
unsigned char sftp_mkdir(const char *path) { return path_op(FXP_MKDIR, path); }
unsigned char sftp_rmdir(const char *path) { return path_op(FXP_RMDIR, path); }

unsigned char sftp_rename(const char *from, const char *to)
{
  rq_begin(FXP_RENAME);
  if (!rq_path(from) || !rq_path(to)) return 0;
  return status_ok();
}

const char *sftp_error_text(void)
{
  switch (sftp_error) {
  case SFTP_E_SSH: return ssh_error_text();
  case SFTP_E_STATUS:
    if (sftp_message[0]) return sftp_message;
    switch (sftp_code) {
    case SFTP_FX_NO_SUCH_FILE: return "no such file";
    case SFTP_FX_PERMISSION_DENIED: return "permission denied";
    default: return "the server refused";
    }
  case SFTP_E_PROTOCOL: return "the server's reply made no sense";
  case SFTP_E_TIMEOUT: return "no answer from the server";
  case SFTP_E_TOOLONG: return "the path is too long";
  default: return "no error";
  }
}
