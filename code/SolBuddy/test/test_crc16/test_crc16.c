/* Unit tests for services/crc16. Run on the PC: pio test -e native */
#include <unity.h>

#include "services/crc16.h"

void setUp(void) {}
void tearDown(void) {}

static void test_standard_check_value(void)
{
    /* Every CRC catalogue lists this check value for CRC-16/CCITT-FALSE. */
    const uint8_t msg[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(msg, sizeof msg));
}

static void test_empty_input_returns_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt(NULL, 0));
}

static void test_single_bit_change_changes_crc(void)
{
    uint8_t a[4] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t b[4] = { 0x10, 0x20, 0x31, 0x40 };
    TEST_ASSERT_NOT_EQUAL(crc16_ccitt(a, sizeof a), crc16_ccitt(b, sizeof b));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_check_value);
    RUN_TEST(test_empty_input_returns_initial_value);
    RUN_TEST(test_single_bit_change_changes_crc);
    return UNITY_END();
}
