#include "services/autorange.h"

#include <stdbool.h>

#define ADC_MAX_COUNTS      65535u  /* the ADC is 16-bit whatever ATIME/ASTEP say */
#define CLIPPED_STEPS_DOWN  3u      /* clipped: true brightness unknown, drop 8x */

static uint32_t full_scale(const as7343_reading_t *r)
{
    uint32_t fs = ((uint32_t)r->atime + 1u) * ((uint32_t)r->astep + 1u);
    return (fs > ADC_MAX_COUNTS) ? ADC_MAX_COUNTS : fs;
}

static bool is_fd_slot(uint32_t slot)
{
    return slot == AS7343_SLOT_FD_1 || slot == AS7343_SLOT_FD_2 || slot == AS7343_SLOT_FD_3;
}

autorange_decision_t autorange_evaluate(const as7343_reading_t *r)
{
    const uint32_t fs     = full_scale(r);
    const uint32_t clip   = fs - 1u;        /* saturated counts read fs or fs - 1 */
    const uint32_t high   = fs * 4u / 5u;   /* 80 % */
    const uint32_t low    = fs / 5u;        /* 20 % */
    const uint32_t target = fs / 2u;        /* aim for 50 % after a change */

    /* Judge brightness on the spectral channels; FD saturates far earlier
     * (larger photodiode) and is tracked only to explain the flag. */
    uint32_t peak = 0, fd_peak = 0;
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        uint32_t c = r->counts[i];
        if (is_fd_slot(i)) {
            if (c > fd_peak) fd_peak = c;
        } else {
            if (c > peak) peak = c;
        }
    }
    const bool flag_explained_by_fd = (fd_peak >= clip);

    const uint8_t gain = r->gain;
    autorange_decision_t d = { AUTORANGE_ACCEPT, gain };

    if (peak >= clip) {
        d.next_gain = (gain > CLIPPED_STEPS_DOWN) ? (uint8_t)(gain - CLIPPED_STEPS_DOWN) : 0u;
    } else if (peak > high || (r->saturated && !flag_explained_by_fd)) {
        /* Each gain step halves the counts: halve until at or below target. */
        uint32_t steps = 0;
        for (uint32_t p = peak; p > target; p /= 2u) steps++;
        if (steps == 0u) steps = 1u;    /* unexplained flag with modest counts */
        d.next_gain = (gain > steps) ? (uint8_t)(gain - steps) : 0u;
    } else if (peak < low) {
        /* Each gain step doubles the counts: double while still at or below target. */
        uint32_t steps = 0;
        for (uint32_t p = (peak > 0u) ? peak : 1u; p * 2u <= target; p *= 2u) steps++;
        uint32_t next = gain + steps;
        d.next_gain = (next > AS7343_GAIN_2048X) ? AS7343_GAIN_2048X : (uint8_t)next;
        return d;   /* a dim reading is still valid: log it, raise gain next time */
    }

    if (d.next_gain < gain) d.action = AUTORANGE_RETRY;
    return d;
}
