#ifndef SOLBUDDY_HAL_QSPI_H
#define SOLBUDDY_HAL_QSPI_H

/*
 * QSPI bus layer: the nRF52840 QSPI peripheral, single-line SPI mode,
 * 8 MHz, 24-bit addresses. Knows nothing about any particular flash chip
 * beyond the standard opcodes the peripheral itself sends.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QSPI_OK = 0,
    QSPI_ERR_ARG,       /* bad argument, misaligned address/buffer/length */
    QSPI_ERR_TIMEOUT    /* READY event never came */
} qspi_result_t;

/* Configure pins and peripheral, enable, activate. */
qspi_result_t qspi_init(void);

/* Deactivate and disable, including the errata [122] current fix. */
void qspi_uninit(void);

/*
 * Custom instruction: opcode, then tx_len bytes from tx, then rx_len bytes
 * clocked into rx. tx_len + rx_len <= 8. wren: send Write Enable first.
 */
qspi_result_t qspi_cinstr(uint8_t opcode, const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len, bool wren);

/* EasyDMA transfers. addr, buf and len must all be multiples of 4,
 * buf in RAM. Programs and erases return once STARTED: poll the flash
 * chip's status register for completion. */
qspi_result_t qspi_read(uint32_t addr, void *buf, size_t len);
qspi_result_t qspi_write(uint32_t addr, const void *buf, size_t len);
qspi_result_t qspi_erase_4k(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif
