/*
 * Bring-up test: BLE Time + Log Read on top of sampling + storage.
 * Logs a record every 3 s; a phone can set the time and pull the log.
 * Serial is only used to print results; it is not part of the firmware design.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include <string.h>

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "app/ble.h"
#include "app/clock.h"
#include "app/sampler.h"
#include "app/storage.h"
#include "services/protocol.h"
#include "services/record.h"

#define FW_VERSION  0x0001u     /* 0.1 */

static void update_status(void)
{
    bool is_utc = false;
    uint32_t ts = clock_timestamp(&is_utc);

    proto_status_t s;
    memset(&s, 0, sizeof s);
    s.state      = ble_syncing() ? PROTO_STATE_SYNC : PROTO_STATE_NORMAL;
    s.flags      = is_utc ? PROTO_STATUS_FLAG_UTC_SET : 0u;
    s.fw_version = FW_VERSION;
    s.boot_id    = storage_boot_id();
    s.uptime_s   = clock_uptime_s();
    s.utc_s      = is_utc ? ts : 0u;
    s.oldest_seq = storage_oldest_seq();
    s.next_seq   = storage_next_seq();
    ble_set_status(&s);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    clock_init();
    i2c_init();
    Serial.printf("as7343_init -> %d, sampler_init -> %d\n", as7343_init(), sampler_init());
    Serial.printf("storage_init -> %d | seqs [%lu, %lu) | boot_id %u\n", storage_init(),
                  storage_oldest_seq(), storage_next_seq(), storage_boot_id());
    ble_init();
    Serial.printf("advertising as %s\n", ble_name());
    update_status();
}

void loop()
{
    static bool was_connected = false, was_syncing = false, was_utc = false;

    bool connected = ble_connected();
    if (connected != was_connected) {
        Serial.println(connected ? "phone connected" : "phone disconnected");
        was_connected = connected;
    }
    static uint32_t seen_writes = 0;
    uint8_t w_chr = 0, w_result = 0;
    uint16_t w_len = 0;
    uint32_t writes = ble_last_write(&w_chr, &w_result, &w_len);
    if (writes != seen_writes) {
        static const char *const RESULT[] = { "ACCEPTED", "REJECTED: bad length (0x0D)",
                                              "REJECTED: out of range (0xFF)" };
        Serial.printf("write to %s, %u bytes -> %s\n", w_chr == 3 ? "Time" : "Log Read",
                      w_len, w_result < 3 ? RESULT[w_result] : "?");
        seen_writes = writes;
    }

    bool syncing = ble_syncing();
    if (syncing != was_syncing) {
        if (syncing) {
            Serial.println("Log Read transfer started");
        } else {
            uint16_t interval = 0;
            uint32_t retries = 0;
            ble_last_transfer(&interval, &retries);
            Serial.printf("Log Read transfer ended (connection interval %u.%02u ms, %lu slot waits)\n",
                          interval * 125u / 100u, (interval * 125u) % 100u, retries);
        }
        was_syncing = syncing;
    }

    as7343_reading_t reading;
    uint8_t attempts = 0;
    if (sampler_take_sample(&reading, &attempts) == AS7343_OK) {
        bool is_utc = false;
        uint32_t ts = clock_timestamp(&is_utc);
        if (is_utc && !was_utc) Serial.printf("phone set UTC: %lu\n", ts);
        was_utc = is_utc;

        record_t rec;
        record_from_reading(&rec, &reading, 0, ts, storage_boot_id(),
                            is_utc ? 0u : RECORD_FLAG_TIME_UNSET);
        storage_result_t r = storage_append(&rec);
        Serial.printf("logged seq %lu (%s) ts %lu %s\n", rec.seq, r == STORAGE_OK ? "ok" : "FAIL",
                      ts, is_utc ? "UTC" : "uptime");
    }

    update_status();
    delay(3000);
}
