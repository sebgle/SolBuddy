#ifndef SOLBUDDY_SERVICES_TIMEKEEPER_H
#define SOLBUDDY_SERVICES_TIMEKEEPER_H

/*
 * Seconds since boot and UTC from a free-running 32-bit tick counter.
 *
 * The counter wraps (FreeRTOS at 1024 Hz: every ~48.5 days); the
 * timekeeper extends it to 64 bits. Call any function that takes
 * ticks_now at least once per wrap period — sampling every 30 s does.
 *
 * Before the phone sets UTC, timestamps are seconds since boot and must be
 * flagged TIME_UNSET; records carry boot_id so the app can re-base them.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tick_hz;
    uint32_t last_ticks;     /* raw counter at the previous call */
    uint64_t total_ticks;    /* ticks since init, never wraps */
    bool     utc_valid;
    uint32_t utc_at_set;     /* UTC seconds when set_utc was called */
    uint64_t ticks_at_set;   /* total_ticks at that moment */
} timekeeper_t;

void timekeeper_init(timekeeper_t *tk, uint32_t tick_hz, uint32_t ticks_now);

/* Seconds since init. */
uint32_t timekeeper_uptime_s(timekeeper_t *tk, uint32_t ticks_now);

/* The phone wrote UTC (seconds since 1970). Replaces any earlier value. */
void timekeeper_set_utc(timekeeper_t *tk, uint32_t utc_s, uint32_t ticks_now);

/* Timestamp for a record: UTC if set (*is_utc = true), else seconds since
 * boot (*is_utc = false: set RECORD_FLAG_TIME_UNSET). */
uint32_t timekeeper_timestamp(timekeeper_t *tk, uint32_t ticks_now, bool *is_utc);

#ifdef __cplusplus
}
#endif

#endif
