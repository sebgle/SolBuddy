/* Unit tests for services/ringlog. Run on the PC: pio test -e native */
#include <string.h>
#include <unity.h>

#include "services/ringlog.h"

/* ---- Fake NOR flash in RAM ---------------------------------------------- */
/* 2 guard sectors, then a 4-sector log (256 records). Behaves like NOR:
 * program can only clear bits (1 -> 0), erase sets a sector to 0xFF,
 * program may not cross a 256-byte page. */

#define PAGE_SIZE       256u
#define GUARD_SECTORS   2u
#define LOG_SECTORS     4u
#define LOG_BASE        (GUARD_SECTORS * RINGLOG_SECTOR_SIZE)
#define FLASH_SIZE      ((GUARD_SECTORS + LOG_SECTORS) * RINGLOG_SECTOR_SIZE)
#define CAPACITY        (LOG_SECTORS * RINGLOG_RECORDS_PER_SECTOR)   /* 256 */

static uint8_t  mem[FLASH_SIZE];
static int      erase_count;
static int      program_should_fail;
static int      partial_bytes;      /* >= 0: next program stops after this many bytes */
static int      page_violation;

static int fake_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (addr + len > FLASH_SIZE) return -1;
    memcpy(buf, &mem[addr], len);
    return 0;
}

static int fake_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (program_should_fail) return -1;
    if (addr + len > FLASH_SIZE) return -1;
    if (addr / PAGE_SIZE != (addr + len - 1) / PAGE_SIZE) { page_violation = 1; return -1; }

    size_t n = len;
    if (partial_bytes >= 0) { n = (size_t)partial_bytes; partial_bytes = -1; }
    for (size_t i = 0; i < n; i++) mem[addr + i] &= buf[i];   /* NOR: only 1 -> 0 */
    return 0;
}

static int fake_erase(uint32_t addr)
{
    if (addr % RINGLOG_SECTOR_SIZE != 0 || addr >= FLASH_SIZE) return -1;
    memset(&mem[addr], 0xFF, RINGLOG_SECTOR_SIZE);
    erase_count++;
    return 0;
}

static const ringlog_flash_t FAKE = { fake_read, fake_program, fake_erase };

void setUp(void)
{
    memset(mem, 0xFF, sizeof mem);
    erase_count = 0;
    program_should_fail = 0;
    partial_bytes = -1;
    page_violation = 0;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE_MESSAGE(page_violation, "program crossed a page boundary");
}

/* ---- Helpers ------------------------------------------------------------ */

static record_t make_record(uint32_t timestamp)
{
    record_t rec;
    memset(&rec, 0, sizeof rec);
    rec.timestamp = timestamp;
    rec.gain = 9;
    rec.atime = 29;
    rec.astep = 599;
    for (uint32_t i = 0; i < AS7343_DATA_SLOT_COUNT; i++) {
        rec.counts[i] = (uint16_t)(timestamp + i);
    }
    return rec;
}

static ringlog_t mount_ok(void)
{
    ringlog_t log;
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_mount(&log, &FAKE, LOG_BASE, LOG_SECTORS));
    return log;
}

static void append_n(ringlog_t *log, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        record_t rec = make_record(1000u + log->next_seq);
        TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_append(log, &rec));
    }
}

static uint32_t slot_addr(uint32_t seq)
{
    return LOG_BASE + (seq % CAPACITY) * RECORD_SIZE;
}

/* ---- Mount / empty ------------------------------------------------------ */

static void test_mount_on_erased_flash_is_empty(void)
{
    ringlog_t log = mount_ok();
    TEST_ASSERT_EQUAL_UINT32(0, log.next_seq);
    TEST_ASSERT_EQUAL_UINT32(0, ringlog_oldest_seq(&log));
}

static void test_mount_rejects_bad_arguments(void)
{
    ringlog_t log;
    TEST_ASSERT_EQUAL(RINGLOG_ERR_ARG, ringlog_mount(&log, &FAKE, LOG_BASE + 1, LOG_SECTORS));
    TEST_ASSERT_EQUAL(RINGLOG_ERR_ARG, ringlog_mount(&log, &FAKE, LOG_BASE, 1));
}

/* ---- Append / read ------------------------------------------------------ */

static void test_append_assigns_consecutive_seqs(void)
{
    ringlog_t log = mount_ok();
    for (uint32_t i = 0; i < 3; i++) {
        record_t rec = make_record(5);
        rec.seq = 999;   /* caller's value must be ignored */
        TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_append(&log, &rec));
        TEST_ASSERT_EQUAL_UINT32(i, rec.seq);
    }
    TEST_ASSERT_EQUAL_UINT32(3, log.next_seq);
}

static void test_read_returns_the_appended_record(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 5);
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&log, 3, &out));
    TEST_ASSERT_EQUAL_UINT32(3, out.seq);
    TEST_ASSERT_EQUAL_UINT32(1003, out.timestamp);
    TEST_ASSERT_EQUAL_UINT16(1003 + 7, out.counts[7]);
}

static void test_record_is_stored_at_seq_mod_capacity(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 3);
    uint8_t raw[RECORD_SIZE];
    record_t rec;
    memcpy(raw, &mem[slot_addr(2)], RECORD_SIZE);
    TEST_ASSERT_EQUAL(RECORD_OK, record_unpack(raw, &rec));
    TEST_ASSERT_EQUAL_UINT32(2, rec.seq);
}

static void test_first_write_in_each_sector_erases_it(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 1);
    TEST_ASSERT_EQUAL(1, erase_count);
    append_n(&log, RINGLOG_RECORDS_PER_SECTOR - 1);   /* rest of sector 0 */
    TEST_ASSERT_EQUAL(1, erase_count);
    append_n(&log, 1);                                 /* first of sector 1 */
    TEST_ASSERT_EQUAL(2, erase_count);
}

static void test_guard_sectors_before_base_are_untouched(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, CAPACITY + 10);   /* wrap fully */
    for (uint32_t i = 0; i < LOG_BASE; i++) {
        if (mem[i] != 0xFF) TEST_FAIL_MESSAGE("wrote outside the log region");
    }
}

static void test_read_of_unwritten_seq_is_not_available(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 5);
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_NOT_AVAILABLE, ringlog_read(&log, 5, &out));
}

static void test_flash_error_leaves_next_seq_unchanged(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 2);
    program_should_fail = 1;
    record_t rec = make_record(1);
    TEST_ASSERT_EQUAL(RINGLOG_ERR_FLASH, ringlog_append(&log, &rec));
    TEST_ASSERT_EQUAL_UINT32(2, log.next_seq);
}

/* ---- Wrap-around -------------------------------------------------------- */

static void test_full_ring_keeps_everything_until_next_sector_is_needed(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, CAPACITY);          /* exactly full; sector 0 not reused yet */
    TEST_ASSERT_EQUAL_UINT32(0, ringlog_oldest_seq(&log));
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&log, 0, &out));
}

static void test_wrap_overwrites_the_oldest_sector(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, CAPACITY + 10);     /* reused sector 0: seqs 0-63 gone */
    TEST_ASSERT_EQUAL_UINT32(64, ringlog_oldest_seq(&log));
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_NOT_AVAILABLE, ringlog_read(&log, 63, &out));
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&log, 64, &out));
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&log, CAPACITY + 9, &out));
    TEST_ASSERT_EQUAL_UINT32(CAPACITY + 9, out.seq);
}

/* ---- Remount (reboot) --------------------------------------------------- */

static void test_remount_continues_where_it_left_off(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 100);
    ringlog_t again = mount_ok();
    TEST_ASSERT_EQUAL_UINT32(100, again.next_seq);
    TEST_ASSERT_EQUAL_UINT32(0, ringlog_oldest_seq(&again));
}

static void test_remount_after_wrap_finds_newest_and_oldest(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 2 * CAPACITY + 70);
    ringlog_t again = mount_ok();
    TEST_ASSERT_EQUAL_UINT32(2 * CAPACITY + 70, again.next_seq);
    TEST_ASSERT_EQUAL_UINT32(log.next_seq, again.next_seq);
    TEST_ASSERT_EQUAL_UINT32(ringlog_oldest_seq(&log), ringlog_oldest_seq(&again));
}

static void test_remount_at_exact_sector_boundary(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, CAPACITY + RINGLOG_RECORDS_PER_SECTOR);   /* sector 1 full of old data */
    ringlog_t again = mount_ok();
    TEST_ASSERT_EQUAL_UINT32(CAPACITY + RINGLOG_RECORDS_PER_SECTOR, again.next_seq);
    TEST_ASSERT_EQUAL_UINT32(RINGLOG_RECORDS_PER_SECTOR, ringlog_oldest_seq(&again));
}

/* ---- Power loss --------------------------------------------------------- */

static void test_half_written_record_is_skipped_and_reported_lost(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, 10);
    partial_bytes = 32;                       /* power dies mid-write of seq 10 */
    record_t rec = make_record(1);
    ringlog_append(&log, &rec);

    ringlog_t again = mount_ok();             /* reboot */
    TEST_ASSERT_EQUAL_UINT32(11, again.next_seq);
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_LOST, ringlog_read(&again, 10, &out));
    append_n(&again, 1);
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&again, 11, &out));
    TEST_ASSERT_EQUAL_UINT32(11, out.seq);
}

static void test_damaged_first_record_of_sector_is_rewritten(void)
{
    ringlog_t log = mount_ok();
    append_n(&log, RINGLOG_RECORDS_PER_SECTOR);   /* sector 0 full */
    partial_bytes = 20;                           /* first write in sector 1 dies */
    record_t rec = make_record(1);
    ringlog_append(&log, &rec);

    ringlog_t again = mount_ok();
    TEST_ASSERT_EQUAL_UINT32(RINGLOG_RECORDS_PER_SECTOR, again.next_seq);
    append_n(&again, 1);                          /* must re-erase sector 1 first */
    record_t out;
    TEST_ASSERT_EQUAL(RINGLOG_OK, ringlog_read(&again, RINGLOG_RECORDS_PER_SECTOR, &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_on_erased_flash_is_empty);
    RUN_TEST(test_mount_rejects_bad_arguments);
    RUN_TEST(test_append_assigns_consecutive_seqs);
    RUN_TEST(test_read_returns_the_appended_record);
    RUN_TEST(test_record_is_stored_at_seq_mod_capacity);
    RUN_TEST(test_first_write_in_each_sector_erases_it);
    RUN_TEST(test_guard_sectors_before_base_are_untouched);
    RUN_TEST(test_read_of_unwritten_seq_is_not_available);
    RUN_TEST(test_flash_error_leaves_next_seq_unchanged);
    RUN_TEST(test_full_ring_keeps_everything_until_next_sector_is_needed);
    RUN_TEST(test_wrap_overwrites_the_oldest_sector);
    RUN_TEST(test_remount_continues_where_it_left_off);
    RUN_TEST(test_remount_after_wrap_finds_newest_and_oldest);
    RUN_TEST(test_remount_at_exact_sector_boundary);
    RUN_TEST(test_half_written_record_is_skipped_and_reported_lost);
    RUN_TEST(test_damaged_first_record_of_sector_is_rewritten);
    return UNITY_END();
}
