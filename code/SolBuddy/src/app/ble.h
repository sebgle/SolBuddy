#ifndef SOLBUDDY_APP_BLE_H
#define SOLBUDDY_APP_BLE_H

/*
 * BLE peripheral: the SolBuddy GATT service (docs/ble_protocol.md) on top
 * of Adafruit Bluefruit / the Nordic SoftDevice.
 */

#include <stdint.h>
#include <stdbool.h>

#include "services/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the SoftDevice, register the service, start the sync task, start
 * advertising. Requires storage_init() and clock_init() first. */
void ble_init(void);

/* Advertised name, e.g. "SolBuddy-1A2B". Valid after ble_init. */
const char *ble_name(void);

bool ble_connected(void);

/* True while a Log Read transfer is running (state SYNC). */
bool ble_syncing(void);

/* Last Log Read transfer: connection interval (1.25 ms units) and how many
 * times a notification had to wait for a free transmit slot. */
void ble_last_transfer(uint16_t *interval_units, uint32_t *retries);

/* Most recent write to Time (chr_id 3) or Log Read (4) and its
 * proto_result_t. Returns a count that increases with every write, so a
 * caller can tell when a new one arrived. */
uint32_t ble_last_write(uint8_t *chr_id, uint8_t *result, uint16_t *len);

/* Replace the Status value (protocol §5.1); also refreshes Time's value. */
void ble_set_status(const proto_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
