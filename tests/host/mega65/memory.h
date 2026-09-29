/* The host's stand-in for mega65-libc's memory.h: PEEK of $D7FA is a
 * 50 Hz frame counter from the clock, far memory is an array covering
 * banks 0-5, and every other register reads as noise. For the host
 * harness only (tests/sftp_host.c). */
#ifndef HOST_MEGA65_MEMORY_H
#define HOST_MEGA65_MEMORY_H
#include <stdint.h>
#include <stddef.h>
uint8_t host_peek(uint32_t a);
void host_poke(uint32_t a, uint8_t v);
#define PEEK(a) host_peek((uint32_t)(a))
#define POKE(a, v) host_poke((uint32_t)(a), (uint8_t)(v))
void mega65_io_enable(void);
uint8_t lpeek(uint32_t address);
void lpoke(uint32_t address, uint8_t value);
void lcopy(uint32_t source_address, uint32_t destination_address, size_t count);
void lfill(uint32_t destination_address, uint8_t value, size_t count);
#endif
