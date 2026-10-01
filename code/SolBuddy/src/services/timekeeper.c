#include "services/timekeeper.h"

/* Fold the raw 32-bit counter into the 64-bit total. Unsigned subtraction
 * gives the right difference even if the counter wrapped since last time. */
static void advance(timekeeper_t *tk, uint32_t ticks_now)
{
    tk->total_ticks += (uint32_t)(ticks_now - tk->last_ticks);
    tk->last_ticks   = ticks_now;
}

void timekeeper_init(timekeeper_t *tk, uint32_t tick_hz, uint32_t ticks_now)
{
    tk->tick_hz      = tick_hz;
    tk->last_ticks   = ticks_now;
    tk->total_ticks  = 0;
    tk->utc_valid    = false;
    tk->utc_at_set   = 0;
    tk->ticks_at_set = 0;
}

uint32_t timekeeper_uptime_s(timekeeper_t *tk, uint32_t ticks_now)
{
    advance(tk, ticks_now);
    return (uint32_t)(tk->total_ticks / tk->tick_hz);
}

void timekeeper_set_utc(timekeeper_t *tk, uint32_t utc_s, uint32_t ticks_now)
{
    advance(tk, ticks_now);
    tk->utc_valid    = true;
    tk->utc_at_set   = utc_s;
    tk->ticks_at_set = tk->total_ticks;
}

uint32_t timekeeper_timestamp(timekeeper_t *tk, uint32_t ticks_now, bool *is_utc)
{
    advance(tk, ticks_now);
    *is_utc = tk->utc_valid;
    if (!tk->utc_valid) return (uint32_t)(tk->total_ticks / tk->tick_hz);

    /* Count from the exact tick UTC was set: no rounding error accumulates. */
    return tk->utc_at_set + (uint32_t)((tk->total_ticks - tk->ticks_at_set) / tk->tick_hz);
}
