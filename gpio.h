#pragma once

#include <stdint.h>
#include <stdbool.h>

// This define will let programs check if they are being compiled for use with
// an RTk GPIO board.
#define RTk_GPIO 1

#ifdef __cplusplus
extern "C" {
#endif

enum {
  gpio_low       =  0,
  gpio_high      =  1,
  gpio_pull_down =  0,
  gpio_pull_up   =  1,
  gpio_pull_none = -1,
  // default spi pins
  spi_sck        =  11,
  spi_mosi       =  10,
  spi_miso       =  9,
  // default i2c pins
  i2c_sck        =  3,
  i2c_sda        =  2,
};

/**
 * Open the serial port the GPIO board is attached to.
 *
 * arg port - The name of the com port the GPIO interface is attached to:
 *            on Windows for example "COM4".
 *            on Linux for example, "/dev/..."
 *
 * note: `port` can be NULL to attempt to attempt to use the first available
 *       COM port.
 *
 * returns - true if the serial port was opened successfully.
**/
bool gpio_open(const char *port);

/**
 * Close the serial connection to the GPIO board.
**/
void gpio_close(void);

/**
 * Check if the GPIO interface has been opened.
 *
 * returns - true if GPIO board is currently open and attached.
**/
bool gpio_is_open(void);

/**
 * Set a GPIO pin to act as an input.
 *
 * arg pin - the GPIO pin to set as an input.
 */
void gpio_input(int pin);

/**
 * Set a GPIO pin to act as an output.
 *
 * arg pin - the GPIO pin to set as an output.
 */
void gpio_output(int pin);

/**
 * Set the digital logic level of an output GPIO pin.
 *
 * arg pin   - the GPIO pin to set the output level of.
 * arg state - if set to 0 the pin will be voltage level low, otherwise high.
 */
void gpio_write(int pin, int state);

/**
 * Read the digital logic level on an input GPIO pin.
 *
 * arg pin - the GPIO pin to read.
 *
 * returns - 1 if a digital logic level high was read or 0 for low.
 */
int gpio_read(int pin);

/**
 * Set the pull up or pull down state of a pin.
 * 
 * arg pin   - the pin to set the pull state of.
 * arg state - set to 0 pull pin low, 1 to pull to up and -1 for no pull.
 */
void gpio_pull(int pin, int state);

/**
 * Setup pins for use as a software SPI interface.
 *
 * arg cs   - the GPIO pin that will act as the chip select pin (optional).
 * arg sck  - the GPIO pin that will act as the SPI clock.
 * arg mosi - the GPIO pin that will act as the 'master out slave in' pin.
 * arg miso - the GPIO pin that will act as the 'master in shave out' pin.
 */
void spi_sw_init(int cs=-1, int sck=spi_sck, int mosi=spi_mosi, int miso=spi_miso);

/**
 * Perform a software SPI data transfer from the GPIO board.
 *
 * arg data - the data that will be transfered to the slave.
 * arg cs   - the GPIO pin that will act as the chip select pin (optional).
 * arg sck  - the GPIO pin that will act as the SPI clock.
 * arg mosi - the GPIO pin that will act as the 'master out slave in' pin.
 * arg miso - the GPIO pin that will act as the 'master in shave out' pin.
 *
 * returns  - data received by the GPIO board during the SPI transaction.
 *
 * note, since software SPI can support any of the GPIO pins, `spi_sw_init`
 * should be called before this function to setup their state.
 */
uint8_t spi_sw_send(uint8_t data, int cs=-1, int sck=spi_sck, int mosi=spi_mosi, int miso=spi_miso);

/**
 * Perform a hardware SPI data transfer from the GPIO board.
 *
 * arg data - the data that will be transfered to the slave.
 * arg cs   - the GPIO pin that will act as the chip select pin (optional).
 *
 * returns  - data received by the GPIO board during the SPI transaction.
 *
 * pins     - sck  : gp11
 *            miso : gp9
 *            mosi : gp10
 */
uint8_t spi_hw_send(uint8_t data, int cs=-1);

/**
 * Query the RTk.GPIO board firmware version
 *
 * arg dst      - destination buffer for version string.
 * arg dst_size - size of the destination buffer.
 */
void gpio_board_version(char* dst, uint32_t dst_size);

/**
 * Delay for a number of milliseconds.
 *
 * arg ms - milliseconds to delay for.
 */
void gpio_delay(uint32_t ms);

/**
 */
void pwm_frequency(int pin, uint32_t hz);

/**
 */
void pwm_duty(int pin, uint8_t duty);

/**
 */
void i2c_sw_init(int sck = i2c_sck, int sda = i2c_sda);

/**
 */
bool i2c_sw_read(uint8_t addr, uint8_t *recv, int sck = i2c_sck, int sda = i2c_sda);

/**
 */
bool i2c_sw_write(uint8_t addr, uint8_t send, int sck = i2c_sck, int sda = i2c_sda);

#ifdef __cplusplus
}  // extern "C"
#endif
