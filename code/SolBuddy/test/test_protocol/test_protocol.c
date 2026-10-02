/* Unit tests for services/protocol (docs/ble_protocol.md v1).
 * Run on the PC: pio test -e native */
#include <string.h>
#include <unity.h>

#include "services/protocol.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- Status (§5.1) ------------------------------------------------------ */

static void test_status_fields_at_documented_offsets(void)
{
    proto_status_t s = {
        .state = PROTO_STATE_SYNC, .flags = PROTO_STATUS_FLAG_UTC_SET | PROTO_STATUS_FLAG_USB,
        .fw_version = 0x0102, .boot_id = 0x0A0B, .uptime_s = 0x11223344, .utc_s = 0x55667788,
        .vcell_mv = 3700, .soc_x256 = 0x3280, .oldest_seq = 0x01020304, .next_seq = 0x05060708,
        .err_i2c = 0x1111, .err_flash = 0x2222, .err_sensor = 0x3333,
    };
    uint8_t b[PROTO_STATUS_LEN];
    memset(b, 0xAA, sizeof b);
    proto_pack_status(&s, b);

    const uint8_t expected[PROTO_STATUS_LEN] = {
        0x01, 0x01, 0x03, 0x00,                 /* version, state, flags, reserved */
        0x02, 0x01,  0x0B, 0x0A,                /* fw_version, boot_id */
        0x44, 0x33, 0x22, 0x11,                 /* uptime_s */
        0x88, 0x77, 0x66, 0x55,                 /* utc_s */
        0x74, 0x0E,  0x80, 0x32,                /* vcell_mv 3700, soc_x256 */
        0x04, 0x03, 0x02, 0x01,                 /* oldest_seq */
        0x08, 0x07, 0x06, 0x05,                 /* next_seq */
        0x11, 0x11,  0x22, 0x22,  0x33, 0x33,   /* err_i2c, err_flash, err_sensor */
        0x00, 0x00,                             /* reserved */
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b, PROTO_STATUS_LEN);
}

/* ---- Time (§5.2) -------------------------------------------------------- */

static void test_time_parses_little_endian_utc(void)
{
    const uint8_t d[4] = { 0x00, 0x87, 0xBE, 0x6A };   /* 0x6ABE8700 = 1790871296 */
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL(PROTO_OK, proto_parse_time(d, sizeof d, &utc));
    TEST_ASSERT_EQUAL_UINT32(0x6ABE8700u, utc);
}

static void test_time_rejects_wrong_length(void)
{
    const uint8_t d[5] = { 0 };
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_time(d, 3, &utc));
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_time(d, 5, &utc));
}

static void test_time_rejects_unset_phone_clock(void)
{
    uint32_t utc = 0;
    const uint8_t before[4] = { 0x7F, 0x00, 0x92, 0x65 };   /* PROTO_UTC_MIN - 1 = 0x6592007F */
    TEST_ASSERT_EQUAL(PROTO_ERR_VALUE, proto_parse_time(before, 4, &utc));
    const uint8_t at[4] = { 0x80, 0x00, 0x92, 0x65 };       /* PROTO_UTC_MIN = 0x65920080 */
    TEST_ASSERT_EQUAL(PROTO_OK, proto_parse_time(at, 4, &utc));
}

/* ---- Config (§5.4) ------------------------------------------------------ */

static void test_config_defaults_match_the_spec(void)
{
    proto_config_t c;
    proto_config_defaults(&c);
    TEST_ASSERT_EQUAL_UINT8(5, c.live_timeout_min);
    TEST_ASSERT_EQUAL_UINT16(30, c.sample_interval_s);
    TEST_ASSERT_EQUAL_UINT16(1285, c.adv_interval_ms);
}

static void test_config_packs_at_documented_offsets(void)
{
    proto_config_t c = { .live_timeout_min = 5, .sample_interval_s = 30, .adv_interval_ms = 1285 };
    uint8_t b[PROTO_CONFIG_LEN];
    proto_pack_config(&c, b);
    const uint8_t expected[PROTO_CONFIG_LEN] = { 0x01, 0x05, 0x1E, 0x00, 0x05, 0x05, 0x00, 0x00 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b, PROTO_CONFIG_LEN);
}

static void test_config_round_trips(void)
{
    proto_config_t in = { .live_timeout_min = 60, .sample_interval_s = 3600, .adv_interval_ms = 20 };
    uint8_t b[PROTO_CONFIG_LEN];
    proto_pack_config(&in, b);
    proto_config_t out;
    proto_config_defaults(&out);
    TEST_ASSERT_EQUAL(PROTO_OK, proto_parse_config(b, sizeof b, &out));
    TEST_ASSERT_EQUAL_UINT8(60, out.live_timeout_min);
    TEST_ASSERT_EQUAL_UINT16(3600, out.sample_interval_s);
    TEST_ASSERT_EQUAL_UINT16(20, out.adv_interval_ms);
}

static void assert_config_rejected(const uint8_t b[PROTO_CONFIG_LEN], proto_result_t expected)
{
    proto_config_t c;
    proto_config_defaults(&c);
    TEST_ASSERT_EQUAL(expected, proto_parse_config(b, PROTO_CONFIG_LEN, &c));
    /* all-or-nothing: unchanged */
    TEST_ASSERT_EQUAL_UINT8(5, c.live_timeout_min);
    TEST_ASSERT_EQUAL_UINT16(30, c.sample_interval_s);
    TEST_ASSERT_EQUAL_UINT16(1285, c.adv_interval_ms);
}

static void test_config_rejects_each_out_of_range_field(void)
{
    const uint8_t bad_version[8]  = { 0x02, 5, 30, 0, 0x05, 0x05, 0, 0 };
    const uint8_t live_zero[8]    = { 0x01, 0, 30, 0, 0x05, 0x05, 0, 0 };
    const uint8_t live_61[8]      = { 0x01, 61, 30, 0, 0x05, 0x05, 0, 0 };
    const uint8_t interval_9[8]   = { 0x01, 5, 9, 0, 0x05, 0x05, 0, 0 };
    const uint8_t interval_3601[8]= { 0x01, 5, 0x11, 0x0E, 0x05, 0x05, 0, 0 };
    const uint8_t adv_19[8]       = { 0x01, 5, 30, 0, 19, 0, 0, 0 };
    const uint8_t adv_10241[8]    = { 0x01, 5, 30, 0, 0x01, 0x28, 0, 0 };
    assert_config_rejected(bad_version, PROTO_ERR_VALUE);
    assert_config_rejected(live_zero, PROTO_ERR_VALUE);
    assert_config_rejected(live_61, PROTO_ERR_VALUE);
    assert_config_rejected(interval_9, PROTO_ERR_VALUE);
    assert_config_rejected(interval_3601, PROTO_ERR_VALUE);
    assert_config_rejected(adv_19, PROTO_ERR_VALUE);
    assert_config_rejected(adv_10241, PROTO_ERR_VALUE);
}

static void test_config_rejects_wrong_length(void)
{
    const uint8_t b[9] = { 0x01, 5, 30, 0, 0x05, 0x05, 0, 0, 0 };
    proto_config_t c;
    proto_config_defaults(&c);
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_config(b, 7, &c));
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_config(b, 9, &c));
    TEST_ASSERT_EQUAL_UINT16(30, c.sample_interval_s);
}

/* ---- Log Read (§5.3) ---------------------------------------------------- */

static void test_log_start_parses_seq_and_count(void)
{
    const uint8_t d[9] = { 0x01, 0x10, 0x27, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00 };
    proto_log_cmd_t cmd;
    TEST_ASSERT_EQUAL(PROTO_OK, proto_parse_log_cmd(d, sizeof d, &cmd));
    TEST_ASSERT_EQUAL_UINT8(PROTO_LOG_OP_START, cmd.op);
    TEST_ASSERT_EQUAL_UINT32(10000, cmd.from_seq);
    TEST_ASSERT_EQUAL_UINT32(100, cmd.max_count);
}

static void test_log_stop_parses(void)
{
    const uint8_t d[1] = { 0x02 };
    proto_log_cmd_t cmd;
    TEST_ASSERT_EQUAL(PROTO_OK, proto_parse_log_cmd(d, sizeof d, &cmd));
    TEST_ASSERT_EQUAL_UINT8(PROTO_LOG_OP_STOP, cmd.op);
}

static void test_log_cmd_rejects_bad_requests(void)
{
    const uint8_t start_short[5] = { 0x01, 0, 0, 0, 0 };
    const uint8_t stop_long[2]   = { 0x02, 0 };
    const uint8_t unknown[1]     = { 0x7F };
    proto_log_cmd_t cmd;
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_log_cmd(start_short, sizeof start_short, &cmd));
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_log_cmd(stop_long, sizeof stop_long, &cmd));
    TEST_ASSERT_EQUAL(PROTO_ERR_VALUE, proto_parse_log_cmd(unknown, sizeof unknown, &cmd));
    TEST_ASSERT_EQUAL(PROTO_ERR_LENGTH, proto_parse_log_cmd(unknown, 0, &cmd));
}

static void test_end_packet_layout(void)
{
    uint8_t b[PROTO_END_LEN];
    proto_pack_end(PROTO_END_CAUGHT_UP, 0x00012345u, b);
    const uint8_t expected[PROTO_END_LEN] = { 0xE0, 0x00, 0x45, 0x23, 0x01, 0x00 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b, PROTO_END_LEN);
}

static void test_records_per_notification(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, proto_records_per_notification(23));    /* default MTU */
    TEST_ASSERT_EQUAL_UINT32(0, proto_records_per_notification(66));
    TEST_ASSERT_EQUAL_UINT32(1, proto_records_per_notification(67));
    TEST_ASSERT_EQUAL_UINT32(2, proto_records_per_notification(185));  /* iOS typical */
    TEST_ASSERT_EQUAL_UINT32(3, proto_records_per_notification(247));  /* our max */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_fields_at_documented_offsets);
    RUN_TEST(test_time_parses_little_endian_utc);
    RUN_TEST(test_time_rejects_wrong_length);
    RUN_TEST(test_time_rejects_unset_phone_clock);
    RUN_TEST(test_config_defaults_match_the_spec);
    RUN_TEST(test_config_packs_at_documented_offsets);
    RUN_TEST(test_config_round_trips);
    RUN_TEST(test_config_rejects_each_out_of_range_field);
    RUN_TEST(test_config_rejects_wrong_length);
    RUN_TEST(test_log_start_parses_seq_and_count);
    RUN_TEST(test_log_stop_parses);
    RUN_TEST(test_log_cmd_rejects_bad_requests);
    RUN_TEST(test_end_packet_layout);
    RUN_TEST(test_records_per_notification);
    return UNITY_END();
}
