#ifndef SOLBUDDY_DRIVERS_SPI_NOR_H
#define SOLBUDDY_DRIVERS_SPI_NOR_H

/*
 * SPI NOR flash driver over hal/qspi. Supports the chips we use, all of
 * which share the standard commands:
 *   Macronix MX25R6435F (SolBuddy PCB, 8 MB)    JEDEC ID C2 28 17
 *   GigaDevice GD25Q16C (Feather Express, 2 MB) JEDEC ID C8 40 15
 *
 * Programs and erases wait for completion by polling the chip's
 * write-in-progress bit, sleeping between polls through the caller's
 * sleep function — the driver itself never touches the OS.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_NOR_PAGE_SIZE     256u
#define SPI_NOR_SECTOR_SIZE   4096u

typedef enum {
    SPI_NOR_OK = 0,
    SPI_NOR_ERR_ARG,            /* misaligned address/length, or crosses a page */
    SPI_NOR_ERR_BUS,            /* QSPI peripheral error */
    SPI_NOR_ERR_UNKNOWN_CHIP,   /* JEDEC ID not in the supported list */
    SPI_NOR_ERR_TIMEOUT         /* program/erase did not finish in time */
} spi_nor_result_t;

typedef void (*spi_nor_sleep_ms_fn)(uint32_t ms);

/* Start QSPI, wake the chip, identify it. *size_bytes receives its capacity. */
spi_nor_result_t spi_nor_init(spi_nor_sleep_ms_fn sleep_ms, uint32_t *size_bytes);

/* Stop QSPI (with the errata [122] current fix). Call spi_nor_init again to use. */
void spi_nor_uninit(void);

/* addr and len must be multiples of 4. buf may be anywhere. */
spi_nor_result_t spi_nor_read(uint32_t addr, uint8_t *buf, size_t len);

/* Program within one 256-byte page; addr and len multiples of 4.
 * NOR rule: can only change 1 bits to 0. Returns when programming is done. */
spi_nor_result_t spi_nor_program(uint32_t addr, const uint8_t *buf, size_t len);

/* Erase the 4 KB sector at addr (sets it to 0xFF). Returns when done. */
spi_nor_result_t spi_nor_erase_sector(uint32_t addr);

/* Deep power-down between uses. MX25R: standby 5 uA typ / 24 uA max (ISB1)
 * vs deep power-down 0.007 uA typ (ISB2) — standby alone would eat most of
 * the device's 7-22 uA budget, so the flash must sleep between writes. */
spi_nor_result_t spi_nor_power_down(void);
spi_nor_result_t spi_nor_wake(void);

#ifdef __cplusplus
}
#endif

#endif
