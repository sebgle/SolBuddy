/*
 * Bring-up test: read the AS7343's ID register over our own I2C layer.
 * Serial is only used to print results; it is not part of the firmware design.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343_REGS.h"

#define AS7343_I2C_ADDR     0x39u

static const char *result_name(i2c_result_t r)
{
    switch (r) {
        case I2C_OK:            return "OK";
        case I2C_ERR_ARG:       return "ERR_ARG";
        case I2C_ERR_ADDR_NACK: return "ERR_ADDR_NACK";
        case I2C_ERR_DATA_NACK: return "ERR_DATA_NACK";
        case I2C_ERR_SHORT:     return "ERR_SHORT";
        case I2C_ERR_TIMEOUT:   return "ERR_TIMEOUT";
        case I2C_ERR_BUS:       return "ERR_BUS";
    }
    return "UNKNOWN";
}

static void read_id_test(void)
{
    /* Local arrays live on the stack, i.e. in RAM, so EasyDMA can reach them. */
    uint8_t select_bank1[] = { AS7343_REG_CFG0, AS7343_CFG0_REG_BANK_MASK };
    uint8_t select_bank0[] = { AS7343_REG_CFG0, 0x00 };
    uint8_t id_reg[]       = { AS7343_REG_ID };
    uint8_t id             = 0;

    i2c_result_t r_bank1 = i2c_write(AS7343_I2C_ADDR, select_bank1, sizeof select_bank1);
    Serial.printf("bank 1 select: %s\n", result_name(r_bank1));
    if (r_bank1 != I2C_OK) {
        Serial.println("FAIL");
        return;
    }

    i2c_result_t r_id = i2c_write_read(AS7343_I2C_ADDR, id_reg, sizeof id_reg, &id, 1);
    Serial.printf("read ID:       %s, ID = 0x%02X (expected 0x%02X)\n",
                  result_name(r_id), id, AS7343_DEVICE_ID_EXPECTED);

    /* Always attempt to return to bank 0, even if the ID read failed. */
    i2c_result_t r_bank0 = i2c_write(AS7343_I2C_ADDR, select_bank0, sizeof select_bank0);
    Serial.printf("bank 0 select: %s\n", result_name(r_bank0));

    bool pass = r_id == I2C_OK
             && r_bank0 == I2C_OK
             && id == AS7343_DEVICE_ID_EXPECTED;
    Serial.println(pass ? "PASS" : "FAIL");
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(10);

    i2c_init();
}

void loop()
{
    static uint32_t run = 0;
    Serial.printf("--- AS7343 ID test, run %lu ---\n", ++run);
    read_id_test();
    delay(2000);
}
