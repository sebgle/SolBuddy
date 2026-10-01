#include "services/ringlog.h"

#include <stdbool.h>

#define RPS RINGLOG_RECORDS_PER_SECTOR

typedef enum {
    SLOT_VALID,     /* good record whose seq belongs in this slot */
    SLOT_ERASED,    /* never written since the last erase */
    SLOT_BAD        /* damaged, or a seq that doesn't belong here */
} slot_state_t;

static uint32_t slot_of(const ringlog_t *log, uint32_t seq)
{
    return seq % log->capacity;
}

static uint32_t addr_of(const ringlog_t *log, uint32_t slot)
{
    return log->base + slot * RECORD_SIZE;
}

/* Returns false on a flash read error. */
static bool read_slot(const ringlog_t *log, uint32_t slot, record_t *rec, slot_state_t *state)
{
    uint8_t raw[RECORD_SIZE];
    if (log->flash->read(addr_of(log, slot), raw, sizeof raw) != 0) return false;

    record_result_t r = record_unpack(raw, rec);
    if (r == RECORD_ERASED) {
        *state = SLOT_ERASED;
    } else if (r == RECORD_OK && slot_of(log, rec->seq) == slot) {
        *state = SLOT_VALID;
    } else {
        *state = SLOT_BAD;
    }
    return true;
}

ringlog_result_t ringlog_mount(ringlog_t *log, const ringlog_flash_t *flash,
                               uint32_t base, uint32_t sectors)
{
    if (log == NULL || flash == NULL)       return RINGLOG_ERR_ARG;
    if (base % RINGLOG_SECTOR_SIZE != 0u)   return RINGLOG_ERR_ARG;
    if (sectors < 2u)                       return RINGLOG_ERR_ARG;

    log->flash    = flash;
    log->base     = base;
    log->capacity = sectors * RPS;
    log->next_seq = 0;

    record_t rec;
    slot_state_t state;

    /* 1. Newest sector: the one whose first record has the highest seq. */
    bool found = false;
    uint32_t newest = 0, newest_sector = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        if (!read_slot(log, s * RPS, &rec, &state)) return RINGLOG_ERR_FLASH;
        if (state == SLOT_VALID && (!found || rec.seq > newest)) {
            found = true;
            newest = rec.seq;
            newest_sector = s;
        }
    }
    if (!found) return RINGLOG_OK;   /* empty log: next_seq = 0 */

    /* 2. Newest record within that sector. Damaged slots are passed over;
     *    the first erased slot marks where writing stopped. */
    for (uint32_t i = 1; i < RPS; i++) {
        if (!read_slot(log, newest_sector * RPS + i, &rec, &state)) return RINGLOG_ERR_FLASH;
        if (state == SLOT_ERASED) break;
        if (state == SLOT_VALID) newest = rec.seq;
    }

    /* 3. Skip damaged slots after the newest record (power lost mid-write).
     *    Stop at a sector boundary: that sector is erased on the next append. */
    uint32_t next = newest + 1u;
    while (next % RPS != 0u) {
        if (!read_slot(log, slot_of(log, next), &rec, &state)) return RINGLOG_ERR_FLASH;
        if (state == SLOT_ERASED) break;
        next++;
    }

    log->next_seq = next;
    return RINGLOG_OK;
}

ringlog_result_t ringlog_append(ringlog_t *log, record_t *rec)
{
    const uint32_t seq  = log->next_seq;
    const uint32_t slot = slot_of(log, seq);
    const uint32_t addr = addr_of(log, slot);

    /* Entering a sector: erase it first (discards the oldest 64 once wrapped). */
    if (slot % RPS == 0u) {
        if (log->flash->erase_sector(addr) != 0) return RINGLOG_ERR_FLASH;
    }

    rec->seq = seq;
    uint8_t raw[RECORD_SIZE];
    record_pack(rec, raw);
    if (log->flash->program(addr, raw, sizeof raw) != 0) return RINGLOG_ERR_FLASH;

    log->next_seq = seq + 1u;
    return RINGLOG_OK;
}

uint32_t ringlog_oldest_seq(const ringlog_t *log)
{
    /* Everything is kept except the sector currently being refilled:
     * round next_seq up to a sector boundary, then go back one full ring. */
    const uint32_t end = (log->next_seq + RPS - 1u) / RPS * RPS;
    return (end > log->capacity) ? end - log->capacity : 0u;
}

ringlog_result_t ringlog_read(const ringlog_t *log, uint32_t seq, record_t *rec)
{
    if (seq >= log->next_seq || seq < ringlog_oldest_seq(log)) return RINGLOG_NOT_AVAILABLE;

    slot_state_t state;
    if (!read_slot(log, slot_of(log, seq), rec, &state)) return RINGLOG_ERR_FLASH;
    if (state != SLOT_VALID || rec->seq != seq)          return RINGLOG_LOST;
    return RINGLOG_OK;
}
