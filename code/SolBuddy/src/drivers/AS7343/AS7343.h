#ifndef SOLBUDDY_DRIVERS_AS7343_H
#define SOLBUDDY_DRIVERS_AS7343_H

#include <stdint.h>
#include <stdbool.h>

// Driver will always configure in 18-channel mode
#define AS7343_NUM_CHANNELS 18

typedef enum {
  AS7343_GAIN_0_5X = 0,   ///< 0.5x gain
  AS7343_GAIN_1X = 1,     ///< 1x gain
  AS7343_GAIN_2X = 2,     ///< 2x gain
  AS7343_GAIN_4X = 3,     ///< 4x gain
  AS7343_GAIN_8X = 4,     ///< 8x gain
  AS7343_GAIN_16X = 5,    ///< 16x gain
  AS7343_GAIN_32X = 6,    ///< 32x gain
  AS7343_GAIN_64X = 7,    ///< 64x gain
  AS7343_GAIN_128X = 8,   ///< 128x gain
  AS7343_GAIN_256X = 9,   ///< 256x gain (default)
  AS7343_GAIN_512X = 10,  ///< 512x gain
  AS7343_GAIN_1024X = 11, ///< 1024x gain
  AS7343_GAIN_2048X = 12, ///< 2048x gain
} as7343_gain_t;

typedef enum {
  AS7343_CHANNEL_FZ = 0,        ///< 450nm (blue)
  AS7343_CHANNEL_FY = 1,        ///< 555nm (yellow-green)
  AS7343_CHANNEL_FXL = 2,       ///< 600nm (orange)
  AS7343_CHANNEL_NIR = 3,       ///< 855nm (near-IR)
  AS7343_CHANNEL_VIS_TL_0 = 4,  ///< Clear (top-left) cycle 1
  AS7343_CHANNEL_VIS_BR_0 = 5,  ///< Clear (both-right) cycle 1
  AS7343_CHANNEL_F2 = 6,        ///< 425nm (violet-blue)
  AS7343_CHANNEL_F3 = 7,        ///< 475nm (blue-cyan)
  AS7343_CHANNEL_F4 = 8,        ///< 515nm (green)
  AS7343_CHANNEL_F6 = 9,        ///< 640nm (red)
  AS7343_CHANNEL_VIS_TL_1 = 10, ///< Clear (top-left) cycle 2
  AS7343_CHANNEL_VIS_BR_1 = 11, ///< Clear (both-right) cycle 2
  AS7343_CHANNEL_F1 = 12,       ///< 405nm (violet)
  AS7343_CHANNEL_F7 = 13,       ///< 690nm (deep red)
  AS7343_CHANNEL_F8 = 14,       ///< 745nm (near-IR)
  AS7343_CHANNEL_F5 = 15,       ///< 550nm (green-yellow)
  AS7343_CHANNEL_VIS_TL_2 = 16, ///< Clear (top-left) cycle 3
  AS7343_CHANNEL_VIS_BR_2 = 17, ///< Clear (both-right) cycle 3
} as7343_channel_t;

typedef struct {
    uint16_t counts[AS7343_NUM_CHANNELS];
    as7343_gain_t gain;
    uint8_t atime;
    uint16_t astep;
    bool is_analog_saturated;
    bool is_digital_saturated;
} as7343_reading_t;

typedef enum {
    AS7343_OK,
    AS7343_NOT_PRESENT,
    AS7343_WRONG_ID,
    AS7343_BUS_ERROR,
    AS7343_TIMEOUT,
    AS7343_INVALID_ARGUMENT,
    AS7343_NOT_INITIALIZED
} as7343_result_t;

typedef enum {
    AS7343_READY,
    AS7343_BUSY
} as7343_measurement_status_t;


/* 18 channel mode, explicit writes (no power on defaults), leaves sensor asleep*/
as7343_result_t as7343_init(void);
as7343_result_t as7343_reset(void);
as7343_result_t as7343_health_check(void);

/* invalid values are rejected with INVALID_ARGUMENT */
as7343_result_t as7343_set_gain(as7343_gain_t gain);
as7343_gain_t as7343_get_gain(void); // cached value

/* ASTEP ~ sets the integration time per step in increments of 2.78us:
    0:      2.78us
    n:      2.78us * (n+1)
    599:    1.67 ms
    999:    2.78 ms
    17999:  50 ms
    65534:  182 ms
    65535:  DO NOT USE
*/
as7343_result_t as7343_set_astep(uint16_t astep);
uint16_t as7343_get_astep(void); // cached value

/* ATIME ~ sets the number of integration steps from 0 to 255 (integration time):
    0:      ASTEP
    n:      ASTEP * (n+1)
    255:    ASTEP * 256
*/
as7343_result_t as7343_set_atime(uint8_t atime);
uint8_t as7343_get_atime(void); // cached value

/* INTEGRATION TIME = (ATIME + 1) * (ASTEP + 1) * 2.78us 
    -> atime and astep may not be 0 at the same time
*/
uint32_t as7343_get_integration_time_us(void); // cached value
uint32_t as7343_get_full_scale_counts(void); // cached value

/* set enable bit */
as7343_result_t as7343_start_measurement(void);

/* requires all data to be ready */
as7343_result_t as7343_read_all_channels(as7343_reading_t *reading); 

as7343_result_t as7343_get_status(as7343_measurement_status_t *status); // chip read

/* blocks for one integration period, sleeps while waiting, returns TIMEOUT if sensor doesn't return ready */
as7343_result_t as7343_measure(as7343_reading_t *reading, uint32_t timeout_ms);

as7343_result_t as7343_enable_data_ready_interrupt(bool enable);

/* always acknowledge after reading */
as7343_result_t as7343_ack_data_ready_interrupt(void);

as7343_result_t as7343_sleep(void);
as7343_result_t as7343_wake(void);

#endif

