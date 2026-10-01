#ifndef SOLBUDDY_HAL_I2C_H
#define SOLBUDDY_HAL_I2C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I2C_OK = 0,
    I2C_ERR_ARG,        /* bad argument, or a buffer EasyDMA cannot reach */
    I2C_ERR_ADDR_NACK,  /* nobody answered at that address */
    I2C_ERR_DATA_NACK,  /* device answered, then rejected a byte */
    I2C_ERR_SHORT,      /* fewer bytes moved than requested */
    I2C_ERR_TIMEOUT,    /* bus never finished; likely held low */
    I2C_ERR_BUS         /* any other controller error */
} i2c_result_t;

/* Configure the bus pins and controller. Call once at boot. */
void i2c_init(void);

/* START, address+W, data[0..len-1], STOP. */
i2c_result_t i2c_write(uint8_t addr, const uint8_t *data, size_t len);

/* START, address+W, tx[..], repeated START, address+R, rx[..], STOP.
 * Typical use: tx = { register }, rx = the register's contents. */
i2c_result_t i2c_write_read(uint8_t addr,
                            const uint8_t *tx, size_t tx_len,
                            uint8_t *rx, size_t rx_len);

#ifdef __cplusplus
}
#endif

#endif
