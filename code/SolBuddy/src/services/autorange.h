#ifndef SOLBUDDY_SERVICES_AUTORANGE_H
#define SOLBUDDY_SERVICES_AUTORANGE_H

/*
 * Auto-ranging policy for the AS7343: given a completed reading, decide
 * whether to keep it and which gain to use next. Pure logic, no hardware.
 */

#include <stdint.h>

#include "drivers/AS7343/AS7343.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTORANGE_ACCEPT,   /* log this reading; use next_gain for the next sample */
    AUTORANGE_RETRY     /* discard; re-measure now at next_gain */
} autorange_action_t;

typedef struct {
    autorange_action_t action;
    uint8_t            next_gain;   /* AS7343_GAIN_* code */
} autorange_decision_t;

autorange_decision_t autorange_evaluate(const as7343_reading_t *reading);

#ifdef __cplusplus
}
#endif

#endif
