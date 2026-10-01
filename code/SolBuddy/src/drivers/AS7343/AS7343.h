#ifndef SOLBUDDY_DRIVERS_AS7343_H
#define SOLBUDDY_DRIVERS_AS7343_H

#include <stdint.h>
#include <stdbool.h>

#include "drivers/AS7343/AS7343_REGS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS7343_OK = 0,
    AS7343_ERR_NOT_PRESENT, /* nothing answered at 0x39 */
    AS7343_ERR_BUS,         /* answered, but the transfer failed */
    AS7343_ERR_WRONG_ID,    /* answered, but it is not an AS7343 */
    AS7343_ERR_NOT_READY    /* read requested before a measurement completed */
} as7343_result_t;

/* One complete 18-channel measurement, exactly as the chip reported it. */
typedef struct {
    uint16_t counts[AS7343_DATA_SLOT_COUNT]; /* index with AS7343_SLOT_* */
    uint8_t  gain;       /* AGAIN code latched with this data (ASTATUS) */
    bool     saturated;  /* ASTATUS.ASAT_STATUS: analog or digital saturation */
} as7343_reading_t;

/*
 * Verify the sensor is present and is an AS7343, write the full
 * configuration (18-channel mode, default exposure, interrupts and LED
 * off), and leave the sensor asleep (PON = 0).
 *
 * Requires i2c_init() to have been called. The sensor NACKs for ~300 us
 * after power-up (datasheet §8); do not call earlier than that.
 */
as7343_result_t as7343_init(void);

/*
 * One measurement, driven by the caller:
 *
 *   as7343_start_measurement();
 *   do { wait a few ms; as7343_data_ready(&ready); } while (!ready);
 *   as7343_read(&reading);
 *   as7343_sleep();
 *
 * The driver never waits; the caller owns timing and timeouts.
 */
as7343_result_t as7343_start_measurement(void);   /* PON = 1, then SP_EN = 1 */
as7343_result_t as7343_data_ready(bool *ready);   /* STATUS2.AVALID */
as7343_result_t as7343_read(as7343_reading_t *reading);
as7343_result_t as7343_sleep(void);               /* SP_EN = 0, PON = 0 */

#ifdef __cplusplus
}
#endif

#endif
