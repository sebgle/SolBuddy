/*
 * Unit tests for services/autorange. Run on the PC: pio test -e native
 *
 * Default exposure ATIME 29 / ASTEP 599 -> full scale (fs) = 18000.
 * Thresholds: clipped >= 17999, too bright > 14400 (80 %),
 * too dim < 3600 (20 %), target after a change = 9000 (50 %).
 */
#include <string.h>
#include <unity.h>

#include "services/autorange.h"

void setUp(void) {}
void tearDown(void) {}

static int is_fd_slot(uint32_t slot)
{
    return slot == AS7343_SLOT_FD_1 || slot == AS7343_SLOT_FD_2 || slot == AS7343_SLOT_FD_3;
}

/* Every non-FD slot = level, every FD slot = fd. */
static as7343_reading_t make_reading(uint8_t gain, uint16_t level, uint16_t fd, bool saturated)
{
    as7343_reading_t r;
    memset(&r, 0, sizeof r);
    r.gain      = gain;
    r.atime     = 29;
    r.astep     = 599;
    r.saturated = saturated;
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        r.counts[i] = is_fd_slot(i) ? fd : level;
    }
    return r;
}

static void assert_decision(autorange_action_t action, uint8_t next_gain, const as7343_reading_t *r)
{
    autorange_decision_t d = autorange_evaluate(r);
    TEST_ASSERT_EQUAL_MESSAGE(action, d.action, "action");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(next_gain, d.next_gain, "next_gain");
}

/* ---- in range ----------------------------------------------------------- */

static void test_in_range_reading_is_accepted_at_same_gain(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 9000, 9000, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

static void test_peak_is_the_brightest_non_fd_slot(void)
{
    /* Most slots are dim, but F4 is mid-scale: the reading is in range. */
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 1000, 1000, false);
    r.counts[AS7343_SLOT_F4] = 9000;
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

static void test_peak_at_80_percent_is_accepted(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 14400, 14400, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

static void test_peak_at_20_percent_is_accepted(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 3600, 3600, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

/* ---- clipped ------------------------------------------------------------ */

static void test_clipped_reading_drops_three_steps_and_retries(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_2048X, 17999, 18000, true);
    assert_decision(AUTORANGE_RETRY, AS7343_GAIN_256X, &r);
}

static void test_clipped_near_min_gain_clamps_to_zero(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_2X, 17999, 18000, true);
    assert_decision(AUTORANGE_RETRY, AS7343_GAIN_0_5X, &r);
}

static void test_clipped_at_min_gain_is_accepted(void)
{
    /* Nothing lower to try: log it, saturation flag and all. */
    as7343_reading_t r = make_reading(AS7343_GAIN_0_5X, 17999, 18000, true);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_0_5X, &r);
}

/* ---- too bright, not clipped -------------------------------------------- */

static void test_too_bright_steps_down_toward_half_scale_and_retries(void)
{
    /* 15000 -> one halving -> 7500 <= 9000. */
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 15000, 15000, false);
    assert_decision(AUTORANGE_RETRY, AS7343_GAIN_128X, &r);
}

static void test_unexplained_saturation_flag_backs_off_one_step(void)
{
    /* Flag set, but FD is not clipped: something else saturated (e.g. analog). */
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 5000, 5000, true);
    assert_decision(AUTORANGE_RETRY, AS7343_GAIN_128X, &r);
}

/* ---- too dim ------------------------------------------------------------ */

static void test_too_dim_raises_gain_for_next_sample(void)
{
    /* 1000 -> 2000 -> 4000 -> 8000 (next 16000 > 9000): three steps up. */
    as7343_reading_t r = make_reading(AS7343_GAIN_16X, 1000, 1000, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_128X, &r);
}

static void test_too_dim_gain_is_clamped_at_max(void)
{
    /* 45 would need seven steps up from 32x; clamp at 2048x. */
    as7343_reading_t r = make_reading(AS7343_GAIN_32X, 45, 45, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_2048X, &r);
}

static void test_zero_counts_go_to_max_gain(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_4X, 0, 0, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_2048X, &r);
}

/* ---- FD (flicker) channel ----------------------------------------------- */

static void test_fd_saturation_alone_does_not_back_off(void)
{
    /* Seen on hardware: FD clips and sets the flag; spectral channels fine. */
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 5000, 18000, true);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

static void test_bright_fd_is_ignored_when_judging_peak(void)
{
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 5000, 16000, false);
    assert_decision(AUTORANGE_ACCEPT, AS7343_GAIN_256X, &r);
}

/* ---- full scale --------------------------------------------------------- */

static void test_full_scale_is_capped_at_65535(void)
{
    /* (255+1) x (999+1) = 256000, but the ADC tops out at 65535.
     * 60000 is > 80 % of 65535, so too bright: 60000 -> 30000 <= 32767. */
    as7343_reading_t r = make_reading(AS7343_GAIN_256X, 60000, 60000, false);
    r.atime = 255;
    r.astep = 999;
    assert_decision(AUTORANGE_RETRY, AS7343_GAIN_128X, &r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_in_range_reading_is_accepted_at_same_gain);
    RUN_TEST(test_peak_is_the_brightest_non_fd_slot);
    RUN_TEST(test_peak_at_80_percent_is_accepted);
    RUN_TEST(test_peak_at_20_percent_is_accepted);
    RUN_TEST(test_clipped_reading_drops_three_steps_and_retries);
    RUN_TEST(test_clipped_near_min_gain_clamps_to_zero);
    RUN_TEST(test_clipped_at_min_gain_is_accepted);
    RUN_TEST(test_too_bright_steps_down_toward_half_scale_and_retries);
    RUN_TEST(test_unexplained_saturation_flag_backs_off_one_step);
    RUN_TEST(test_too_dim_raises_gain_for_next_sample);
    RUN_TEST(test_too_dim_gain_is_clamped_at_max);
    RUN_TEST(test_zero_counts_go_to_max_gain);
    RUN_TEST(test_fd_saturation_alone_does_not_back_off);
    RUN_TEST(test_bright_fd_is_ignored_when_judging_peak);
    RUN_TEST(test_full_scale_is_capped_at_65535);
    return UNITY_END();
}
