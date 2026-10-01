#include "app/sampler.h"
#include "services/autorange.h"

#include <FreeRTOS.h>
#include <task.h>

/* Exposure time is fixed for now; only gain is auto-ranged.
 * 30 x 600 steps x 2.78 us = 50 ms per cycle, full scale 18000. */
#define SAMPLER_ATIME           29u
#define SAMPLER_ASTEP           599u
#define SAMPLER_START_GAIN      AS7343_GAIN_256X

/* First try + 4 retries. Four drops of 3 steps (12 -> 9 -> 6 -> 3 -> 0)
 * span the whole gain range, so a clipped reading always reaches 0.5x. */
#define SAMPLER_MAX_ATTEMPTS    5u

#define POLL_INTERVAL_MS        5u
#define TIMEOUT_MARGIN_MS       100u    /* autozero (~15 ms) + slack */

/* 18-channel mode = 3 integration cycles per measurement. */
static uint32_t measurement_timeout_ms(void)
{
    /* 64-bit: at the maximum exposure this product exceeds 32 bits. */
    uint64_t cycle_us = (uint64_t)(SAMPLER_ATIME + 1u) * (SAMPLER_ASTEP + 1u) * 278u / 100u;
    return (uint32_t)(3u * (cycle_us / 1000u)) + TIMEOUT_MARGIN_MS;
}

/* Start, wait (sleeping), read, and always put the sensor back to sleep. */
static as7343_result_t measure_once(as7343_reading_t *reading)
{
    as7343_result_t r = as7343_start_measurement();
    if (r != AS7343_OK) {
        as7343_sleep();
        return r;
    }

    const TickType_t start   = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(measurement_timeout_ms());
    bool ready = false;

    while (!ready) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
        r = as7343_data_ready(&ready);
        if (r != AS7343_OK) break;
        if (!ready && (xTaskGetTickCount() - start) > timeout) {
            r = AS7343_ERR_NOT_READY;
            break;
        }
    }

    if (r == AS7343_OK) r = as7343_read(reading);
    as7343_sleep();
    return r;
}

as7343_result_t sampler_init(void)
{
    return as7343_set_exposure(SAMPLER_START_GAIN, SAMPLER_ATIME, SAMPLER_ASTEP);
}

as7343_result_t sampler_take_sample(as7343_reading_t *reading, uint8_t *attempts)
{
    for (uint8_t n = 1; n <= SAMPLER_MAX_ATTEMPTS; n++) {
        *attempts = n;

        as7343_result_t r = measure_once(reading);
        if (r != AS7343_OK) return r;

        autorange_decision_t d = autorange_evaluate(reading);

        /* Apply the new gain whether we retry now or keep it for next time. */
        if (d.next_gain != reading->gain) {
            r = as7343_set_exposure(d.next_gain, SAMPLER_ATIME, SAMPLER_ASTEP);
            if (r != AS7343_OK) return r;
        }

        if (d.action == AUTORANGE_ACCEPT) return AS7343_OK;
    }

    /* Out of attempts: keep the last reading; its saturated flag says why. */
    return AS7343_OK;
}
