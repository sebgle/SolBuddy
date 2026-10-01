/*
 * Bring-up test: auto-ranged sampling (app/sampler).
 * One sample every 2 s (the real interval will be 30 s).
 * Serial is only used to print results; it is not part of the firmware design.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "drivers/AS7343/AS7343_REGS.h"
#include "app/sampler.h"

/* Gain code -> multiplier text, for readable output. */
static const char *const GAIN_NAMES[] = {
    "0.5x", "1x", "2x", "4x", "8x", "16x", "32x",
    "64x", "128x", "256x", "512x", "1024x", "2048x",
};

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

static uint16_t peak_non_fd(const as7343_reading_t *r)
{
    uint16_t peak = 0;
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        if (i == AS7343_SLOT_FD_1 || i == AS7343_SLOT_FD_2 || i == AS7343_SLOT_FD_3) continue;
        if (r->counts[i] > peak) peak = r->counts[i];
    }
    return peak;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    i2c_init();
    Serial.printf("as7343_init  -> %s\n", as7343_result_name(as7343_init()));
    Serial.printf("sampler_init -> %s\n", as7343_result_name(sampler_init()));
}

void loop()
{
    static uint32_t n = 0;
    as7343_reading_t reading;
    uint8_t attempts = 0;

    uint32_t t0 = millis();
    as7343_result_t r = sampler_take_sample(&reading, &attempts);
    uint32_t took = millis() - t0;

    if (r != AS7343_OK) {
        Serial.printf("#%lu sample -> %s\n", ++n, as7343_result_name(r));
    } else {
        uint16_t peak = peak_non_fd(&reading);
        Serial.printf("#%lu gain %5s | attempts %u | %4lu ms | peak %5u (%3lu%%) | CLEAR %5u | FD %5u%s\n",
                      ++n, GAIN_NAMES[reading.gain], attempts, took,
                      peak, (uint32_t)peak * 100u / 18000u,
                      reading.counts[AS7343_SLOT_CLEAR_1], reading.counts[AS7343_SLOT_FD_1],
                      reading.saturated ? " | SAT flag" : "");
    }
    delay(2000);
}
