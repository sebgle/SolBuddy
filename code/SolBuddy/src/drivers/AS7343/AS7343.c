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

/* Consecutive registers in one transfer; the chip auto-increments (§9). */
static as7343_result_t read_regs(uint8_t first_reg, uint8_t *buf, size_t len)
{
    uint8_t tx[1] = { first_reg };
    return from_i2c(i2c_write_read(AS7343_I2C_ADDR, tx, sizeof tx, buf, len));
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

/* Exposure last written to the chip; copied into every reading. */
static uint8_t  s_atime;
static uint16_t s_astep;

static bool exposure_valid(uint8_t gain, uint8_t atime, uint16_t astep)
{
    if (gain > AS7343_GAIN_2048X)       return false;
    if (astep > AS7343_ASTEP_MAX)       return false;  /* 65535 is reserved */
    if (atime == 0u && astep == 0u)     return false;
    return true;
}

/* Write gain, ATIME, ASTEP. The cache is updated register by register so it
 * always matches the chip, even if a later write fails. */
static as7343_result_t write_exposure(uint8_t gain, uint8_t atime, uint16_t astep)
{
    as7343_result_t r = modify_reg(AS7343_REG_CFG1, AS7343_CFG1_AGAIN_MASK,
                                   (uint8_t)(gain << AS7343_CFG1_AGAIN_POS));
    if (r != AS7343_OK) return r;

    r = write_reg(AS7343_REG_ATIME, atime);
    if (r != AS7343_OK) return r;
    s_atime = atime;

    r = write_reg16(AS7343_REG_ASTEP_L, astep);
    if (r != AS7343_OK) return r;
    s_astep = astep;

    return AS7343_OK;
}

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

    r = write_exposure(DEFAULT_GAIN, DEFAULT_ATIME, DEFAULT_ASTEP);
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

as7343_result_t as7343_set_exposure(uint8_t gain, uint8_t atime, uint16_t astep)
{
    if (!exposure_valid(gain, atime, astep)) return AS7343_ERR_ARG;

    /* §10.2.1: never change configuration while a measurement is running. */
    as7343_result_t r = modify_reg(AS7343_REG_ENABLE, AS7343_ENABLE_SP_EN_MASK, 0u);
    if (r != AS7343_OK) return r;

    return write_exposure(gain, atime, astep);
}

as7343_result_t as7343_start_measurement(void)
{
    /* §10.2.1: oscillator on (PON) first, then start the measurement. */
    as7343_result_t r = modify_reg(AS7343_REG_ENABLE, AS7343_ENABLE_PON_MASK,
                                   AS7343_ENABLE_PON_MASK);
    if (r != AS7343_OK) return r;

    return modify_reg(AS7343_REG_ENABLE, AS7343_ENABLE_SP_EN_MASK,
                      AS7343_ENABLE_SP_EN_MASK);
}

as7343_result_t as7343_data_ready(bool *ready)
{
    uint8_t status2 = 0;
    as7343_result_t r = read_reg(AS7343_REG_STATUS2, &status2);

    *ready = (r == AS7343_OK) && (status2 & AS7343_STATUS2_AVALID_MASK);
    return r;
}

_Static_assert(AS7343_FRAME_BYTE_COUNT == 1u + 2u * AS7343_DATA_SLOT_COUNT,
               "frame = ASTATUS + 18 low/high pairs");

as7343_result_t as7343_read(as7343_reading_t *reading)
{
    bool ready = false;
    as7343_result_t r = as7343_data_ready(&ready);
    if (r != AS7343_OK) return r;
    if (!ready) return AS7343_ERR_NOT_READY;

    /* Observed on hardware (undocumented): the first ASTATUS read after a
     * measurement returns 0x00 — that read performs the latch. A second
     * read returns the real gain/saturation status. So: one latch read,
     * discarded, then the full frame. */
    uint8_t latch;
    r = read_reg(AS7343_REG_ASTATUS, &latch);
    if (r != AS7343_OK) return r;

    /* ASTATUS + 36 data bytes in one burst: reading ASTATUS latches the
     * data, so everything in this frame belongs to the same measurement. */
    uint8_t frame[AS7343_FRAME_BYTE_COUNT];
    r = read_regs(AS7343_REG_ASTATUS, frame, sizeof frame);
    if (r != AS7343_OK) return r;

    uint8_t astatus = frame[0];
    reading->gain      = (uint8_t)((astatus & AS7343_ASTATUS_AGAIN_MASK) >> AS7343_ASTATUS_AGAIN_POS);
    reading->saturated = (astatus & AS7343_ASTATUS_ASAT_MASK) != 0u;
    reading->atime     = s_atime;
    reading->astep     = s_astep;

    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        uint8_t low  = frame[1u + 2u * i];
        uint8_t high = frame[2u + 2u * i];
        reading->counts[i] = (uint16_t)(low | (high << 8));
    }
    return AS7343_OK;
}

as7343_result_t as7343_sleep(void)
{
    return modify_reg(AS7343_REG_ENABLE,
                      AS7343_ENABLE_PON_MASK | AS7343_ENABLE_SP_EN_MASK, 0u);
}
