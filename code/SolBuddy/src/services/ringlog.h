#ifndef SOLBUDDY_SERVICES_RINGLOG_H
#define SOLBUDDY_SERVICES_RINGLOG_H

/*
 * Circular log of 64-byte records in NOR flash.
 *
 * Rule: record `seq` lives in slot (seq % capacity). Sync can compute any
 * record's address from its sequence number, and a record whose seq does
 * not match its slot is rejected as stale.
 *
 * A sector (64 records) is erased when its first slot is written. Once the
 * log has wrapped, that erase discards the oldest 64 records.
 */

#include <stdint.h>
#include <stddef.h>

#include "services/record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RINGLOG_SECTOR_SIZE         4096u
#define RINGLOG_RECORDS_PER_SECTOR  (RINGLOG_SECTOR_SIZE / RECORD_SIZE)   /* 64 */

/* Flash access, supplied by the caller (real driver, or a fake in tests).
 * Each returns 0 on success, nonzero on failure. program() never crosses a
 * 256-byte page: records are 64 bytes and 64-byte aligned. */
typedef struct {
    int (*read)(uint32_t addr, uint8_t *buf, size_t len);
    int (*program)(uint32_t addr, const uint8_t *buf, size_t len);
    int (*erase_sector)(uint32_t addr);
} ringlog_flash_t;

typedef struct {
    const ringlog_flash_t *flash;
    uint32_t base;          /* first byte of the log region, sector aligned */
    uint32_t capacity;      /* records: sectors * 64 */
    uint32_t next_seq;      /* sequence number the next append will get */
} ringlog_t;

typedef enum {
    RINGLOG_OK = 0,
    RINGLOG_ERR_ARG,
    RINGLOG_ERR_FLASH,
    RINGLOG_NOT_AVAILABLE,  /* seq is older than the oldest, or not written yet */
    RINGLOG_LOST            /* slot is damaged (e.g. power lost mid-write) */
} ringlog_result_t;

/* Find where the log left off. Needs at least 2 sectors. */
ringlog_result_t ringlog_mount(ringlog_t *log, const ringlog_flash_t *flash,
                               uint32_t base, uint32_t sectors);

/* Store a record. The log assigns rec->seq. */
ringlog_result_t ringlog_append(ringlog_t *log, record_t *rec);

/* Read one record by sequence number. */
ringlog_result_t ringlog_read(const ringlog_t *log, uint32_t seq, record_t *rec);

/* Same checks as ringlog_read, but returns the record's 64 bytes exactly as
 * stored (CRC included) — what the BLE Log Read characteristic sends. */
ringlog_result_t ringlog_read_raw(const ringlog_t *log, uint32_t seq, uint8_t raw[RECORD_SIZE]);

/* Oldest sequence number still stored (== next_seq when empty). */
uint32_t ringlog_oldest_seq(const ringlog_t *log);

#ifdef __cplusplus
}
#endif

#endif
