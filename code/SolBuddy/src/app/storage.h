#ifndef SOLBUDDY_APP_STORAGE_H
#define SOLBUDDY_APP_STORAGE_H

/*
 * The sample log on external flash: services/ringlog on top of
 * drivers/spi_nor. Keeps the flash in deep power-down between calls.
 *
 * Flash layout:
 *   sector 0 (4 KB)   reserved: calibration / device metadata (not the log)
 *   sector 1 .. end   ring log of 64-byte records
 */

#include <stdint.h>

#include "services/record.h"
#include "services/ringlog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_FLASH,      /* chip missing/unknown, or a flash operation failed */
    STORAGE_NOT_AVAILABLE,  /* seq too old (overwritten) or not written yet */
    STORAGE_LOST            /* record damaged (e.g. power lost mid-write) */
} storage_result_t;

/* Identify the chip and find where the log left off. Call once at boot. */
storage_result_t storage_init(void);

/* Store a record; the log assigns rec->seq. */
storage_result_t storage_append(record_t *rec);

/* Read one record by sequence number. */
storage_result_t storage_read(uint32_t seq, record_t *rec);

/* Boot number for this boot: newest stored record's boot_id + 1 (0 if the
 * log is empty). Valid after storage_init. */
uint16_t storage_boot_id(void);

/* Range currently stored: [oldest, next). Empty when equal. */
uint32_t storage_oldest_seq(void);
uint32_t storage_next_seq(void);

#ifdef __cplusplus
}
#endif

#endif
