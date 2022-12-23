#include <stdbool.h>
#include <stdint.h>

#include "serial.h"

static struct serial_t* serial;

static void sendAddr(uint8_t x)
{
    const char toSend[] = {
        '@' + (x >> 4),
        '@' + (x & 0xf),
        'c', // addr = 0 (set addr)
        'a', // wr   = 1 (write)
    };
    if (serial) {
        serial_send(serial, toSend, sizeof(toSend));
    }
}

static void sendData(uint8_t x)
{
    const char toSend[] = {
        '@' + (x >> 4),
        '@' + (x & 0xf),
        'b', // addr = 1 (set data)
        'a', // wr   = 1 (write)
    };
    if (serial) {
        serial_send(serial, toSend, sizeof(toSend));
    }
}

static void sendRst()
{
    const char toSend[] = {
        'd', // rst = 1 (reset)
    };
    if (serial) {
        serial_send(serial, toSend, sizeof(toSend));
    }
}

void oplpi_write(uint8_t reg, uint8_t byte)
{
    sendAddr(reg);
    sendData(byte);
}

void oplpi_reset()
{
    sendRst();

    // clear all registers (should be done by reset anyways)
    for (int i = 0; i < 256; ++i) {
        oplpi_write(i, 0);
    }

    oplpi_write(0x01, 0x20); // wave select enable
    oplpi_write(0xBD, 0xc0); // DEP AM VIB
}

void oplpi_init()
{
    serial = serial_open("COM14", 115200);
    oplpi_reset();
}
