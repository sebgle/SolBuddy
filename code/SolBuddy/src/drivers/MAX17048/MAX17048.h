#ifndef SOLBUDDY_DRIVERS_MAX17048_H
#define SOLBUDDY_DRIVERS_MAX17048_H

/*
 * MAX17048 fuel gauge (ModelGauge), I2C 0x36, powered from the cell.
 * All registers are 16-bit, transferred MSB first (datasheet: I2C section).
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MAX17048_OK = 0,
    MAX17048_ERR_NOT_PRESENT,   /* nothing answered at 0x36 */
    MAX17048_ERR_BUS,           /* answered, but a transfer failed */
    MAX17048_ERR_WRONG_ID,      /* VERSION is not a MAX17048's */
    MAX17048_ERR_ARG            /* parameter out of range */
} max17048_result_t;

typedef struct {
    uint16_t vcell_mv;       /* cell voltage, mV */
    uint16_t soc_x256;       /* state of charge, 1/256 % (raw SOC register) */
    int16_t  crate_raw;      /* charge(+)/discharge(-) rate, 0.208 %/h per LSb */
} max17048_reading_t;

/* STATUS alert bits, as returned by max17048_read_alerts(). */
#define MAX17048_ALERT_RESET        0x01u   /* RI: powered up, not configured */
#define MAX17048_ALERT_VOLT_HIGH    0x02u   /* VH */
#define MAX17048_ALERT_VOLT_LOW     0x04u   /* VL */
#define MAX17048_ALERT_VOLT_RESET   0x08u   /* VR */
#define MAX17048_ALERT_SOC_LOW      0x10u   /* HD */
#define MAX17048_ALERT_SOC_CHANGE   0x20u   /* SC */

/*
 * Check the chip, clear its reset indicator, write CONFIG explicitly, and
 * alert (ALRT pin low) when VCELL < low_alert_mv. 0..5100 mV, 20 mV steps.
 */
max17048_result_t max17048_init(uint16_t low_alert_mv);

max17048_result_t max17048_read(max17048_reading_t *reading);

/* Which alerts fired (MAX17048_ALERT_* bits). */
max17048_result_t max17048_read_alerts(uint8_t *alerts);

/* Clear all alert bits and CONFIG.ALRT, releasing the ALRT pin. */
max17048_result_t max17048_clear_alerts(void);

#ifdef __cplusplus
}
#endif

#endif
