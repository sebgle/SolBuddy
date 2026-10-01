#include "drivers/AS7343/AS7343.h"
#include "drivers/AS7343/AS7343_REGS.h"
#include "hal/i2c.h"

/* ---- Register access helpers (private) ---------------------------------- */

static as7343_result_t from_i2c(i2c_result_t r)
{
    switch (r) {
        case I2C_OK:            return AS7343_OK;
        case I2C_ERR_ADDR_NACK: return AS7343_ERR_NOT_PRESENT;
        default:                return AS7343_ERR_BUS;
    }
}

static as7343_result_t read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[1] = { reg };
    return from_i2c(i2c_write_read(AS7343_I2C_ADDR, tx, sizeof tx, value, 1));
}

static as7343_result_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { reg, value };
    return from_i2c(i2c_write(AS7343_I2C_ADDR, tx, sizeof tx));
}

/*
 * Change only the bits in `mask`, preserving the rest (including reserved
 * bits). Not for CONTROL: it holds command bits that must not be replayed.
 */
static as7343_result_t modify_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current;
    as7343_result_t r = read_reg(reg, &current);
    if (r != AS7343_OK) return r;

    uint8_t next = (uint8_t)((current & ~mask) | (value & mask));
    if (next == current) return AS7343_OK;

    return write_reg(reg, next);
}

/*
 * 16-bit register pair. Low byte first, both in one transfer: the chip
 * latches the pair on the low byte and expects the high byte next (§9).
 */
static as7343_result_t write_reg16(uint8_t reg_low, uint16_t value)
{
    uint8_t tx[3] = { reg_low, (uint8_t)(value & 0xFFu), (uint8_t)(value >> 8) };
    return from_i2c(i2c_write(AS7343_I2C_ADDR, tx, sizeof tx));
}

/* Bank 1: registers 0x20-0x7F. Bank 0: 0x80 and above. CFG0 works in both. */
static as7343_result_t select_bank(uint8_t bank)
{
    return modify_reg(AS7343_REG_CFG0, AS7343_CFG0_REG_BANK_MASK,
                      bank ? AS7343_CFG0_REG_BANK_MASK : 0u);
}

/* ---- Configuration ------------------------------------------------------ */

/*
 * Default exposure: 30 x 600 steps x 2.78 us = 50 ms integration,
 * full scale (29+1) x (599+1) = 18000 counts. Revisited with set_exposure.
 */
#define DEFAULT_GAIN    AS7343_GAIN_256X
#define DEFAULT_ATIME   29u
#define DEFAULT_ASTEP   599u

_Static_assert(DEFAULT_ATIME != 0u || DEFAULT_ASTEP != 0u, "ATIME and ASTEP must not both be 0");
_Static_assert(DEFAULT_ASTEP <= AS7343_ASTEP_MAX, "ASTEP 65535 is reserved");
_Static_assert(DEFAULT_GAIN <= AS7343_GAIN_2048X, "gain code out of range");

static as7343_result_t check_id(void)
{
    as7343_result_t r = select_bank(1);
    if (r != AS7343_OK) return r;

    uint8_t id = 0;
    as7343_result_t r_id = read_reg(AS7343_REG_ID, &id);

    /* Always return to bank 0, even if the ID read failed. */
    r = select_bank(0);

    if (r_id != AS7343_OK) return r_id;
    if (r != AS7343_OK)    return r;
    if (id != AS7343_DEVICE_ID_EXPECTED) return AS7343_ERR_WRONG_ID;
    return AS7343_OK;
}

/* Write every setting we depend on; never trust power-on defaults. */
static as7343_result_t configure(void)
{
    as7343_result_t r;

    /* §10.2.1: PON first, measurement stopped, before configuring. */
    r = modify_reg(AS7343_REG_ENABLE,
                   AS7343_ENABLE_PON_MASK | AS7343_ENABLE_SP_EN_MASK |
                   AS7343_ENABLE_WEN_MASK | AS7343_ENABLE_FDEN_MASK,
                   AS7343_ENABLE_PON_MASK);
    if (r != AS7343_OK) return r;

    r = modify_reg(AS7343_REG_CFG20, AS7343_CFG20_AUTO_SMUX_MASK,
                   AS7343_AUTO_SMUX_18_CHANNEL << AS7343_CFG20_AUTO_SMUX_POS);
    if (r != AS7343_OK) return r;

    r = modify_reg(AS7343_REG_CFG1, AS7343_CFG1_AGAIN_MASK,
                   DEFAULT_GAIN << AS7343_CFG1_AGAIN_POS);
    if (r != AS7343_OK) return r;

    r = write_reg(AS7343_REG_ATIME, DEFAULT_ATIME);
    if (r != AS7343_OK) return r;

    r = write_reg16(AS7343_REG_ASTEP_L, DEFAULT_ASTEP);
    if (r != AS7343_OK) return r;

    r = write_reg(AS7343_REG_AZ_CONFIG, AS7343_AZERO_FIRST_CYCLE_ONLY);
    if (r != AS7343_OK) return r;

    /* v1 polls: no interrupts, no sleep-after-interrupt. */
    r = modify_reg(AS7343_REG_INTENAB,
                   AS7343_INTENAB_ASIEN_MASK | AS7343_INTENAB_SP_IEN_MASK |
                   AS7343_INTENAB_F_IEN_MASK | AS7343_INTENAB_SIEN_MASK,
                   0u);
    if (r != AS7343_OK) return r;

    r = modify_reg(AS7343_REG_CFG3, AS7343_CFG3_SAI_MASK, 0u);
    if (r != AS7343_OK) return r;

    return modify_reg(AS7343_REG_LED, AS7343_LED_ACT_MASK, 0u);
}

/* ---- Public API --------------------------------------------------------- */

as7343_result_t as7343_init(void)
{
    as7343_result_t r = check_id();
    if (r != AS7343_OK) return r;

    r = configure();

    /* Always try to leave the sensor asleep, even if configuring failed. */
    as7343_result_t r_off = modify_reg(AS7343_REG_ENABLE, AS7343_ENABLE_PON_MASK, 0u);

    if (r != AS7343_OK) return r;
    return r_off;
}
