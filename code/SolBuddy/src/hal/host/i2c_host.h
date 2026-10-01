#ifndef SOLBUDDY_HAL_HOST_I2C_HOST_H
#define SOLBUDDY_HAL_HOST_I2C_HOST_H

/*
 * PC-only implementation of hal/i2c, for unit tests (env:native).
 * Never built into the firmware. A test attaches functions that simulate
 * the device(s) on the bus; with nothing attached every transfer NACKs.
 */

#include "hal/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef i2c_result_t (*i2c_host_write_fn)(uint8_t addr, const uint8_t *data, size_t len);
typedef i2c_result_t (*i2c_host_write_read_fn)(uint8_t addr,
                                               const uint8_t *tx, size_t tx_len,
                                               uint8_t *rx, size_t rx_len);

/* Pass NULLs to detach. */
void i2c_host_attach(i2c_host_write_fn write, i2c_host_write_read_fn write_read);

#ifdef __cplusplus
}
#endif

#endif
