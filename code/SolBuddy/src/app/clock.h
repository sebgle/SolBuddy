#ifndef SOLBUDDY_APP_CLOCK_H
#define SOLBUDDY_APP_CLOCK_H

/* Device clock: services/timekeeper fed by the FreeRTOS tick counter
 * (1024 Hz, from the 32.768 kHz crystal, keeps counting in sleep). */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void     clock_init(void);
uint32_t clock_uptime_s(void);
void     clock_set_utc(uint32_t utc_s);
uint32_t clock_timestamp(bool *is_utc);   /* UTC if set, else uptime */

#ifdef __cplusplus
}
#endif

#endif
