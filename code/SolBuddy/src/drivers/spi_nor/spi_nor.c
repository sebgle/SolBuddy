#include "drivers/spi_nor/spi_nor.h"
#include "hal/qspi.h"

#include <string.h>
#include <stdbool.h>

/* Standard SPI NOR opcodes (identical on MX25R6435F and GD25Q16C). */
#define CMD_RDSR    0x05u   /* read status register */
#define CMD_RDID    0x9Fu   /* read JEDEC ID */
#define CMD_DP      0xB9u   /* enter deep power-down */
#define CMD_RDP     0xABu   /* release from deep power-down */

#define SR_WIP      0x01u   /* status bit 0: write/erase in progress */

/* Timings: the stricter of the two chips' datasheet maxima, plus margin.
 * MX25R6435F (ULP mode): tDP 10 us, tRDP 35 us, tPP 10 ms, tSE 240 ms.
 * GD25Q16C:              tDP 20 us, tRES1 20 us, tPP 2.4 ms, tSE 300 ms. */
/* Deep power-down entry (20 us) and exit (35 us) are covered by sleeping
 * 1 ms, the smallest step the caller's sleep function offers. */
#define DPM_SETTLE_MS       1u
#define PROGRAM_TIMEOUT_MS  20u
#define ERASE_TIMEOUT_MS    400u

typedef struct {
    uint8_t  id[3];
    uint32_t size_bytes;
} chip_t;

static const chip_t SUPPORTED[] = {
    { { 0xC2, 0x28, 0x17 }, 8u * 1024u * 1024u },   /* MX25R6435F */
    { { 0xC8, 0x40, 0x15 }, 2u * 1024u * 1024u },   /* GD25Q16C */
};

static spi_nor_sleep_ms_fn s_sleep_ms;

/* EasyDMA needs a word-aligned RAM buffer; callers' buffers may not be.
 * uint32_t storage guarantees 4-byte alignment. One page is enough. */
static uint32_t s_bounce[SPI_NOR_PAGE_SIZE / 4u];

static spi_nor_result_t from_qspi(qspi_result_t r)
{
    return (r == QSPI_OK) ? SPI_NOR_OK : SPI_NOR_ERR_BUS;
}

static spi_nor_result_t wait_while_busy(uint32_t timeout_ms)
{
    for (uint32_t waited = 0; ; waited++) {
        uint8_t status = 0;
        if (qspi_cinstr(CMD_RDSR, NULL, 0, &status, 1, false) != QSPI_OK) return SPI_NOR_ERR_BUS;
        if ((status & SR_WIP) == 0u) return SPI_NOR_OK;
        if (waited >= timeout_ms)    return SPI_NOR_ERR_TIMEOUT;
        s_sleep_ms(1);
    }
}

spi_nor_result_t spi_nor_init(spi_nor_sleep_ms_fn sleep_ms, uint32_t *size_bytes)
{
    if (sleep_ms == NULL || size_bytes == NULL) return SPI_NOR_ERR_ARG;
    s_sleep_ms = sleep_ms;

    spi_nor_result_t r = from_qspi(qspi_init());
    if (r != SPI_NOR_OK) return r;

    /* The chip survives an MCU reset in whatever state it was in. Wake it
     * (harmless if awake), then let any interrupted program/erase finish. */
    r = spi_nor_wake();
    if (r != SPI_NOR_OK) return r;
    r = wait_while_busy(ERASE_TIMEOUT_MS);
    if (r != SPI_NOR_OK) return r;

    uint8_t id[3] = { 0, 0, 0 };
    r = from_qspi(qspi_cinstr(CMD_RDID, NULL, 0, id, sizeof id, false));
    if (r != SPI_NOR_OK) return r;

    for (size_t i = 0; i < sizeof SUPPORTED / sizeof SUPPORTED[0]; i++) {
        if (memcmp(id, SUPPORTED[i].id, sizeof id) == 0) {
            *size_bytes = SUPPORTED[i].size_bytes;
            return SPI_NOR_OK;
        }
    }
    return SPI_NOR_ERR_UNKNOWN_CHIP;
}

void spi_nor_uninit(void)
{
    qspi_uninit();
}

spi_nor_result_t spi_nor_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (((addr | len) & 3u) != 0u || buf == NULL) return SPI_NOR_ERR_ARG;

    while (len > 0u) {
        size_t chunk = (len < sizeof s_bounce) ? len : sizeof s_bounce;
        spi_nor_result_t r = from_qspi(qspi_read(addr, s_bounce, chunk));
        if (r != SPI_NOR_OK) return r;
        memcpy(buf, s_bounce, chunk);
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
    return SPI_NOR_OK;
}

spi_nor_result_t spi_nor_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (((addr | len) & 3u) != 0u || buf == NULL || len == 0u) return SPI_NOR_ERR_ARG;
    /* A page program that runs past the page end wraps to its start. */
    if (addr / SPI_NOR_PAGE_SIZE != (addr + len - 1u) / SPI_NOR_PAGE_SIZE) return SPI_NOR_ERR_ARG;

    memcpy(s_bounce, buf, len);
    spi_nor_result_t r = from_qspi(qspi_write(addr, s_bounce, len));
    if (r != SPI_NOR_OK) return r;
    return wait_while_busy(PROGRAM_TIMEOUT_MS);
}

spi_nor_result_t spi_nor_erase_sector(uint32_t addr)
{
    if ((addr % SPI_NOR_SECTOR_SIZE) != 0u) return SPI_NOR_ERR_ARG;

    spi_nor_result_t r = from_qspi(qspi_erase_4k(addr));
    if (r != SPI_NOR_OK) return r;
    return wait_while_busy(ERASE_TIMEOUT_MS);
}

/* Sent as plain commands rather than through the QSPI peripheral's DPM
 * feature, which leaves the peripheral BUSY while the flash sleeps. */
spi_nor_result_t spi_nor_power_down(void)
{
    spi_nor_result_t r = from_qspi(qspi_cinstr(CMD_DP, NULL, 0, NULL, 0, false));
    if (r == SPI_NOR_OK) s_sleep_ms(DPM_SETTLE_MS);
    return r;
}

spi_nor_result_t spi_nor_wake(void)
{
    spi_nor_result_t r = from_qspi(qspi_cinstr(CMD_RDP, NULL, 0, NULL, 0, false));
    if (r == SPI_NOR_OK) s_sleep_ms(DPM_SETTLE_MS);
    return r;
}
