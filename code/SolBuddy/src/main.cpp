/*
 * Bring-up test: as7343_init() writes the full configuration.
 * Serial is only used to print results; it is not part of the firmware design.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   /* provides USB Serial; PlatformIO only links it if included */

#include "hal/i2c.h"
#include "drivers/AS7343/AS7343.h"
#include "drivers/AS7343/AS7343_REGS.h"

static const char *as7343_result_name(as7343_result_t r)
{
    switch (r) {
        case AS7343_OK:              return "OK";
        case AS7343_ERR_NOT_PRESENT: return "ERR_NOT_PRESENT";
        case AS7343_ERR_BUS:         return "ERR_BUS";
        case AS7343_ERR_WRONG_ID:    return "ERR_WRONG_ID";
    }
    return "UNKNOWN";
}

/* Test-only raw access, so the test checks the chip, not the driver's helpers. */
static void raw_write(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { reg, value };
    i2c_write(AS7343_I2C_ADDR, tx, sizeof tx);
}

static bool check(const char *name, uint8_t reg, uint8_t mask, uint8_t expected)
{
    uint8_t tx[1] = { reg };
    uint8_t value = 0;
    i2c_result_t r = i2c_write_read(AS7343_I2C_ADDR, tx, sizeof tx, &value, 1);
    bool ok = (r == I2C_OK) && ((value & mask) == expected);
    Serial.printf("  %-14s reg 0x%02X = 0x%02X, field 0x%02X, expected 0x%02X  %s\n",
                  name, reg, value, value & mask, expected, ok ? "ok" : "MISMATCH");
    return ok;
}

/* 16-bit pair read low byte first in one burst, per the latching rule (§9). */
static bool check16(const char *name, uint8_t reg_low, uint16_t expected)
{
    uint8_t tx[1] = { reg_low };
    uint8_t rx[2] = { 0, 0 };
    i2c_result_t r = i2c_write_read(AS7343_I2C_ADDR, tx, sizeof tx, rx, sizeof rx);
    uint16_t value = (uint16_t)(rx[0] | (rx[1] << 8));
    bool ok = (r == I2C_OK) && (value == expected);
    Serial.printf("  %-14s reg 0x%02X = %u, expected %u  %s\n",
                  name, reg_low, value, expected, ok ? "ok" : "MISMATCH");
    return ok;
}

static void config_test(void)
{
    /* Scribble non-default values so we know init really overwrites them. */
    raw_write(AS7343_REG_CFG1, AS7343_GAIN_2X);
    raw_write(AS7343_REG_ATIME, 5);
    raw_write(AS7343_REG_LED, AS7343_LED_ACT_MASK);

    as7343_result_t r = as7343_init();
    Serial.printf("as7343_init -> %s\n", as7343_result_name(r));
    if (r != AS7343_OK) {
        Serial.println("FAIL");
        return;
    }

    /* Expected values mirror the driver's defaults (gain 256x, ATIME 29, ASTEP 599). */
    bool pass = true;
    pass &= check("ENABLE",        AS7343_REG_ENABLE,
                  AS7343_ENABLE_PON_MASK | AS7343_ENABLE_SP_EN_MASK |
                  AS7343_ENABLE_WEN_MASK | AS7343_ENABLE_FDEN_MASK, 0x00);
    pass &= check("CFG20.auto_smux", AS7343_REG_CFG20, AS7343_CFG20_AUTO_SMUX_MASK,
                  AS7343_AUTO_SMUX_18_CHANNEL << AS7343_CFG20_AUTO_SMUX_POS);
    pass &= check("CFG1.AGAIN",    AS7343_REG_CFG1, AS7343_CFG1_AGAIN_MASK, AS7343_GAIN_256X);
    pass &= check("ATIME",         AS7343_REG_ATIME, 0xFF, 29);
    pass &= check16("ASTEP",       AS7343_REG_ASTEP_L, 599);
    pass &= check("AZ_CONFIG",     AS7343_REG_AZ_CONFIG, 0xFF, AS7343_AZERO_FIRST_CYCLE_ONLY);
    pass &= check("INTENAB",       AS7343_REG_INTENAB,
                  AS7343_INTENAB_ASIEN_MASK | AS7343_INTENAB_SP_IEN_MASK |
                  AS7343_INTENAB_F_IEN_MASK | AS7343_INTENAB_SIEN_MASK, 0x00);
    pass &= check("CFG3.SAI",      AS7343_REG_CFG3, AS7343_CFG3_SAI_MASK, 0x00);
    pass &= check("CFG3.reserved", AS7343_REG_CFG3, 0x0F, 0x0C);
    pass &= check("LED.LED_ACT",   AS7343_REG_LED, AS7343_LED_ACT_MASK, 0x00);

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
    Serial.printf("--- AS7343 config test, run %lu ---\n", ++run);
    config_test();
    delay(5000);
}
