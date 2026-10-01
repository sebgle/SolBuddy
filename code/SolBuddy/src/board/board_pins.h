#ifndef SOLBUDDY_BOARD_PINS_H
#define SOLBUDDY_BOARD_PINS_H

/*
 * Pin assignments, and nothing else.
 *
 * Pins are encoded as (port * 32) + pin, which is the same encoding the
 * nRF52840's PSEL registers use: bits 0-4 select the pin, bit 5 the port.
 *
 * The board is chosen by a build flag in platformio.ini:
 *   -DBOARD_FEATHER_NRF52840   Adafruit Feather nRF52840 Express (dev)
 *   -DBOARD_SOLBUDDY_REVA      SolBuddy rev A PCB
 */
#define BOARD_PIN(port, pin)    (((port) << 5) | (pin))

/* I2C bus: AS7343 (0x39) and MAX17048 (0x36). Same on both boards. */
#define BOARD_I2C_SCL           BOARD_PIN(0, 11)
#define BOARD_I2C_SDA           BOARD_PIN(0, 12)

#if defined(BOARD_SOLBUDDY_REVA)

/* QSPI flash, MX25R6435F (U6). From revA.kicad_pcb, U3 pads 42-48.
 * SCK is not on Nordic's recommended P0.19; run QSPI slowly (decided). */
#define BOARD_QSPI_CS           BOARD_PIN(0, 19)
#define BOARD_QSPI_SCK          BOARD_PIN(0, 20)
#define BOARD_QSPI_IO0          BOARD_PIN(0, 21)
#define BOARD_QSPI_IO1          BOARD_PIN(0, 22)
#define BOARD_QSPI_IO2          BOARD_PIN(0, 23)
#define BOARD_QSPI_IO3          BOARD_PIN(0, 24)

#elif defined(BOARD_FEATHER_NRF52840)

/* QSPI flash, GD25Q16C (2 MB). From the Adafruit core's variant.cpp. */
#define BOARD_QSPI_SCK          BOARD_PIN(0, 19)
#define BOARD_QSPI_CS           BOARD_PIN(0, 20)
#define BOARD_QSPI_IO0          BOARD_PIN(0, 17)
#define BOARD_QSPI_IO1          BOARD_PIN(0, 22)
#define BOARD_QSPI_IO2          BOARD_PIN(0, 23)
#define BOARD_QSPI_IO3          BOARD_PIN(0, 21)

#else
#error "No board selected: add -DBOARD_FEATHER_NRF52840 or -DBOARD_SOLBUDDY_REVA to build_flags"
#endif

#endif
