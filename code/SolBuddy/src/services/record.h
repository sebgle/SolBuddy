#ifndef SOLBUDDY_SERVICES_RECORD_H
#define SOLBUDDY_SERVICES_RECORD_H

/*
 * The 64-byte log record: what is stored in flash and synced over BLE.
 *
 * Offset Size Field
 *   0     1   version     (RECORD_VERSION; 0xFF never used: erased flash)
 *   1     1   flags       (RECORD_FLAG_*)
 *   2     1   gain        AS7343 gain code 0-12
 *   3     1   atime
 *   4     2   astep
 *   6     2   reserved    0
 *   8     4   seq         sequence number
 *  12     4   timestamp   seconds (UTC, or since boot if TIME_UNSET)
 *  16    36   counts[18]  raw counts, AS7343_SLOT_* order
 *  52    10   spare       0
 *  62     2   crc16       CRC-16/CCITT-FALSE over bytes 0-61
 *
 * All multi-byte fields little-endian.
 */

#include <stdint.h>
#include <stdbool.h>

#include "drivers/AS7343/AS7343.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SIZE             64u
#define RECORD_VERSION          1u

#define RECORD_FLAG_SATURATED   0x01u   /* AS7343 ASTATUS saturation flag */
#define RECORD_FLAG_CHARGING    0x02u   /* USB power present: possibly not worn */
#define RECORD_FLAG_TIME_UNSET  0x04u   /* timestamp is seconds since boot, not UTC */

typedef struct {
    uint32_t seq;
    uint32_t timestamp;
    uint8_t  flags;
    uint8_t  gain;
    uint8_t  atime;
    uint16_t astep;
    uint16_t counts[AS7343_DATA_SLOT_COUNT];
} record_t;

typedef enum {
    RECORD_OK = 0,
    RECORD_ERASED,          /* all 0xFF: never written */
    RECORD_BAD_CRC,         /* corrupted or partially written */
    RECORD_BAD_VERSION      /* valid CRC, but a format this code doesn't know */
} record_result_t;

/* Fill a record from a sensor reading. extra_flags: e.g. CHARGING, TIME_UNSET. */
void record_from_reading(record_t *rec, const as7343_reading_t *reading,
                         uint32_t seq, uint32_t timestamp, uint8_t extra_flags);

void record_pack(const record_t *rec, uint8_t out[RECORD_SIZE]);

record_result_t record_unpack(const uint8_t in[RECORD_SIZE], record_t *rec);

#ifdef __cplusplus
}
#endif

#endif
