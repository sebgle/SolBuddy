/*
 * Bring-up test: end to end. Sensor -> sampler -> record -> ring log -> flash,
 * then read back and compare. Press reset: the log must continue its seqs.
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
           memcmp(a->counts, b->counts, sizeof a->counts) == 0;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    i2c_init();
    Serial.printf("as7343_init  -> %d\n", as7343_init());
    Serial.printf("sampler_init -> %d\n", sampler_init());

    uint32_t t0 = millis();
    storage_result_t r = storage_init();
    Serial.printf("storage_init -> %s in %lu ms | stored seqs [%lu, %lu)\n",
                  storage_result_name(r), millis() - t0,
                  storage_oldest_seq(), storage_next_seq());
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

    /* No real clock yet: seconds since boot, flagged as such. */
    record_t rec;
    record_from_reading(&rec, &reading, 0, millis() / 1000u, RECORD_FLAG_TIME_UNSET);

    uint32_t t0 = millis();
    storage_result_t r = storage_append(&rec);
    uint32_t t_append = millis() - t0;

    record_t back;
    memset(&back, 0, sizeof back);
    storage_result_t rb = storage_read(rec.seq, &back);

    Serial.printf("seq %5lu | append %s %3lu ms | read back %s %s | gain %2u CLEAR %5u%s\n",
                  rec.seq, storage_result_name(r), t_append, storage_result_name(rb),
                  (rb == STORAGE_OK && records_equal(&rec, &back)) ? "MATCH" : "MISMATCH",
                  rec.gain, rec.counts[AS7343_SLOT_CLEAR_1],
                  (rec.seq % 64u == 0u) ? "  <- new sector (erase)" : "");
    delay(3000);
}
