#include "app/storage.h"
#include "drivers/spi_nor/spi_nor.h"

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

#define HFXO_START_WAIT_MS  10u     /* HFXO typically starts in < 1 ms */

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

/* ---- errata [244]: hold HFXO during QSPI --------------------------------- */
/* QSPI data is corrupted if the HF clock switches between HFXO and HFINT
 * mid-transfer, which the SoftDevice does as the radio comes and goes.
 * Asking the SoftDevice to keep HFXO running for the whole session stops
 * the switching. Without the SoftDevice nothing switches the clock, so
 * there is nothing to do. */

static bool s_hfxo_requested;

static void hfxo_hold(void)
{
    uint8_t sd_enabled = 0;
    if (sd_softdevice_is_enabled(&sd_enabled) != NRF_SUCCESS || !sd_enabled) return;
    if (sd_clock_hfclk_request() != NRF_SUCCESS) return;
    s_hfxo_requested = true;

    for (uint32_t waited = 0; waited < HFXO_START_WAIT_MS; waited++) {
        uint32_t running = 0;
        sd_clock_hfclk_is_running(&running);
        if (running) return;
        sleep_ms(1);
    }
}

static void hfxo_release(void)
{
    if (s_hfxo_requested) {
        sd_clock_hfclk_release();
        s_hfxo_requested = false;
    }
}

/* ---- power: flash awake only inside a session ---------------------------- */

static bool session_begin(void)
{
    hfxo_hold();
    uint32_t size = 0;
    if (spi_nor_init(sleep_ms, &size) != SPI_NOR_OK) {
        spi_nor_uninit();
        hfxo_release();
        return false;
    }
    s_flash_size = size;
    return true;
}

static void session_end(void)
{
    spi_nor_power_down();   /* chip: 5 uA standby -> 0.007 uA */
    spi_nor_uninit();       /* QSPI peripheral off, errata [122] fix */
    hfxo_release();         /* HFXO costs ~ hundreds of uA if left on */
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

/* ---- lock: one flash session at a time ---------------------------------- */
/* Sampling (append) and BLE sync (read) run in different tasks. A session
 * powers the chip up and down, so two overlapping ones would turn the flash
 * off under each other. Every public call holds this mutex for its session. */

static SemaphoreHandle_t s_lock;

static bool lock(void)
{
    return s_lock != NULL && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* ---- public ------------------------------------------------------------- */

storage_result_t storage_init(void)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (!lock()) return STORAGE_ERR_FLASH;
    storage_result_t result = STORAGE_ERR_FLASH;
    if (!session_begin()) goto out;

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
    result = from_ringlog(r);
out:
    unlock();
    return result;
}

uint16_t storage_boot_id(void)
{
    return s_boot_id;
}

storage_result_t storage_append(record_t *rec)
{
    if (!lock()) return STORAGE_ERR_FLASH;
    storage_result_t result = STORAGE_ERR_FLASH;
    if (session_begin()) {
        result = from_ringlog(ringlog_append(&s_log, rec));
        session_end();
    }
    unlock();
    return result;
}

storage_result_t storage_read(uint32_t seq, record_t *rec)
{
    if (!lock()) return STORAGE_ERR_FLASH;
    storage_result_t result = STORAGE_ERR_FLASH;
    if (session_begin()) {
        result = from_ringlog(ringlog_read(&s_log, seq, rec));
        session_end();
    }
    unlock();
    return result;
}

storage_result_t storage_read_raw_batch(uint32_t *seq, uint8_t *out,
                                        uint32_t max_records, uint32_t *count)
{
    *count = 0;
    if (!lock()) return STORAGE_ERR_FLASH;
    storage_result_t result = STORAGE_ERR_FLASH;

    if (session_begin()) {
        result = STORAGE_OK;
        uint32_t oldest = ringlog_oldest_seq(&s_log);
        if (*seq < oldest) *seq = oldest;      /* asked for overwritten data */

        while (*count < max_records && *seq < s_log.next_seq) {
            ringlog_result_t r = ringlog_read_raw(&s_log, *seq, &out[*count * RECORD_SIZE]);
            if (r == RINGLOG_ERR_FLASH) { result = STORAGE_ERR_FLASH; break; }
            if (r == RINGLOG_OK) (*count)++;   /* LOST: skip, the app sees a gap */
            (*seq)++;
        }
        session_end();
    }
    unlock();
    return result;
}

uint32_t storage_oldest_seq(void)
{
    return ringlog_oldest_seq(&s_log);
}

uint32_t storage_next_seq(void)
{
    return s_log.next_seq;
}
