/* The host harness's machine: the frame counter, far memory, mega-net's
 * sockets, the crypto bank's calls (on the same primitives the bank is
 * built from), and a known-hosts store that trusts on first use in
 * memory. Enough for the unchanged transport and the SFTP layer to run
 * against real servers on the development machine. */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "crypto.h"
#include "ckit.h"
#include "hosts.h"

/* ---- the machine ---------------------------------------------------- */

uint8_t host_peek(uint32_t a)
{
  if (a == 0xd7fa) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint8_t)((t.tv_sec * 50) + t.tv_nsec / 20000000);
  }
  return (uint8_t)rand();
}
void host_poke(uint32_t a, uint8_t v) { (void)a; (void)v; }
void mega65_io_enable(void) {}

static uint8_t far_mem[0x60000];
static uint8_t *far(uint32_t a, size_t n)
{
  if (a + n > sizeof far_mem) { fprintf(stderr, "far access out of range: $%lx+%zu\n", (unsigned long)a, n); exit(3); }
  return far_mem + a;
}
uint8_t lpeek(uint32_t a) { return *far(a, 1); }
void lpoke(uint32_t a, uint8_t v) { *far(a, 1) = v; }
void lcopy(uint32_t s, uint32_t d, size_t n)
{
  /* bank 0 addresses below $10000 are the program's own memory on the
   * machine; the harness has no such thing, so a copy touching them is
   * a mistake in the code under test */
  if (s < 0x10000 || d < 0x10000) { fprintf(stderr, "lcopy into bank 0 on the host: $%lx -> $%lx\n", (unsigned long)s, (unsigned long)d); exit(3); }
  memmove(far(d, n), far(s, n), n);
}
void lfill(uint32_t d, uint8_t v, size_t n) { memset(far(d, n), v, n); }

/* ---- mega-net ------------------------------------------------------- */

static struct { int fd; uint8_t state, flags; } sock[4] = { { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 } };

void meganet_poll(void) { usleep(200); }

uint8_t meganet_tcp_connect_s(uint8_t s, const uint8_t *ip4, uint16_t port)
{
  struct sockaddr_in sa;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_port = htons(port);
  memcpy(&sa.sin_addr, ip4, 4);
  sock[s].flags = 0;
  if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
    sock[s].state = MEGANET_TCP_CLOSED; sock[s].flags = MEGANET_TCP_F_REFUSED; close(fd); return 0;
  }
  fcntl(fd, F_SETFL, O_NONBLOCK);
  sock[s].fd = fd; sock[s].state = MEGANET_TCP_ESTABLISHED;
  return 1;
}

uint8_t meganet_tcp_state_s(uint8_t s, uint8_t *flags, uint16_t *avail)
{
  if (avail) {
    struct pollfd p = { sock[s].fd, POLLIN, 0 };
    *avail = (sock[s].fd >= 0 && poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) ? 1 : 0;
  }
  if (flags) *flags = sock[s].flags;
  return sock[s].state;
}

uint16_t meganet_tcp_send_s(uint8_t s, const void *data, uint16_t len)
{
  ssize_t k;
  if (sock[s].fd < 0) return 0;
  k = send(sock[s].fd, data, len, 0);
  if (k < 0) { if (errno != EAGAIN) { sock[s].flags |= MEGANET_TCP_F_RESET; sock[s].state = MEGANET_TCP_CLOSED; } return 0; }
  return (uint16_t)k;
}

uint16_t meganet_tcp_recv_s(uint8_t s, void *buf, uint16_t cap)
{
  ssize_t k;
  if (sock[s].fd < 0) return 0;
  k = recv(sock[s].fd, buf, cap, 0);
  if (k == 0) { sock[s].flags |= MEGANET_TCP_F_EOF; return 0; }
  if (k < 0) { if (errno != EAGAIN) { sock[s].flags |= MEGANET_TCP_F_RESET; sock[s].state = MEGANET_TCP_CLOSED; } return 0; }
  return (uint16_t)k;
}

void meganet_tcp_close_s(uint8_t s) { if (sock[s].fd >= 0) close(sock[s].fd); sock[s].fd = -1; sock[s].state = MEGANET_TCP_CLOSED; }
void meganet_tcp_abort_s(uint8_t s) { meganet_tcp_close_s(s); }

/* ---- the crypto bank ------------------------------------------------ */

static sha256_ctx s256[4];
static sha512_ctx s512;
static uint8_t keys[4][32];
static poly1305_ctx poly[2];

void ck_sha256_init(uint8_t slot) { sha256_init(&s256[slot]); }
void ck_sha256_update(uint8_t slot, const void *p, uint16_t n) { sha256_update(&s256[slot], p, n); }
void ck_sha256_final(uint8_t slot, uint8_t out[32]) { sha256_final(&s256[slot], out); }
void ck_sha512_init(void) { sha512_init(&s512); }
void ck_sha512_update(const void *p, uint16_t n) { sha512_update(&s512, p, n); }
void ck_sha512_final(uint8_t out[64]) { sha512_final(&s512, out); }
void ck_key_set(uint8_t slot, const uint8_t key[32]) { memcpy(keys[slot], key, 32); }
void ck_chacha_block(uint8_t k, const uint8_t nonce[8], uint32_t counter, uint8_t out[64]) { chacha20_block64(keys[k], nonce, counter, out); }
void ck_chacha_xor(uint8_t k, const uint8_t nonce[8], uint32_t counter, uint8_t *p, uint16_t n) { chacha20_xor64(keys[k], nonce, counter, p, n); }
void ck_poly_init(uint8_t slot, const uint8_t key[32]) { poly1305_init(&poly[slot], key); }
void ck_poly_update(uint8_t slot, const void *p, uint16_t n) { poly1305_update(&poly[slot], p, n); }
void ck_poly_final(uint8_t slot, uint8_t out[16]) { poly1305_final(&poly[slot], out); }
void ck_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) { x25519(out, scalar, point); }
void ck_x25519_base(uint8_t out[32], const uint8_t scalar[32]) { x25519_base(out, scalar); }
uint8_t ck_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, uint16_t n, const uint8_t pk[32]) { return (uint8_t)ed25519_verify(sig, msg, n, pk); }
uint8_t ck_ed25519_keypair(uint8_t pk[32], const uint8_t seed[32]) { ed25519_keypair(pk, seed); return 1; }
uint8_t ck_ed25519_sign(uint8_t sig[64], const uint8_t *msg, uint16_t n, const uint8_t seed[32], const uint8_t pk[32]) { ed25519_sign(sig, msg, n, seed, pk); return 1; }
uint8_t ck_equal(const uint8_t *a, const uint8_t *b, uint16_t n) { return (uint8_t)crypto_equal(a, b, n); }

/* ---- known hosts: trust on first use, in memory --------------------- */

void hosts_load(unsigned char drive) { (void)drive; }
unsigned char hosts_check(const char *host, unsigned int port, const uint8_t key[32])
{
  int i;
  fprintf(stderr, "host key of %s:%u: ", host, port);
  for (i = 0; i < 32; i++) fprintf(stderr, "%02x", key[i]);
  fprintf(stderr, " (trusted for this run)\n");
  return HOSTS_KNOWN;
}
unsigned char hosts_add(const char *host, unsigned int port, const uint8_t key[32]) { (void)host; (void)port; (void)key; return 1; }

/* ---- randomness: the host's own (rnd.c is the machine's) ------------- */

#include "rnd.h"
void rnd_init(void) {}
void rnd_stir(uint8_t v) { (void)v; }
void rnd_fill(uint8_t *out, unsigned n) { arc4random_buf(out, n); }
