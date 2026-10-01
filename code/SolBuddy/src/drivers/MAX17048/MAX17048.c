#include "drivers/MAX17048/MAX17048.h"
#include "hal/i2c.h"

#define MAX17048_I2C_ADDR   0x36u

/* Registers (datasheet Table 2). */
#define REG_VCELL           0x02u   /* 78.125 uV/LSb */
#define REG_SOC             0x04u   /* 1/256 %/LSb */
#define REG_VERSION         0x08u   /* 0x001_ */
#define REG_CONFIG          0x0Cu
#define REG_VALRT           0x14u   /* MSB = MIN, LSB = MAX, 20 mV/LSb */
#define REG_CRATE           0x16u   /* 0.208 %/h per LSb, signed */
#define REG_STATUS          0x1Au

#define VERSION_MASK        0xFFF0u
#define VERSION_EXPECTED    0x0010u

/* CONFIG: RCOMP (MSB) = 0x97 POR default; LSB = SLEEP|ALSC|ALRT|ATHD[4:0].
 * ATHD 0x1C = empty alert at 4 % (POR default). Written explicitly. */
#define CONFIG_DEFAULT      0x971Cu
#define CONFIG_ALRT         0x0020u

/* STATUS (MSB): EnVr bit 6, SC 5, HD 4, VR 3, VL 2, VH 1, RI 0. */
#define STATUS_ENVR         0x4000u
#define STATUS_RI           0x0100u
#define STATUS_ALERT_BITS   0x3F00u   /* SC|HD|VR|VL|VH|RI */

#define VALRT_STEP_MV       20u
#define VALRT_MAX_OFF       0xFFu     /* VALRT.MAX = 5.1 V: high alert off */

static max17048_result_t from_i2c(i2c_result_t r)
{
    switch (r) {
        case I2C_OK:            return MAX17048_OK;
        case I2C_ERR_ADDR_NACK: return MAX17048_ERR_NOT_PRESENT;
        default:                return MAX17048_ERR_BUS;
    }
}

/* 16-bit registers, MSB first on the wire. */
static max17048_result_t read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t tx[1] = { reg };
    uint8_t rx[2] = { 0, 0 };
    max17048_result_t r = from_i2c(i2c_write_read(MAX17048_I2C_ADDR, tx, sizeof tx, rx, sizeof rx));
    if (r == MAX17048_OK) *value = (uint16_t)((rx[0] << 8) | rx[1]);
    return r;
}

static max17048_result_t write_reg(uint8_t reg, uint16_t value)
{
    uint8_t tx[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFFu) };
    return from_i2c(i2c_write(MAX17048_I2C_ADDR, tx, sizeof tx));
}

max17048_result_t max17048_init(uint16_t low_alert_mv)
{
    if (low_alert_mv > VALRT_MAX_OFF * VALRT_STEP_MV) return MAX17048_ERR_ARG;

    uint16_t version = 0;
    max17048_result_t r = read_reg(REG_VERSION, &version);
    if (r != MAX17048_OK) return r;
    if ((version & VERSION_MASK) != VERSION_EXPECTED) return MAX17048_ERR_WRONG_ID;

    r = write_reg(REG_CONFIG, CONFIG_DEFAULT);
    if (r != MAX17048_OK) return r;

    uint16_t valrt = (uint16_t)(((low_alert_mv / VALRT_STEP_MV) << 8) | VALRT_MAX_OFF);
    r = write_reg(REG_VALRT, valrt);
    if (r != MAX17048_OK) return r;

    /* Clear RI ("not configured") last, once configuration succeeded. */
    uint16_t status = 0;
    r = read_reg(REG_STATUS, &status);
    if (r != MAX17048_OK) return r;
    return write_reg(REG_STATUS, (uint16_t)(status & ~STATUS_RI));
}

max17048_result_t max17048_read(max17048_reading_t *reading)
{
    uint16_t vcell = 0, soc = 0, crate = 0;
    max17048_result_t r = read_reg(REG_VCELL, &vcell);
    if (r == MAX17048_OK) r = read_reg(REG_SOC, &soc);
    if (r == MAX17048_OK) r = read_reg(REG_CRATE, &crate);
    if (r != MAX17048_OK) return r;

    /* 78.125 uV = 5/64 mV, so mV = raw * 5 / 64 (32-bit: no overflow). */
    reading->vcell_mv  = (uint16_t)(((uint32_t)vcell * 5u) / 64u);
    reading->soc_x256  = soc;
    reading->crate_raw = (int16_t)crate;
    return MAX17048_OK;
}

max17048_result_t max17048_read_alerts(uint8_t *alerts)
{
    uint16_t status = 0;
    max17048_result_t r = read_reg(REG_STATUS, &status);
    if (r != MAX17048_OK) return r;

    /* RI, VH, VL, VR, HD, SC occupy STATUS bits 8..13 in that order, which
     * is exactly the MAX17048_ALERT_* layout shifted down by 8. */
    *alerts = (uint8_t)((status & STATUS_ALERT_BITS) >> 8);
    return MAX17048_OK;
}

max17048_result_t max17048_clear_alerts(void)
{
    uint16_t status = 0;
    max17048_result_t r = read_reg(REG_STATUS, &status);
    if (r != MAX17048_OK) return r;
    r = write_reg(REG_STATUS, (uint16_t)(status & ~STATUS_ALERT_BITS));
    if (r != MAX17048_OK) return r;

    /* The ALRT pin stays low until CONFIG.ALRT is cleared too. */
    uint16_t config = 0;
    r = read_reg(REG_CONFIG, &config);
    if (r != MAX17048_OK) return r;
    return write_reg(REG_CONFIG, (uint16_t)(config & ~CONFIG_ALRT));
}
