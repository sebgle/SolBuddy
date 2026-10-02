/*
 * C++ because Bluefruit is a C++ library; everything outside this file sees
 * only the C functions declared in app/ble.h.
 */
#include <bluefruit.h>
#include <stdio.h>

#include "app/ble.h"

/* UUID base a222xxxx-40cd-4ab3-a79e-6157aec9630e (docs/ble_protocol.md §4).
 * The SoftDevice wants 128-bit UUIDs least-significant byte first, so the
 * text form is reversed; xxxx is bytes 12 (low) and 13 (high). */
#define SOLBUDDY_UUID(lo, hi) \
    { 0x0e, 0x63, 0xc9, 0xae, 0x57, 0x61, 0x9e, 0xa7, \
      0xb3, 0x4a, 0xcd, 0x40, (lo), (hi), 0x22, 0xa2 }

static const uint8_t UUID_SERVICE[16] = SOLBUDDY_UUID(0x01, 0x00);
static const uint8_t UUID_STATUS[16]  = SOLBUDDY_UUID(0x02, 0x00);

/* Advertising intervals are in 0.625 ms units. */
#define ADV_FAST_UNITS      32u     /*   20 ms: FAST-ADV after reset/USB */
#define ADV_SLOW_UNITS      2056u   /* 1285 ms: normal (spec §3)          */
#define ADV_FAST_TIMEOUT_S  30u

static BLEService        s_service(UUID_SERVICE);
static BLECharacteristic s_status(UUID_STATUS);

static char          s_name[16];
static volatile bool s_connected;

/* Called from the Bluefruit task. Keep them short. */
static void on_connect(uint16_t conn_handle)
{
    (void)conn_handle;
    s_connected = true;
}

static void on_disconnect(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;
    (void)reason;
    s_connected = false;
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

    /* Security defaults in Bluefruit are already Just Works + bonding + LESC
     * (BLESecurity.cpp: io_caps NONE, bond 1, mitm 0). The permission below
     * is what enforces it: unencrypted reads are refused, which makes the
     * phone pair. */
    s_service.begin();

    s_status.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    s_status.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    s_status.setFixedLen(BLE_STATUS_LEN);
    s_status.begin();

    uint8_t initial[BLE_STATUS_LEN] = { 1 };   /* protocol_version = 1, rest 0 */
    s_status.write(initial, sizeof initial);

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

void ble_set_status(const uint8_t status[BLE_STATUS_LEN])
{
    s_status.write(status, BLE_STATUS_LEN);
}
