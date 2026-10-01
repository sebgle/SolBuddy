/*
 * Bring-up test: one AS7343 measurement every 2 s.
 * Serial is only used to print results; it is not part of the firmware design.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "drivers/AS7343/AS7343_REGS.h"

#define POLL_INTERVAL_MS    5
#define MEASURE_TIMEOUT_MS  1000

/* Slot order in 18-channel mode, matching AS7343_SLOT_* in AS7343_REGS.h. */
static const char *const SLOT_NAMES[AS7343_DATA_SLOT_COUNT] = {
    "FZ",  "FY", "FXL", "NIR", "CLEAR", "FD",
    "F2",  "F3", "F4",  "F6",  "CLEAR", "FD",
    "F1",  "F7", "F8",  "F5",  "CLEAR", "FD",
};

static const char *as7343_result_name(as7343_result_t r)
{
    switch (r) {
        case AS7343_OK:              return "OK";
        case AS7343_ERR_NOT_PRESENT: return "ERR_NOT_PRESENT";
        case AS7343_ERR_BUS:         return "ERR_BUS";
        case AS7343_ERR_WRONG_ID:    return "ERR_WRONG_ID";
        case AS7343_ERR_NOT_READY:   return "ERR_NOT_READY";
    }
    return "UNKNOWN";
}

/* Test-only raw STATUS2 read: -1 on bus error, else the whole byte. */
static int status2_raw(void)
{
    uint8_t tx[1] = { AS7343_REG_STATUS2 };
    uint8_t value = 0;
    if (i2c_write_read(AS7343_I2C_ADDR, tx, sizeof tx, &value, 1) != I2C_OK) return -1;
    return value;
}

/* Test-only peek at AVALID, to learn when the chip sets and clears it. */
static int avalid_raw(void)
{
    int s = status2_raw();
    if (s < 0) return -1;
    return (s & AS7343_STATUS2_AVALID_MASK) ? 1 : 0;
}

static void measure_once(void)
{
    as7343_result_t r = as7343_start_measurement();
    uint32_t t_start = millis();
    if (r != AS7343_OK) {
        Serial.printf("start: %s\n", as7343_result_name(r));
        return;
    }
    Serial.printf("AVALID right after start: %d\n", avalid_raw());

    /* Calling read() before data is ready must be refused. */
    as7343_reading_t reading;
    Serial.printf("early read -> %s (expect ERR_NOT_READY)\n",
                  as7343_result_name(as7343_read(&reading)));

    bool ready = false;
    while (!ready && (millis() - t_start) < MEASURE_TIMEOUT_MS) {
        delay(POLL_INTERVAL_MS);
        r = as7343_data_ready(&ready);
        if (r != AS7343_OK) break;
    }
    uint32_t elapsed = millis() - t_start;

    if (!ready) {
        Serial.printf("no data after %lu ms (%s)\n", elapsed, as7343_result_name(r));
        as7343_sleep();
        return;
    }

    r = as7343_read(&reading);
    Serial.printf("ready after %lu ms, read -> %s, AVALID after read: %d\n",
                  elapsed, as7343_result_name(r), avalid_raw());
    as7343_sleep();
    if (r != AS7343_OK) return;

    Serial.printf("gain code %u, saturated %s\n",
                  reading.gain, reading.saturated ? "YES" : "no");
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        Serial.printf("  %2lu %-5s %5u\n", i, SLOT_NAMES[i], reading.counts[i]);
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    i2c_init();
    Serial.printf("as7343_init -> %s\n", as7343_result_name(as7343_init()));
}

void loop()
{
    static uint32_t run = 0;
    Serial.printf("--- measurement %lu ---\n", ++run);
    measure_once();
    delay(2000);
}
