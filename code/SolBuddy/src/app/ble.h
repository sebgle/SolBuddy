#ifndef SOLBUDDY_APP_BLE_H
#define SOLBUDDY_APP_BLE_H

/*
 * BLE peripheral: the SolBuddy GATT service (docs/ble_protocol.md) on top
 * of Adafruit Bluefruit / the Nordic SoftDevice.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_STATUS_LEN  36u

/* Start the SoftDevice, register the service, start advertising. */
void ble_init(void);

/* Advertised name, e.g. "SolBuddy-1A2B". Valid after ble_init. */
const char *ble_name(void);

bool ble_connected(void);

/* Replace the Status value (docs/ble_protocol.md §5.1). */
void ble_set_status(const uint8_t status[BLE_STATUS_LEN]);

#ifdef __cplusplus
}
#endif

#endif
