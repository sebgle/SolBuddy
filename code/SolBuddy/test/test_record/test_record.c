/* Unit tests for services/record. Run on the PC: pio test -e native */
#include <string.h>
#include <unity.h>

#include "services/record.h"
#include "services/crc16.h"

void setUp(void) {}
void tearDown(void) {}

/* A record with every field distinct, so misplaced bytes are caught. */
static record_t sample_record(void)
{
    record_t rec;
    memset(&rec, 0, sizeof rec);
    rec.seq       = 0x11223344u;
    rec.timestamp = 0xA1B2C3D4u;
    rec.flags     = RECORD_FLAG_SATURATED | RECORD_FLAG_TIME_UNSET;
    rec.gain      = 9;
    rec.atime     = 29;
    rec.astep     = 0x0257;   /* 599 */
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        rec.counts[i] = (uint16_t)(0x0100u + i);
    }
    return rec;
}

static void packed_sample(uint8_t buf[RECORD_SIZE])
{
    record_t rec = sample_record();
    memset(buf, 0xAA, RECORD_SIZE);   /* garbage, so unwritten bytes show */
    record_pack(&rec, buf);
}

/* ---- pack: layout ------------------------------------------------------- */

static void test_pack_header_bytes_at_fixed_offsets(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    TEST_ASSERT_EQUAL_HEX8(RECORD_VERSION, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(RECORD_FLAG_SATURATED | RECORD_FLAG_TIME_UNSET, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(9, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(29, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x57, buf[4]);   /* astep, low byte first */
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[5]);
}

static void test_pack_seq_and_timestamp_little_endian(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    const uint8_t seq[4] = { 0x44, 0x33, 0x22, 0x11 };
    const uint8_t ts[4]  = { 0xD4, 0xC3, 0xB2, 0xA1 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(seq, &buf[8], 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ts, &buf[12], 4);
}

static void test_pack_counts_little_endian_in_slot_order(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX8(i, buf[16 + 2 * i]);         /* low byte = i */
        TEST_ASSERT_EQUAL_HEX8(0x01, buf[16 + 2 * i + 1]);  /* high byte */
    }
}

static void test_pack_zeroes_reserved_and_spare(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    TEST_ASSERT_EQUAL_HEX8(0, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0, buf[7]);
    for (uint32_t i = 52; i < 62; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, buf[i]);
    }
}

static void test_pack_crc_covers_bytes_0_to_61(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    uint16_t expected = crc16_ccitt(buf, 62);
    TEST_ASSERT_EQUAL_HEX8(expected & 0xFF, buf[62]);
    TEST_ASSERT_EQUAL_HEX8(expected >> 8, buf[63]);
}

/* ---- unpack ------------------------------------------------------------- */

static void test_unpack_round_trips_a_packed_record(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    record_t in = sample_record();
    record_t out;
    memset(&out, 0, sizeof out);

    TEST_ASSERT_EQUAL(RECORD_OK, record_unpack(buf, &out));
    TEST_ASSERT_EQUAL_HEX32(in.seq, out.seq);
    TEST_ASSERT_EQUAL_HEX32(in.timestamp, out.timestamp);
    TEST_ASSERT_EQUAL_HEX8(in.flags, out.flags);
    TEST_ASSERT_EQUAL_UINT8(in.gain, out.gain);
    TEST_ASSERT_EQUAL_UINT8(in.atime, out.atime);
    TEST_ASSERT_EQUAL_UINT16(in.astep, out.astep);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(in.counts, out.counts, AS7343_DATA_SLOT_COUNT);
}

static void test_unpack_rejects_a_corrupted_byte(void)
{
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    buf[30] ^= 0x01;   /* one bit flipped inside the counts */
    record_t out;
    TEST_ASSERT_EQUAL(RECORD_BAD_CRC, record_unpack(buf, &out));
}

static void test_unpack_detects_erased_flash(void)
{
    uint8_t buf[RECORD_SIZE];
    memset(buf, 0xFF, sizeof buf);
    record_t out;
    TEST_ASSERT_EQUAL(RECORD_ERASED, record_unpack(buf, &out));
}

static void test_unpack_partially_written_record_is_bad_crc(void)
{
    /* Power lost mid-write: first half written, the rest still erased. */
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    memset(&buf[32], 0xFF, 32);
    record_t out;
    TEST_ASSERT_EQUAL(RECORD_BAD_CRC, record_unpack(buf, &out));
}

static void test_unpack_rejects_unknown_version(void)
{
    /* Valid CRC but a future version number. */
    uint8_t buf[RECORD_SIZE];
    packed_sample(buf);
    buf[0] = RECORD_VERSION + 1;
    uint16_t crc = crc16_ccitt(buf, 62);
    buf[62] = crc & 0xFF;
    buf[63] = crc >> 8;
    record_t out;
    TEST_ASSERT_EQUAL(RECORD_BAD_VERSION, record_unpack(buf, &out));
}

/* ---- from_reading ------------------------------------------------------- */

static as7343_reading_t sample_reading(bool saturated)
{
    as7343_reading_t r;
    memset(&r, 0, sizeof r);
    r.gain = 6;
    r.atime = 29;
    r.astep = 599;
    r.saturated = saturated;
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        r.counts[i] = (uint16_t)(1000u + i);
    }
    return r;
}

static void test_from_reading_copies_measurement_fields(void)
{
    as7343_reading_t r = sample_reading(false);
    record_t rec;
    memset(&rec, 0xEE, sizeof rec);
    record_from_reading(&rec, &r, 42, 1700000000u, 0);

    TEST_ASSERT_EQUAL_UINT32(42, rec.seq);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, rec.timestamp);
    TEST_ASSERT_EQUAL_UINT8(6, rec.gain);
    TEST_ASSERT_EQUAL_UINT8(29, rec.atime);
    TEST_ASSERT_EQUAL_UINT16(599, rec.astep);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(r.counts, rec.counts, AS7343_DATA_SLOT_COUNT);
    TEST_ASSERT_EQUAL_HEX8(0, rec.flags);
}

static void test_from_reading_sets_saturated_flag_and_keeps_extra_flags(void)
{
    as7343_reading_t r = sample_reading(true);
    record_t rec;
    record_from_reading(&rec, &r, 1, 2, RECORD_FLAG_CHARGING);
    TEST_ASSERT_EQUAL_HEX8(RECORD_FLAG_SATURATED | RECORD_FLAG_CHARGING, rec.flags);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pack_header_bytes_at_fixed_offsets);
    RUN_TEST(test_pack_seq_and_timestamp_little_endian);
    RUN_TEST(test_pack_counts_little_endian_in_slot_order);
    RUN_TEST(test_pack_zeroes_reserved_and_spare);
    RUN_TEST(test_pack_crc_covers_bytes_0_to_61);
    RUN_TEST(test_unpack_round_trips_a_packed_record);
    RUN_TEST(test_unpack_rejects_a_corrupted_byte);
    RUN_TEST(test_unpack_detects_erased_flash);
    RUN_TEST(test_unpack_partially_written_record_is_bad_crc);
    RUN_TEST(test_unpack_rejects_unknown_version);
    RUN_TEST(test_from_reading_copies_measurement_fields);
    RUN_TEST(test_from_reading_sets_saturated_flag_and_keeps_extra_flags);
    return UNITY_END();
}
