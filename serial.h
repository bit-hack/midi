#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct serial_t;

struct serial_t* serial_open(const char *port, uint32_t baud_rate);

void serial_close(struct serial_t* serial);

uint32_t serial_send(struct serial_t* serial, const void* src, size_t nbytes);

uint32_t serial_read(struct serial_t* serial, void* dst, size_t nbytes);

#ifdef __cplusplus
}
#endif
