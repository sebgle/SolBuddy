/*
 * C++ because Bluefruit is a C++ library; everything outside this file sees
 * only the C functions declared in app/ble.h.
 */
#include <bluefruit.h>
#include <stdio.h>
#include <string.h>

#include "app/ble.h"
#include "app/clock.h"
#include "app/storage.h"
#include "services/protocol.h"
#include "services/record.h"

/* UUID base a222xxxx-40cd-4ab3-a79e-6157aec9630e (docs/ble_protocol.md §4).
 * The SoftDevice wants 128-bit UUIDs least-significant byte first, so the
 * text form is reversed; xxxx is bytes 12 (low) and 13 (high). */
#define SOLBUDDY_UUID(lo, hi) \
    { 0x0e, 0x63, 0xc9, 0xae, 0x57, 0x61, 0x9e, 0xa7, \
      0xb3, 0x4a, 0xcd, 0x40, (lo), (hi), 0x22, 0xa2 }

static const uint8_t UUID_SERVICE[16]  = SOLBUDDY_UUID(0x01, 0x00);
static const uint8_t UUID_STATUS[16]   = SOLBUDDY_UUID(0x02, 0x00);
static const uint8_t UUID_TIME[16]     = SOLBUDDY_UUID(0x03, 0x00);
static const uint8_t UUID_LOG_READ[16] = SOLBUDDY_UUID(0x04, 0x00);

/* Advertising intervals are in 0.625 ms units. */
#define ADV_FAST_UNITS      32u     /*   20 ms: FAST-ADV after reset/USB */
#define ADV_SLOW_UNITS      2056u   /* 1285 ms: normal (spec §3)          */
#define ADV_FAST_TIMEOUT_S  30u

#define MAX_RECORDS_PER_NOTIFICATION  3u     /* MTU 247 -> 3 x 64 bytes */
#define SYNC_TASK_STACK_WORDS         512u   /* 2 KB */

static BLEService        s_service(UUID_SERVICE);
static BLECharacteristic s_status(UUID_STATUS);
static BLECharacteristic s_time(UUID_TIME);
static BLECharacteristic s_log_read(UUID_LOG_READ);

static char          s_name[16];
static volatile bool s_connected;
static volatile bool s_syncing;

/* ---- Log Read request, handed from the BLE task to the sync task ---------- */
/* The write callback only records the request and wakes the sync task; the
 * transfer itself runs in the sync task, which may block on flash and on
 * the radio. `generation` lets a new START or a STOP cancel a running one. */
static volatile uint32_t s_req_generation;
static volatile uint8_t  s_req_op;
static volatile uint32_t s_req_from_seq;
static volatile uint32_t s_req_max_count;
static volatile uint16_t s_req_conn;
static TaskHandle_t      s_sync_task;

/* ---- write authorization ---------------------------------------------------- */

static uint16_t gatt_status_for(proto_result_t r)
{
    switch (r) {
        case PROTO_OK:         return BLE_GATT_STATUS_SUCCESS;
        case PROTO_ERR_LENGTH: return BLE_GATT_STATUS_ATTERR_INVALID_ATT_VAL_LENGTH;
        default:               return BLE_GATT_STATUS_ATTERR_CPS_OUT_OF_RANGE;
    }
}

/* Accept or reject a write. `update` must always be 1 for writes
 * (ble_gatts.h); the status decides whether the value is stored. */
static void reply_write(uint16_t conn, const ble_gatts_evt_write_t *req, uint16_t status)
{
    ble_gatts_rw_authorize_reply_params_t reply;
    memset(&reply, 0, sizeof reply);
    reply.type                     = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
    reply.params.write.gatt_status = status;
    reply.params.write.update      = 1;
    reply.params.write.offset      = req->offset;
    reply.params.write.len         = req->len;
    reply.params.write.p_data      = req->data;
    sd_ble_gatts_rw_authorize_reply(conn, &reply);
}

/* Last write on each characteristic, for the app (or a test) to report. */
static volatile uint32_t s_write_count;
static volatile uint8_t  s_last_write_chr;      /* 3 = Time, 4 = Log Read */
static volatile uint8_t  s_last_write_result;   /* proto_result_t */
static volatile uint16_t s_last_write_len;

static void note_write(uint8_t chr_id, proto_result_t r, uint16_t len)
{
    s_last_write_chr    = chr_id;
    s_last_write_result = (uint8_t)r;
    s_last_write_len    = len;
    s_write_count++;
}

static void on_time_write(uint16_t conn, BLECharacteristic *chr, ble_gatts_evt_write_t *req)
{
    (void)chr;
    uint32_t utc = 0;
    proto_result_t r = proto_parse_time(req->data, req->len, &utc);
    if (r == PROTO_OK) clock_set_utc(utc);
    reply_write(conn, req, gatt_status_for(r));
    note_write(3, r, req->len);
}

static void on_log_read_write(uint16_t conn, BLECharacteristic *chr, ble_gatts_evt_write_t *req)
{
    (void)chr;
    proto_log_cmd_t cmd;
    proto_result_t r = proto_parse_log_cmd(req->data, req->len, &cmd);
    reply_write(conn, req, gatt_status_for(r));
    note_write(4, r, req->len);
    if (r != PROTO_OK) return;

    s_req_op        = cmd.op;
    s_req_from_seq  = cmd.from_seq;
    s_req_max_count = cmd.max_count;
    s_req_conn      = conn;
    s_req_generation++;                 /* cancels any transfer in progress */
    xTaskNotifyGive(s_sync_task);
}

/* ---- the sync task: streams records for one START -------------------------- */

/* Per-transfer diagnostics for the app to print. */
static volatile uint16_t s_xfer_interval_units;   /* connection interval, 1.25 ms units */
static volatile uint32_t s_xfer_retries;

/*
 * notify() takes one of the SoftDevice's 3 transmit slots and gives up if
 * none frees within 100 ms. Slots free when the peer acknowledges, once per
 * connection event, and a central may space those > 100 ms apart. So a
 * failure usually just means "not yet": retry the same packet. Our packets
 * fit in one ATT packet, so a failed notify sent nothing — no duplicates.
 */
#define NOTIFY_RETRY_DELAY_MS   10u
#define NOTIFY_STALL_LIMIT_MS   10000u

static bool notify_reliably(uint16_t conn, uint32_t generation, const uint8_t *data, uint16_t len)
{
    for (uint32_t waited = 0; waited < NOTIFY_STALL_LIMIT_MS; waited += NOTIFY_RETRY_DELAY_MS) {
        if (s_log_read.notify(conn, data, len)) return true;
        if (!s_connected || generation != s_req_generation) return false;
        s_xfer_retries++;
        vTaskDelay(pdMS_TO_TICKS(NOTIFY_RETRY_DELAY_MS));
    }
    return false;
}

static bool send_end(uint16_t conn, uint8_t reason, uint32_t next_seq)
{
    uint8_t pkt[PROTO_END_LEN];
    proto_pack_end(reason, next_seq, pkt);
    return notify_reliably(conn, s_req_generation, pkt, sizeof pkt);
}

static void run_transfer(uint32_t generation, uint16_t conn, uint32_t seq, uint32_t max_count)
{
    BLEConnection *c = Bluefruit.Connection(conn);
    if (c == NULL) return;

    uint32_t per_packet = proto_records_per_notification(c->getMtu());
    if (per_packet == 0u) {
        send_end(conn, PROTO_END_MTU_TOO_SMALL, seq);
        return;
    }
    if (per_packet > MAX_RECORDS_PER_NOTIFICATION) per_packet = MAX_RECORDS_PER_NOTIFICATION;

    static uint8_t buf[MAX_RECORDS_PER_NOTIFICATION * RECORD_SIZE];
    uint32_t sent = 0;

    s_xfer_interval_units = c->getConnectionInterval();
    s_xfer_retries        = 0;
    s_syncing = true;
    for (;;) {
        if (generation != s_req_generation || !s_connected) break;   /* cancelled */

        uint32_t want = per_packet;
        if (max_count != 0u) {
            if (sent >= max_count) { send_end(conn, PROTO_END_STOPPED, seq); break; }
            if (max_count - sent < want) want = max_count - sent;
        }

        uint32_t count = 0;
        if (storage_read_raw_batch(&seq, buf, want, &count) != STORAGE_OK) {
            send_end(conn, PROTO_END_STORAGE_ERROR, seq);
            break;
        }
        if (count > 0u) {
            /* Waits for a free transmit slot: radio-paced. */
            if (!notify_reliably(conn, generation, buf, (uint16_t)(count * RECORD_SIZE))) break;
            sent += count;
        } else if (seq >= storage_next_seq()) {
            send_end(conn, PROTO_END_CAUGHT_UP, seq);
            break;
        }
    }
    s_syncing = false;
}

static void sync_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t generation = s_req_generation;
        uint16_t conn       = s_req_conn;
        if (s_req_op == PROTO_LOG_OP_STOP) {
            send_end(conn, PROTO_END_STOPPED, s_req_from_seq);
            continue;
        }
        run_transfer(generation, conn, s_req_from_seq, s_req_max_count);
    }
}

/* ---- connection ------------------------------------------------------------- */

static void on_connect(uint16_t conn_handle)
{
    (void)conn_handle;
    s_connected = true;
}

static void on_disconnect(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;
    (void)reason;
    s_connected = false;          /* a running transfer sees this and stops */
}

static void start_advertising(void)
{
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(s_service);   /* 128-bit UUID: 18 of 31 bytes */
    Bluefruit.ScanResponse.addName();              /* name in the scan response */

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(ADV_FAST_UNITS, ADV_SLOW_UNITS);
    Bluefruit.Advertising.setFastTimeout(ADV_FAST_TIMEOUT_S);
    Bluefruit.Advertising.start(0);                /* 0 = never stop */
}

void ble_init(void)
{
    /* ATT MTU up to 247 (protocol §2.2 needs >= 67). Must precede begin(). */
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.autoConnLed(false);    /* SolBuddy has no LEDs */

    /* Name: last two address bytes. The address is stored LSB first, so
     * mac[1], mac[0] are the last two as the address is usually written. */
    uint8_t mac[6];
    Bluefruit.getAddr(mac);
    snprintf(s_name, sizeof s_name, "SolBuddy-%02X%02X", mac[1], mac[0]);
    Bluefruit.setName(s_name);

    Bluefruit.Periph.setConnectCallback(on_connect);
    Bluefruit.Periph.setDisconnectCallback(on_disconnect);

    /* Security: Bluefruit's defaults are Just Works + bonding + LESC; the
     * SECMODE_ENC_NO_MITM permissions below enforce an encrypted link on
     * every characteristic (protocol §2.1). */
    s_service.begin();

    s_status.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    s_status.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    s_status.setFixedLen(PROTO_STATUS_LEN);
    s_status.begin();

    /* Write authorization: our callback validates and answers with the
     * exact ATT error. false = run it directly in the BLE event task. */
    s_time.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    s_time.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    s_time.setFixedLen(PROTO_TIME_LEN);
    s_time.setWriteAuthorizeCallback(on_time_write, false);
    s_time.begin();

    s_log_read.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    s_log_read.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    s_log_read.setMaxLen(MAX_RECORDS_PER_NOTIFICATION * RECORD_SIZE);
    s_log_read.setWriteAuthorizeCallback(on_log_read_write, false);
    s_log_read.begin();

    proto_status_t initial;
    memset(&initial, 0, sizeof initial);
    ble_set_status(&initial);

    xTaskCreate(sync_task, "sync", SYNC_TASK_STACK_WORDS, NULL, TASK_PRIO_LOW, &s_sync_task);

    start_advertising();
}

const char *ble_name(void)
{
    return s_name;
}

bool ble_connected(void)
{
    return s_connected;
}

bool ble_syncing(void)
{
    return s_syncing;
}

void ble_last_transfer(uint16_t *interval_units, uint32_t *retries)
{
    *interval_units = s_xfer_interval_units;
    *retries        = s_xfer_retries;
}

uint32_t ble_last_write(uint8_t *chr_id, uint8_t *result, uint16_t *len)
{
    *chr_id = s_last_write_chr;
    *result = s_last_write_result;
    *len    = s_last_write_len;
    return s_write_count;
}

void ble_set_status(const proto_status_t *status)
{
    uint8_t b[PROTO_STATUS_LEN];
    proto_pack_status(status, b);
    s_status.write(b, sizeof b);

    /* Time reads return the current UTC, 0 if not set (protocol §5.2). */
    bool is_utc = false;
    uint32_t t = clock_timestamp(&is_utc);
    uint8_t tb[PROTO_TIME_LEN] = { 0, 0, 0, 0 };
    if (is_utc) {
        tb[0] = (uint8_t)t; tb[1] = (uint8_t)(t >> 8); tb[2] = (uint8_t)(t >> 16); tb[3] = (uint8_t)(t >> 24);
    }
    s_time.write(tb, sizeof tb);
}
