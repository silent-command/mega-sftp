/* The SFTP layer on the development machine, over the unchanged SSH
 * transport (tests/host/ supplies the machine): a command-line client
 * for checking the protocol against real servers before any of it goes
 * near the MEGA65.
 *
 *   sftp_host IP PORT USER PASSWORD COMMAND [ARGS]
 *     pwd                  the login directory
 *     ls PATH              the listing, one "kind size name" a line
 *     get REMOTE LOCAL     a download, READ_LEN bytes a request
 *     put LOCAL REMOTE     an upload, WRITE_LEN bytes a request
 *     stat PATH | rm PATH | mkdir PATH | rmdir PATH | mv FROM TO
 *     pk                   with PASSWORD "seed:HEX64": the public key, and no connection
 *
 * Exits 0 when the command worked, 1 with the reason otherwise. */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "transport.h"
#include "rnd.h"
#include "sftp.h"
#include "ckit.h"

#define READ_LEN 8192
#define WRITE_LEN 1024

static FILE *out_file;
static unsigned long names;

static void on_name(const char *name, uint8_t kind, uint32_t size)
{
  static const char k[] = "-dl?";
  printf("%c %10lu %s\n", k[kind & 3], (unsigned long)size, name);
  names++;
}

static void on_data(const uint8_t *p, uint16_t n) { fwrite(p, 1, n, out_file); }

static int fail(const char *what)
{
  fprintf(stderr, "%s: %s\n", what, sftp_error_text());
  return 1;
}

static double now(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
  unsigned char ip[4];
  const char *cmd;
  static char path[SFTP_PATH_MAX + 1];
  double t0;

  if (argc < 6) { fprintf(stderr, "usage: %s IP PORT USER PASSWORD COMMAND [ARGS]\n", argv[0]); return 2; }
  if (inet_pton(AF_INET, argv[1], ip) != 1) { fprintf(stderr, "an IPv4 address, please\n"); return 2; }
  cmd = argv[5];
  rnd_init();
  if (!strcmp(cmd, "pk") && !strncmp(argv[4], "seed:", 5)) {
    static uint8_t seed[32], pk[32];
    int i; unsigned v;
    for (i = 0; i < 32; i++) { sscanf(argv[4] + 5 + 2 * i, "%2x", &v); seed[i] = (uint8_t)v; }
    ck_ed25519_keypair(pk, seed);
    for (i = 0; i < 32; i++) printf("%02x", pk[i]);
    printf("\n");
    return 0;
  }
  if (!ssh_connect(ip, (unsigned)atoi(argv[2]), argv[1])) { fprintf(stderr, "connect: %s\n", ssh_error_text()); return 1; }
  if (!strncmp(argv[4], "seed:", 5)) {
    /* PASSWORD as "seed:" and 64 hex digits: the Ed25519 identity whose
     * public key the "pk" command prints, for a server that only takes keys */
    static uint8_t seed[32], pk[32];
    int i; unsigned v;
    for (i = 0; i < 32; i++) { sscanf(argv[4] + 5 + 2 * i, "%2x", &v); seed[i] = (uint8_t)v; }
    ck_ed25519_keypair(pk, seed);
    if (!ssh_login_key(argv[3], seed, pk)) { fprintf(stderr, "login: %s\n", ssh_error_text()); return 1; }
  } else if (!ssh_login(argv[3], argv[4])) { fprintf(stderr, "login: %s\n", ssh_error_text()); return 1; }
  if (!sftp_start()) return fail("start");
  sftp_on_name = on_name;

  if (!strcmp(cmd, "pwd")) {
    if (!sftp_realpath(".", path)) return fail("realpath");
    printf("%s\n", path);
  } else if (!strcmp(cmd, "ls") && argc > 6) {
    signed char r;
    if (!sftp_opendir(argv[6])) return fail("opendir");
    while ((r = sftp_readdir()) > 0) ;
    if (r < 0) return fail("readdir");
    if (!sftp_close()) return fail("close");
    fprintf(stderr, "%lu names\n", names);
  } else if (!strcmp(cmd, "get") && argc > 7) {
    uint32_t off = 0; uint16_t got;
    out_file = fopen(argv[7], "wb");
    if (!out_file) { perror(argv[7]); return 1; }
    sftp_on_data = on_data;
    t0 = now();
    if (!sftp_open_read(argv[6])) return fail("open");
    do {
      if (!sftp_read(off, READ_LEN, &got)) return fail("read");
      off += got;
    } while (got);
    if (!sftp_close()) return fail("close");
    fclose(out_file);
    fprintf(stderr, "got %lu bytes in %.2f s\n", (unsigned long)off, now() - t0);
  } else if (!strcmp(cmd, "put") && argc > 7) {
    static uint8_t buf[WRITE_LEN];
    uint32_t off = 0; size_t n;
    FILE *in = fopen(argv[6], "rb");
    if (!in) { perror(argv[6]); return 1; }
    t0 = now();
    if (!sftp_open_write(argv[7])) return fail("open");
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
      if (!sftp_write(off, buf, (uint16_t)n)) return fail("write");
      off += (uint32_t)n;
    }
    fclose(in);
    if (!sftp_close()) return fail("close");
    fprintf(stderr, "sent %lu bytes in %.2f s\n", (unsigned long)off, now() - t0);
  } else if (!strcmp(cmd, "stat") && argc > 6) {
    uint8_t kind; uint32_t size;
    if (!sftp_stat(argv[6], &kind, &size)) return fail("stat");
    printf("kind %u size %lu\n", kind, (unsigned long)size);
  } else if (!strcmp(cmd, "rm") && argc > 6) {
    if (!sftp_remove(argv[6])) return fail("rm");
  } else if (!strcmp(cmd, "mkdir") && argc > 6) {
    if (!sftp_mkdir(argv[6])) return fail("mkdir");
  } else if (!strcmp(cmd, "rmdir") && argc > 6) {
    if (!sftp_rmdir(argv[6])) return fail("rmdir");
  } else if (!strcmp(cmd, "mv") && argc > 7) {
    if (!sftp_rename(argv[6], argv[7])) return fail("mv");
  } else {
    fprintf(stderr, "unknown command or missing arguments\n");
    return 2;
  }
  ssh_disconnect();
  return 0;
}
