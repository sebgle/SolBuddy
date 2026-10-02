/*
 * Bring-up test: BLE + sampling + storage together (errata 244 clock hold).
 * Logs a record every 3 s and reads it back while advertising/connected;
 * Status carries the real boot_id, uptime and stored range.
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
#include "services/record.h"

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Partial Status (protocol §5.1); battery and faults come later. */
static void update_status(void)
{
    uint8_t s[BLE_STATUS_LEN];
    memset(s, 0, sizeof s);
    bool is_utc = false;
    uint32_t ts = clock_timestamp(&is_utc);

    s[0] = 1;                                   /* protocol_version */
    s[2] = is_utc ? 0x01 : 0x00;                /* flags: bit0 UTC set */
    put_u16(&s[6],  storage_boot_id());
    put_u32(&s[8],  clock_uptime_s());
    put_u32(&s[12], is_utc ? ts : 0u);
    put_u32(&s[20], storage_oldest_seq());
    put_u32(&s[24], storage_next_seq());
    ble_set_status(s);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    clock_init();
    ble_init();                 /* SoftDevice running from here on */
    i2c_init();
    Serial.printf("advertising as %s\n", ble_name());
    Serial.printf("as7343_init -> %d, sampler_init -> %d\n", as7343_init(), sampler_init());
    Serial.printf("storage_init -> %d | seqs [%lu, %lu) | boot_id %u\n", storage_init(),
                  storage_oldest_seq(), storage_next_seq(), storage_boot_id());
    update_status();
}

void loop()
{
    static bool was_connected = false;
    bool connected = ble_connected();
    if (connected != was_connected) {
        Serial.println(connected ? "phone connected" : "phone disconnected");
        was_connected = connected;
    }

    as7343_reading_t reading;
    uint8_t attempts = 0;
    if (sampler_take_sample(&reading, &attempts) == AS7343_OK) {
        bool is_utc = false;
        uint32_t ts = clock_timestamp(&is_utc);
        record_t rec, back;
        record_from_reading(&rec, &reading, 0, ts, storage_boot_id(),
                            is_utc ? 0u : RECORD_FLAG_TIME_UNSET);

        uint32_t t0 = millis();
        storage_result_t r = storage_append(&rec);
        uint32_t took = millis() - t0;
        memset(&back, 0, sizeof back);
        storage_result_t rb = storage_read(rec.seq, &back);
        bool match = (rb == STORAGE_OK) && back.seq == rec.seq &&
                     memcmp(back.counts, rec.counts, sizeof rec.counts) == 0;

        Serial.printf("seq %5lu | %s | append %d in %2lu ms | read back %s%s\n",
                      rec.seq, connected ? "BLE connected" : "advertising  ",
                      r, took, match ? "MATCH" : "MISMATCH",
                      (r == STORAGE_OK && match) ? "" : "   <-- FAIL");
    }

    update_status();
    delay(3000);
}
