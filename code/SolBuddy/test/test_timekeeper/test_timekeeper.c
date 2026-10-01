/* Unit tests for services/timekeeper. Run on the PC: pio test -e native */
#include <unity.h>

#include "services/timekeeper.h"

#define HZ          1024u
#define SECONDS(s)  ((uint32_t)((s) * HZ))
#define UTC_2025    1735689600u     /* 2025-01-01T00:00:00Z */

void setUp(void) {}
void tearDown(void) {}

/* ---- uptime ------------------------------------------------------------- */

static void test_uptime_starts_at_zero_whatever_the_counter_says(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 123456u);
    TEST_ASSERT_EQUAL_UINT32(0, timekeeper_uptime_s(&tk, 123456u));
}

static void test_uptime_counts_whole_seconds(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    TEST_ASSERT_EQUAL_UINT32(90, timekeeper_uptime_s(&tk, SECONDS(90)));
    TEST_ASSERT_EQUAL_UINT32(90, timekeeper_uptime_s(&tk, SECONDS(90) + HZ - 1));  /* 90.999 s */
}

static void test_uptime_survives_the_counter_wrapping(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0xFFFFFFFFu - SECONDS(10) + 1u);   /* 10 s before wrap */
    TEST_ASSERT_EQUAL_UINT32(25, timekeeper_uptime_s(&tk, SECONDS(15)));  /* 15 s after */
}

static void test_uptime_keeps_counting_past_many_wraps(void)
{
    /* 100 days, one call every 30 s like the sampler: the 32-bit counter
     * wraps twice (every ~48.5 days at 1024 Hz). */
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    uint32_t ticks = 0;
    for (uint32_t i = 0; i < 100u * 24u * 120u; i++) {
        ticks += SECONDS(30);
        timekeeper_uptime_s(&tk, ticks);
    }
    TEST_ASSERT_EQUAL_UINT32(100u * 24u * 3600u, timekeeper_uptime_s(&tk, ticks));
}

static void test_other_tick_rates_work(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, 32768u, 0);
    TEST_ASSERT_EQUAL_UINT32(7, timekeeper_uptime_s(&tk, 7u * 32768u));
}

/* ---- timestamps --------------------------------------------------------- */

static void test_timestamp_before_utc_is_uptime_and_not_utc(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    bool is_utc = true;
    TEST_ASSERT_EQUAL_UINT32(42, timekeeper_timestamp(&tk, SECONDS(42), &is_utc));
    TEST_ASSERT_FALSE(is_utc);
}

static void test_timestamp_after_set_utc_is_utc(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    timekeeper_set_utc(&tk, UTC_2025, SECONDS(100));
    bool is_utc = false;
    TEST_ASSERT_EQUAL_UINT32(UTC_2025 + 60u, timekeeper_timestamp(&tk, SECONDS(160), &is_utc));
    TEST_ASSERT_TRUE(is_utc);
}

static void test_utc_counts_from_the_exact_tick_it_was_set(void)
{
    /* Set half-way through a second: one full second later is +1, 0.9 s is +0. */
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    timekeeper_set_utc(&tk, UTC_2025, SECONDS(10) + HZ / 2u);
    bool is_utc;
    TEST_ASSERT_EQUAL_UINT32(UTC_2025, timekeeper_timestamp(&tk, SECONDS(11) + HZ / 2u - HZ / 10u, &is_utc));
    TEST_ASSERT_EQUAL_UINT32(UTC_2025 + 1u, timekeeper_timestamp(&tk, SECONDS(11) + HZ / 2u, &is_utc));
}

static void test_setting_utc_again_replaces_it(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0);
    timekeeper_set_utc(&tk, UTC_2025, SECONDS(10));
    timekeeper_set_utc(&tk, UTC_2025 + 3600u, SECONDS(20));   /* phone corrects by an hour */
    bool is_utc;
    TEST_ASSERT_EQUAL_UINT32(UTC_2025 + 3600u + 5u, timekeeper_timestamp(&tk, SECONDS(25), &is_utc));
}

static void test_utc_survives_the_counter_wrapping(void)
{
    timekeeper_t tk;
    timekeeper_init(&tk, HZ, 0xFFFFFFFFu - SECONDS(10) + 1u);
    timekeeper_set_utc(&tk, UTC_2025, 0xFFFFFFFFu - SECONDS(10) + 1u);
    bool is_utc;
    TEST_ASSERT_EQUAL_UINT32(UTC_2025 + 25u, timekeeper_timestamp(&tk, SECONDS(15), &is_utc));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uptime_starts_at_zero_whatever_the_counter_says);
    RUN_TEST(test_uptime_counts_whole_seconds);
    RUN_TEST(test_uptime_survives_the_counter_wrapping);
    RUN_TEST(test_uptime_keeps_counting_past_many_wraps);
    RUN_TEST(test_other_tick_rates_work);
    RUN_TEST(test_timestamp_before_utc_is_uptime_and_not_utc);
    RUN_TEST(test_timestamp_after_set_utc_is_utc);
    RUN_TEST(test_utc_counts_from_the_exact_tick_it_was_set);
    RUN_TEST(test_setting_utc_again_replaces_it);
    RUN_TEST(test_utc_survives_the_counter_wrapping);
    return UNITY_END();
}
