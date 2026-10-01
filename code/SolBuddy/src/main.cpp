/*
 * Bring-up test: as7343_set_exposure().
 * Measures the same scene at several exposures; counts should scale with
 * gain x integration time. Serial is only used to print results.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "drivers/AS7343/AS7343_REGS.h"

#define POLL_INTERVAL_MS    5
#define MEASURE_TIMEOUT_MS  1000

typedef struct {
    uint8_t  gain;
    uint8_t  atime;
    uint16_t astep;
    float    expected_ratio;   /* vs. the first row */
} exposure_t;

static const exposure_t EXPOSURES[] = {
    { AS7343_GAIN_256X,  29, 599, 1.0f  },   /* baseline: 256x, 50 ms */
    { AS7343_GAIN_64X,   29, 599, 0.25f },   /* 1/4 the gain */
    { AS7343_GAIN_2048X, 29, 599, 8.0f  },   /* 8x the gain */
    { AS7343_GAIN_256X,  59, 599, 2.0f  },   /* 2x the time: 100 ms */
};
#define EXPOSURE_COUNT  (sizeof EXPOSURES / sizeof EXPOSURES[0])

static const char *as7343_result_name(as7343_result_t r)
{
    switch (r) {
        case AS7343_OK:              return "OK";
        case AS7343_ERR_NOT_PRESENT: return "ERR_NOT_PRESENT";
        case AS7343_ERR_BUS:         return "ERR_BUS";
        case AS7343_ERR_WRONG_ID:    return "ERR_WRONG_ID";
        case AS7343_ERR_NOT_READY:   return "ERR_NOT_READY";
        case AS7343_ERR_ARG:         return "ERR_ARG";
    }
    return "UNKNOWN";
}

/* One complete measurement: start, poll, read, sleep. */
static as7343_result_t measure(as7343_reading_t *reading, uint32_t *elapsed_ms)
{
    as7343_result_t r = as7343_start_measurement();
    uint32_t t_start = millis();
    if (r != AS7343_OK) return r;

    bool ready = false;
    while (!ready && (millis() - t_start) < MEASURE_TIMEOUT_MS) {
        delay(POLL_INTERVAL_MS);
        r = as7343_data_ready(&ready);
        if (r != AS7343_OK) break;
    }
    *elapsed_ms = millis() - t_start;

    if (r == AS7343_OK) r = ready ? as7343_read(reading) : AS7343_ERR_NOT_READY;
    as7343_sleep();
    return r;
}

static void argument_tests(void)
{
    Serial.println("argument checks (all should be ERR_ARG):");
    Serial.printf("  gain 13          -> %s\n", as7343_result_name(as7343_set_exposure(13, 29, 599)));
    Serial.printf("  atime 0, astep 0 -> %s\n", as7343_result_name(as7343_set_exposure(AS7343_GAIN_256X, 0, 0)));
    Serial.printf("  astep 65535      -> %s\n", as7343_result_name(as7343_set_exposure(AS7343_GAIN_256X, 29, 65535)));
}

static void exposure_sweep(void)
{
    float baseline_clear = 0.0f;

    for (uint32_t i = 0; i < EXPOSURE_COUNT; i++) {
        const exposure_t *e = &EXPOSURES[i];

        as7343_result_t r = as7343_set_exposure(e->gain, e->atime, e->astep);
        if (r != AS7343_OK) {
            Serial.printf("  set_exposure -> %s\n", as7343_result_name(r));
            return;
        }

        as7343_reading_t reading;
        uint32_t elapsed = 0;
        r = measure(&reading, &elapsed);
        if (r != AS7343_OK) {
            Serial.printf("  measure -> %s\n", as7343_result_name(r));
            return;
        }

        float clear = reading.counts[AS7343_SLOT_CLEAR_1];
        if (i == 0) baseline_clear = clear;
        float ratio = (baseline_clear > 0.0f) ? clear / baseline_clear : 0.0f;

        /* Brightest slot excluding FD, to see what saturates first. */
        uint32_t max_slot = 0;
        for (uint32_t s = 0; s < AS7343_DATA_SLOT_COUNT; s++) {
            if (s == AS7343_SLOT_FD_1 || s == AS7343_SLOT_FD_2 || s == AS7343_SLOT_FD_3) continue;
            if (reading.counts[s] > reading.counts[max_slot]) max_slot = s;
        }

        Serial.printf("  gain %2u atime %3u astep %5u | %4lu ms | CLEAR %5u FD %5u max-non-FD %5u (slot %2lu) | "
                      "ratio %5.2f (expect %4.2f)%s\n",
                      reading.gain, reading.atime, reading.astep, elapsed,
                      reading.counts[AS7343_SLOT_CLEAR_1], reading.counts[AS7343_SLOT_FD_1],
                      reading.counts[max_slot], max_slot, ratio, e->expected_ratio,
                      reading.saturated ? "  SATURATED" : "");
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    i2c_init();
    Serial.printf("as7343_init -> %s\n", as7343_result_name(as7343_init()));
    argument_tests();
}

void loop()
{
    static uint32_t run = 0;
    Serial.printf("--- exposure sweep %lu ---\n", ++run);
    exposure_sweep();
    delay(3000);
}
