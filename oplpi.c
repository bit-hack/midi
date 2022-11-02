#include <stdbool.h>
#include <stdint.h>

#include "wiringPi.h"

enum {
    D0 = 9,  // D0
    D1 = 8,  // D1
    D2 = 12, // D2
    D3 = 13, // D3
    D4 = 5,  // D4
    D5 = 6,  // D5
    D6 = 4,  // D6
    D7 = 3,  // D7
    CS = 15, // /CS   pullup    +
    A0 = 16, // A0
    WR = 7,  // /WR   pulldown  -
    IC = 2,  // /IC   pullup    +
             // /RD   pullup    +
};

static void setA0(int x)
{
    digitalWrite(A0, x);
}

static void setCS(int x)
{
    digitalWrite(CS, x);
}

static void setIC(int x)
{
    digitalWrite(IC, x);
}

static void setDataLines(uint8_t d)
{
    digitalWrite(D0, 1 & (d >> 0));
    digitalWrite(D1, 1 & (d >> 1));
    digitalWrite(D2, 1 & (d >> 2));
    digitalWrite(D3, 1 & (d >> 3));
    digitalWrite(D4, 1 & (d >> 4));
    digitalWrite(D5, 1 & (d >> 5));
    digitalWrite(D6, 1 & (d >> 6));
    digitalWrite(D7, 1 & (d >> 7));
}

void oplpi_write(uint8_t reg, uint8_t byte)
{
    setCS(1);
    // write address
    {
        setA0(0);
        setDataLines(reg);
        delayMicroseconds(10);
    }
    {
        setCS(0);
        delayMicroseconds(10);
        setCS(1);
        delayMicroseconds(10);
    }
    // write data
    {
        setA0(1);
        setDataLines(byte);
        delayMicroseconds(10);
    }
    {
        setCS(0);
        delayMicroseconds(10);
        setCS(1);
        delayMicroseconds(10);
    }
}

void oplpi_reset()
{
    // reset
    pinMode(IC, OUTPUT);
    // drive low activating reset
    setIC(0);
    // wait for reset to complete
    delay(100);
    // drive high bringing out of reset
    setIC(1);
    delay(100);

    oplpi_write(0x01, 0x20); // wave select enable
    oplpi_write(0xBD, 0xc0); // DEP AM VIB
}

void oplpi_init()
{
    wiringPiSetup(NULL);

    // databus
    pinMode(D0, OUTPUT);
    pinMode(D1, OUTPUT);
    pinMode(D2, OUTPUT);
    pinMode(D3, OUTPUT);
    pinMode(D4, OUTPUT);
    pinMode(D5, OUTPUT);
    pinMode(D6, OUTPUT);
    pinMode(D7, OUTPUT);

    // control
    pinMode(A0, OUTPUT);
    setA0(0);

    pinMode(CS, OUTPUT);
    setCS(0);

    oplpi_reset();
}
