/*
 * Bring-up test: end to end with timekeeping. Sensor -> sampler -> record
 * (timestamp + boot_id) -> ring log -> flash, then read back and compare.
 * After 3 records a fixed UTC is "set by the phone". Press reset: the log
 * continues its seqs and the boot number goes up by one.
 * Serial is only used to print results; it is not part of the firmware design.
 *
 * NOTE: the log uses the Feather's whole external flash except sector 0.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include <string.h>

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "app/sampler.h"
#include "app/storage.h"
#include "app/clock.h"
#include "services/record.h"

static const char *storage_result_name(storage_result_t r)
{
    switch (r) {
        case STORAGE_OK:            return "OK";
        case STORAGE_ERR_FLASH:     return "ERR_FLASH";
        case STORAGE_NOT_AVAILABLE: return "NOT_AVAILABLE";
        case STORAGE_LOST:          return "LOST";
    }
    return "UNKNOWN";
}

static bool records_equal(const record_t *a, const record_t *b)
{
    return a->seq == b->seq && a->timestamp == b->timestamp && a->flags == b->flags &&
           a->gain == b->gain && a->atime == b->atime && a->astep == b->astep &&
           a->boot_id == b->boot_id &&
           memcmp(a->counts, b->counts, sizeof a->counts) == 0;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    clock_init();
    i2c_init();
    Serial.printf("as7343_init  -> %d\n", as7343_init());
    Serial.printf("sampler_init -> %d\n", sampler_init());

    uint32_t t0 = millis();
    storage_result_t r = storage_init();
    Serial.printf("storage_init -> %s in %lu ms | stored seqs [%lu, %lu)\n",
                  storage_result_name(r), millis() - t0,
                  storage_oldest_seq(), storage_next_seq());
    Serial.printf("this boot: boot_id %u\n", storage_boot_id());
}

void loop()
{
    as7343_reading_t reading;
    uint8_t attempts = 0;
    if (sampler_take_sample(&reading, &attempts) != AS7343_OK) {
        Serial.println("sample failed");
        delay(3000);
        return;
    }

    /* Simulate the phone writing the Time characteristic after 3 records. */
    static uint32_t n = 0;
    if (++n == 4) {
        clock_set_utc(1790812800u);   /* 2026-10-01T00:00:00Z */
        Serial.println("--- phone set UTC 1790812800 (2026-10-01T00:00:00Z) ---");
    }

    bool is_utc = false;
    uint32_t ts = clock_timestamp(&is_utc);
    record_t rec;
    record_from_reading(&rec, &reading, 0, ts, storage_boot_id(),
                        is_utc ? 0u : RECORD_FLAG_TIME_UNSET);

    uint32_t t0 = millis();
    storage_result_t r = storage_append(&rec);
    uint32_t t_append = millis() - t0;

    record_t back;
    memset(&back, 0, sizeof back);
    storage_result_t rb = storage_read(rec.seq, &back);

    Serial.printf("seq %5lu | boot %u | ts %10lu %s | append %s %3lu ms | read back %s %s\n",
                  rec.seq, back.boot_id, back.timestamp,
                  (back.flags & RECORD_FLAG_TIME_UNSET) ? "(uptime)" : "(UTC)   ",
                  storage_result_name(r), t_append, storage_result_name(rb),
                  (rb == STORAGE_OK && records_equal(&rec, &back)) ? "MATCH" : "MISMATCH");
    delay(3000);
}
