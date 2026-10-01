#include "hal/host/i2c_host.h"

static i2c_host_write_fn      s_write;
static i2c_host_write_read_fn s_write_read;

void i2c_host_attach(i2c_host_write_fn write, i2c_host_write_read_fn write_read)
{
    s_write      = write;
    s_write_read = write_read;
}

void i2c_init(void) {}

i2c_result_t i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return s_write ? s_write(addr, data, len) : I2C_ERR_ADDR_NACK;
}

i2c_result_t i2c_write_read(uint8_t addr, const uint8_t *tx, size_t tx_len,
                            uint8_t *rx, size_t rx_len)
{
    return s_write_read ? s_write_read(addr, tx, tx_len, rx, rx_len) : I2C_ERR_ADDR_NACK;
}
