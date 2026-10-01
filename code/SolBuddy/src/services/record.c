#include "services/record.h"
#include "services/crc16.h"

#include <string.h>

/* Field offsets — must match the layout table in record.h. */
#define OFF_VERSION     0u
#define OFF_FLAGS       1u
#define OFF_GAIN        2u
#define OFF_ATIME       3u
#define OFF_ASTEP       4u
#define OFF_SEQ         8u
#define OFF_TIMESTAMP   12u
#define OFF_COUNTS      16u
#define OFF_BOOT_ID     52u
#define OFF_CRC         62u

_Static_assert(OFF_COUNTS + 2u * AS7343_DATA_SLOT_COUNT <= OFF_BOOT_ID, "counts overlap boot_id");
_Static_assert(OFF_BOOT_ID + 2u <= OFF_CRC, "boot_id overlaps CRC");
_Static_assert(OFF_CRC + 2u == RECORD_SIZE, "CRC must be the last two bytes");

/* Little-endian helpers: byte order is explicit, independent of the CPU. */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void record_from_reading(record_t *rec, const as7343_reading_t *reading,
                         uint32_t seq, uint32_t timestamp, uint16_t boot_id,
                         uint8_t extra_flags)
{
    rec->seq       = seq;
    rec->timestamp = timestamp;
    rec->flags     = (uint8_t)(extra_flags | (reading->saturated ? RECORD_FLAG_SATURATED : 0u));
    rec->gain      = reading->gain;
    rec->atime     = reading->atime;
    rec->astep     = reading->astep;
    rec->boot_id   = boot_id;
    memcpy(rec->counts, reading->counts, sizeof rec->counts);
}

void record_pack(const record_t *rec, uint8_t out[RECORD_SIZE])
{
    memset(out, 0, RECORD_SIZE);   /* reserved and spare bytes = 0 */

    out[OFF_VERSION] = RECORD_VERSION;
    out[OFF_FLAGS]   = rec->flags;
    out[OFF_GAIN]    = rec->gain;
    out[OFF_ATIME]   = rec->atime;
    put_u16(&out[OFF_ASTEP], rec->astep);
    put_u32(&out[OFF_SEQ], rec->seq);
    put_u32(&out[OFF_TIMESTAMP], rec->timestamp);
    put_u16(&out[OFF_BOOT_ID], rec->boot_id);
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        put_u16(&out[OFF_COUNTS + 2u * i], rec->counts[i]);
    }

    put_u16(&out[OFF_CRC], crc16_ccitt(out, OFF_CRC));
}

record_result_t record_unpack(const uint8_t in[RECORD_SIZE], record_t *rec)
{
    bool erased = true;
    for (uint32_t i = 0; i < RECORD_SIZE; i++) {
        if (in[i] != 0xFFu) { erased = false; break; }
    }
    if (erased) return RECORD_ERASED;

    if (get_u16(&in[OFF_CRC]) != crc16_ccitt(in, OFF_CRC)) return RECORD_BAD_CRC;
    if (in[OFF_VERSION] != RECORD_VERSION)                return RECORD_BAD_VERSION;

    rec->flags     = in[OFF_FLAGS];
    rec->gain      = in[OFF_GAIN];
    rec->atime     = in[OFF_ATIME];
    rec->astep     = get_u16(&in[OFF_ASTEP]);
    rec->seq       = get_u32(&in[OFF_SEQ]);
    rec->timestamp = get_u32(&in[OFF_TIMESTAMP]);
    rec->boot_id   = get_u16(&in[OFF_BOOT_ID]);
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        rec->counts[i] = get_u16(&in[OFF_COUNTS + 2u * i]);
    }
    return RECORD_OK;
}
