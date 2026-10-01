#ifndef SOLBUDDY_DRIVERS_AS7343_H
#define SOLBUDDY_DRIVERS_AS7343_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS7343_OK = 0,
    AS7343_ERR_NOT_PRESENT, /* nothing answered at 0x39 */
    AS7343_ERR_BUS,         /* answered, but the transfer failed */
    AS7343_ERR_WRONG_ID     /* answered, but it is not an AS7343 */
} as7343_result_t;

/*
 * Verify the sensor is present and is an AS7343, write the full
 * configuration (18-channel mode, default exposure, interrupts and LED
 * off), and leave the sensor asleep (PON = 0).
 *
 * Requires i2c_init() to have been called. The sensor NACKs for ~300 us
 * after power-up (datasheet §8); do not call earlier than that.
 */
as7343_result_t as7343_init(void);

#ifdef __cplusplus
}
#endif

#endif
