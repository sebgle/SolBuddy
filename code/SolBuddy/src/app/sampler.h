#ifndef SOLBUDDY_APP_SAMPLER_H
#define SOLBUDDY_APP_SAMPLER_H

/*
 * Takes one loggable sample: measures, applies the auto-range policy,
 * and re-measures at a lower gain when a reading is too bright.
 */

#include <stdint.h>

#include "drivers/AS7343/AS7343.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set the starting exposure. Call once after as7343_init(). */
as7343_result_t sampler_init(void);

/*
 * Blocks (sleeping) for one or more measurements, ~160 ms each.
 * On AS7343_OK, *reading is the sample to log and *attempts says how many
 * measurements it took (1 = no retry). Gain for the next call is already set.
 */
as7343_result_t sampler_take_sample(as7343_reading_t *reading, uint8_t *attempts);

#ifdef __cplusplus
}
#endif

#endif
