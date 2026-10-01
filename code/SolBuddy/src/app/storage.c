#include "app/storage.h"
#include "drivers/spi_nor/spi_nor.h"

#include <FreeRTOS.h>
#include <task.h>

#define RESERVED_SECTORS    1u      /* sector 0: calibration / metadata */

static ringlog_t s_log;
static uint32_t  s_flash_size;
static uint16_t  s_boot_id;

static void sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ---- ringlog flash interface over spi_nor (0 = success) ----------------- */

static int flash_read(uint32_t addr, uint8_t *buf, size_t len)
{
    return spi_nor_read(addr, buf, len) == SPI_NOR_OK ? 0 : -1;
}

static int flash_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    return spi_nor_program(addr, buf, len) == SPI_NOR_OK ? 0 : -1;
}

static int flash_erase_sector(uint32_t addr)
{
    return spi_nor_erase_sector(addr) == SPI_NOR_OK ? 0 : -1;
}

static const ringlog_flash_t FLASH_OPS = { flash_read, flash_program, flash_erase_sector };

/* ---- power: flash awake only inside a session ---------------------------- */

static bool session_begin(void)
{
    uint32_t size = 0;
    if (spi_nor_init(sleep_ms, &size) != SPI_NOR_OK) {
        spi_nor_uninit();
        return false;
    }
    s_flash_size = size;
    return true;
}

static void session_end(void)
{
    spi_nor_power_down();   /* chip: 5 uA standby -> 0.007 uA */
    spi_nor_uninit();       /* QSPI peripheral off, errata [122] fix */
}

static storage_result_t from_ringlog(ringlog_result_t r)
{
    switch (r) {
        case RINGLOG_OK:            return STORAGE_OK;
        case RINGLOG_NOT_AVAILABLE: return STORAGE_NOT_AVAILABLE;
        case RINGLOG_LOST:          return STORAGE_LOST;
        default:                    return STORAGE_ERR_FLASH;
    }
}

/* ---- public ------------------------------------------------------------- */

storage_result_t storage_init(void)
{
    if (!session_begin()) return STORAGE_ERR_FLASH;

    uint32_t sectors = s_flash_size / RINGLOG_SECTOR_SIZE - RESERVED_SECTORS;
    ringlog_result_t r = ringlog_mount(&s_log, &FLASH_OPS,
                                       RESERVED_SECTORS * RINGLOG_SECTOR_SIZE, sectors);

    /* This boot's number: one more than the newest readable record's.
     * Walk back past damaged records (at most one sector's worth, so boot
     * time stays bounded); an empty log starts at boot 0. */
    s_boot_id = 0;
    if (r == RINGLOG_OK) {
        uint32_t oldest = ringlog_oldest_seq(&s_log);
        uint32_t limit  = RINGLOG_RECORDS_PER_SECTOR;
        for (uint32_t seq = s_log.next_seq; seq > oldest && limit > 0u; seq--, limit--) {
            record_t newest;
            if (ringlog_read(&s_log, seq - 1u, &newest) == RINGLOG_OK) {
                s_boot_id = (uint16_t)(newest.boot_id + 1u);
                break;
            }
        }
    }

    session_end();
    return from_ringlog(r);
}

uint16_t storage_boot_id(void)
{
    return s_boot_id;
}

storage_result_t storage_append(record_t *rec)
{
    if (!session_begin()) return STORAGE_ERR_FLASH;
    ringlog_result_t r = ringlog_append(&s_log, rec);
    session_end();
    return from_ringlog(r);
}

storage_result_t storage_read(uint32_t seq, record_t *rec)
{
    if (!session_begin()) return STORAGE_ERR_FLASH;
    ringlog_result_t r = ringlog_read(&s_log, seq, rec);
    session_end();
    return from_ringlog(r);
}

uint32_t storage_oldest_seq(void)
{
    return ringlog_oldest_seq(&s_log);
}

uint32_t storage_next_seq(void)
{
    return s_log.next_seq;
}
