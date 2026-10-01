/*
 * Unit tests for drivers/MAX17048 against a simulated gauge, attached to
 * the PC-only I2C bus (hal/host). Run on the PC: pio test -e native
 *
 * The simulated gauge holds 16-bit registers and speaks the MAX17048 wire
 * format (register, MSB, LSB).
 */
#include <string.h>
#include <unity.h>

#include "drivers/MAX17048/MAX17048.h"
#include "hal/host/i2c_host.h"

/* ---- Fake I2C bus with a MAX17048 at 0x36 ------------------------------- */

#define GAUGE_ADDR  0x36u

#define REG_VCELL   0x02u
#define REG_SOC     0x04u
#define REG_VERSION 0x08u
#define REG_CONFIG  0x0Cu
#define REG_VALRT   0x14u
#define REG_CRATE   0x16u
#define REG_STATUS  0x1Au

static uint16_t regs[256];      /* indexed by register address */
static int      gauge_present;
static int      writes_to[256]; /* how many times each register was written */

static i2c_result_t gauge_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (addr != GAUGE_ADDR || !gauge_present) return I2C_ERR_ADDR_NACK;
    TEST_ASSERT_EQUAL_MESSAGE(3, len, "gauge writes are register + 2 bytes");
    regs[data[0]] = (uint16_t)((data[1] << 8) | data[2]);   /* MSB first */
    writes_to[data[0]]++;
    return I2C_OK;
}

static i2c_result_t gauge_write_read(uint8_t addr, const uint8_t *tx, size_t tx_len,
                                     uint8_t *rx, size_t rx_len)
{
    if (addr != GAUGE_ADDR || !gauge_present) return I2C_ERR_ADDR_NACK;
    TEST_ASSERT_EQUAL_MESSAGE(1, tx_len, "gauge reads send one register byte");
    TEST_ASSERT_EQUAL_MESSAGE(2, rx_len, "gauge registers are 16-bit");
    uint16_t v = regs[tx[0]];
    rx[0] = (uint8_t)(v >> 8);      /* MSB first */
    rx[1] = (uint8_t)(v & 0xFF);
    return I2C_OK;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    memset(writes_to, 0, sizeof writes_to);
    gauge_present = 1;
    i2c_host_attach(gauge_write, gauge_write_read);
    /* Power-on state per datasheet Table 2 / register descriptions. */
    regs[REG_VERSION] = 0x0012;
    regs[REG_CONFIG]  = 0x971C;
    regs[REG_VALRT]   = 0x00FF;
    regs[REG_STATUS]  = 0x0100;     /* RI set: just powered up */
}

void tearDown(void) {}

/* ---- init --------------------------------------------------------------- */

static void test_init_reports_absent_gauge(void)
{
    gauge_present = 0;
    TEST_ASSERT_EQUAL(MAX17048_ERR_NOT_PRESENT, max17048_init(3300));
}

static void test_init_rejects_wrong_version(void)
{
    regs[REG_VERSION] = 0x0000;
    TEST_ASSERT_EQUAL(MAX17048_ERR_WRONG_ID, max17048_init(3300));
}

static void test_init_rejects_alert_voltage_above_range(void)
{
    /* VALRT.MIN is 8 bits x 20 mV: 0..5100 mV. */
    TEST_ASSERT_EQUAL(MAX17048_ERR_ARG, max17048_init(5120));
}

static void test_init_clears_reset_indicator_only(void)
{
    regs[REG_STATUS] = 0x4100;      /* EnVr + RI */
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_init(3300));
    TEST_ASSERT_EQUAL_HEX16(0x4000, regs[REG_STATUS]);
}

static void test_init_writes_config_with_default_rcomp_and_alert_cleared(void)
{
    regs[REG_CONFIG] = 0x123F;      /* scribbled: wrong RCOMP, ALRT set, ATHD 31 */
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_init(3300));
    /* RCOMP 0x97, SLEEP 0, ALSC 0, ALRT 0, ATHD 0x1C (4 %) */
    TEST_ASSERT_EQUAL_HEX16(0x971C, regs[REG_CONFIG]);
}

static void test_init_sets_low_voltage_alert(void)
{
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_init(3300));
    /* MSB = VALRT.MIN = 3300 / 20 = 165 = 0xA5; LSB = VALRT.MAX = 0xFF (off) */
    TEST_ASSERT_EQUAL_HEX16(0xA5FF, regs[REG_VALRT]);
}

/* ---- read --------------------------------------------------------------- */

static void test_read_converts_vcell_to_millivolts(void)
{
    /* 78.125 uV per LSb: 3700 mV = 47360 LSb = 0xB900. */
    regs[REG_VCELL] = 0xB900;
    max17048_reading_t r;
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_read(&r));
    TEST_ASSERT_EQUAL_UINT16(3700, r.vcell_mv);
}

static void test_read_vcell_full_scale(void)
{
    /* 0xFFFF x 78.125 uV = 5119.92 mV -> 5119 (truncated). */
    regs[REG_VCELL] = 0xFFFF;
    max17048_reading_t r;
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_read(&r));
    TEST_ASSERT_EQUAL_UINT16(5119, r.vcell_mv);
}

static void test_read_returns_raw_soc(void)
{
    regs[REG_SOC] = 0x3280;         /* 50.5 % */
    max17048_reading_t r;
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_read(&r));
    TEST_ASSERT_EQUAL_HEX16(0x3280, r.soc_x256);
}

static void test_read_crate_is_signed(void)
{
    regs[REG_CRATE] = 0xFFF6;       /* -10 x 0.208 %/h: discharging */
    max17048_reading_t r;
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_read(&r));
    TEST_ASSERT_EQUAL_INT16(-10, r.crate_raw);
}

static void test_read_reports_absent_gauge(void)
{
    gauge_present = 0;
    max17048_reading_t r;
    TEST_ASSERT_EQUAL(MAX17048_ERR_NOT_PRESENT, max17048_read(&r));
}

/* ---- alerts ------------------------------------------------------------- */

static void test_read_alerts_maps_status_bits(void)
{
    regs[REG_STATUS] = 0x4600;      /* EnVr, VL, VH */
    uint8_t alerts = 0;
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_read_alerts(&alerts));
    TEST_ASSERT_EQUAL_HEX8(MAX17048_ALERT_VOLT_LOW | MAX17048_ALERT_VOLT_HIGH, alerts);
}

static void test_clear_alerts_clears_status_and_config_alrt(void)
{
    regs[REG_STATUS] = 0x7F00;      /* EnVr + every alert bit */
    regs[REG_CONFIG] = 0x973C;      /* ALRT set */
    TEST_ASSERT_EQUAL(MAX17048_OK, max17048_clear_alerts());
    TEST_ASSERT_EQUAL_HEX16(0x4000, regs[REG_STATUS]);  /* EnVr kept */
    TEST_ASSERT_EQUAL_HEX16(0x971C, regs[REG_CONFIG]);  /* only ALRT cleared */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_reports_absent_gauge);
    RUN_TEST(test_init_rejects_wrong_version);
    RUN_TEST(test_init_rejects_alert_voltage_above_range);
    RUN_TEST(test_init_clears_reset_indicator_only);
    RUN_TEST(test_init_writes_config_with_default_rcomp_and_alert_cleared);
    RUN_TEST(test_init_sets_low_voltage_alert);
    RUN_TEST(test_read_converts_vcell_to_millivolts);
    RUN_TEST(test_read_vcell_full_scale);
    RUN_TEST(test_read_returns_raw_soc);
    RUN_TEST(test_read_crate_is_signed);
    RUN_TEST(test_read_reports_absent_gauge);
    RUN_TEST(test_read_alerts_maps_status_bits);
    RUN_TEST(test_clear_alerts_clears_status_and_config_alrt);
    return UNITY_END();
}
