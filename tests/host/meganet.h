/* The host's stand-in for mega-net's ABI: TCP sockets by index over BSD
 * sockets, non-blocking, with the states and flags the clients read. */
#ifndef HOST_MEGANET_H
#define HOST_MEGANET_H
#include <stdint.h>
#define MEGANET_TCP_CLOSED 0
#define MEGANET_TCP_CONNECTING 1
#define MEGANET_TCP_ESTABLISHED 2
#define MEGANET_TCP_F_EOF 0x01
#define MEGANET_TCP_F_RESET 0x02
#define MEGANET_TCP_F_TIMEOUT 0x04
#define MEGANET_TCP_F_REFUSED 0x08
void meganet_poll(void);
uint8_t meganet_tcp_connect_s(uint8_t s, const uint8_t *ip4, uint16_t port);
uint8_t meganet_tcp_state_s(uint8_t s, uint8_t *flags, uint16_t *avail);
uint16_t meganet_tcp_send_s(uint8_t s, const void *data, uint16_t len);
uint16_t meganet_tcp_recv_s(uint8_t s, void *buf, uint16_t cap);
void meganet_tcp_close_s(uint8_t s);
void meganet_tcp_abort_s(uint8_t s);
#endif
