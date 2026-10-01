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
    AS7343_ERR_NOT_READY,   /* read requested before a measurement completed */
    AS7343_ERR_ARG          /* illegal argument, e.g. exposure out of range */
} as7343_result_t;

/* One complete 18-channel measurement, exactly as the chip reported it. */
typedef struct {
    uint16_t counts[AS7343_DATA_SLOT_COUNT]; /* index with AS7343_SLOT_* */
    uint8_t  gain;       /* AGAIN code latched with this data (ASTATUS) */
    bool     saturated;  /* ASTATUS.ASAT_STATUS: analog or digital saturation */
    uint8_t  atime;      /* exposure in effect for this measurement */
    uint16_t astep;
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
 * Set gain (AS7343_GAIN_* code, 0-12), ATIME (0-255) and ASTEP (0-65534).
 * Integration time = (atime + 1) * (astep + 1) * 2.78 us.
 * Rejects illegal values with AS7343_ERR_ARG. Stops any running
 * measurement first; call between measurements.
 */
as7343_result_t as7343_set_exposure(uint8_t gain, uint8_t atime, uint16_t astep);

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
