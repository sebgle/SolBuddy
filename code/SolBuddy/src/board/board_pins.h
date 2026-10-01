#ifndef SOLBUDDY_BOARD_PINS_H
#define SOLBUDDY_BOARD_PINS_H

/*
 * Pin assignments, and nothing else.
 *
 * Pins are encoded as (port * 32) + pin, which is the same encoding the
 * nRF52840's PSEL registers use: bits 0-4 select the pin, bit 5 the port.
 */
#define BOARD_PIN(port, pin)    (((port) << 5) | (pin))

/* I2C bus: AS7343 (0x39) and MAX17048 (0x36).
 * Identical on the Feather nRF52840 Express and the SolBuddy board. */
#define BOARD_I2C_SCL           BOARD_PIN(0, 11)
#define BOARD_I2C_SDA           BOARD_PIN(0, 12)

#endif
