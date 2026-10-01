#include "hal/qspi.h"
#include "board/board_pins.h"

#include <string.h>
#include <nrf.h>

/* 32 MHz / (3 + 1) = 8 MHz. Slow on purpose: our SCK pin is not Nordic's
 * recommended one, and we only move 64 bytes every 30 s. */
#define QSPI_SCKFREQ        3u

/* Minimum CS-high time between commands, in 62.5 ns units.
 * MX25R needs 30 ns after a write/erase (tSHSL); 1 unit covers it. */
#define QSPI_SCKDELAY       1u

/* Safety net for READY never arriving; not a precise timeout. */
#define QSPI_POLL_LIMIT     2000000u

/* EasyDMA can only reach data RAM. */
#define RAM_START           0x20000000u
#define RAM_END             0x20040000u

static void configure_pin(uint32_t pin, uint32_t pull)
{
    NRF_GPIO_Type *port = (pin >> 5) ? NRF_P1 : NRF_P0;

    /* PS §6.19.1 step 2: QSPI pins need high drive. */
    port->PIN_CNF[pin & 0x1Fu] =
        (GPIO_PIN_CNF_DIR_Input      << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Connect  << GPIO_PIN_CNF_INPUT_Pos) |
        (pull                        << GPIO_PIN_CNF_PULL_Pos)  |
        (GPIO_PIN_CNF_DRIVE_H0H1     << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

static qspi_result_t wait_ready(void)
{
    for (uint32_t polls = 0; polls < QSPI_POLL_LIMIT; polls++) {
        if (NRF_QSPI->EVENTS_READY) {
            NRF_QSPI->EVENTS_READY = 0;
            return QSPI_OK;
        }
    }
    return QSPI_ERR_TIMEOUT;
}

/* EasyDMA rules (PS §6.19 registers): word-aligned flash address and RAM
 * buffer, length a multiple of 4, buffer in RAM. */
static bool dma_args_ok(uint32_t addr, const void *buf, size_t len)
{
    uintptr_t b = (uintptr_t)buf;
    if (len == 0u || len > 0x3FFFFu)            return false;
    if (((addr | b | len) & 3u) != 0u)          return false;
    return b >= RAM_START && b + len <= RAM_END;
}

qspi_result_t qspi_init(void)
{
    configure_pin(BOARD_QSPI_SCK, GPIO_PIN_CNF_PULL_Disabled);
    configure_pin(BOARD_QSPI_CS,  GPIO_PIN_CNF_PULL_Disabled);
    configure_pin(BOARD_QSPI_IO0, GPIO_PIN_CNF_PULL_Disabled);
    /* IO1 is the flash's data-out. ACTIVATE polls the flash's busy bit, but
     * our flash is in deep power-down then and drives nothing: a floating
     * IO1 read as "busy" stalled ACTIVATE for 50-280 ms at random (seen on
     * hardware). The pull-down makes an undriven IO1 read 0 = not busy. */
    configure_pin(BOARD_QSPI_IO1, GPIO_PIN_CNF_PULL_Pulldown);
    configure_pin(BOARD_QSPI_IO2, GPIO_PIN_CNF_PULL_Disabled);
    configure_pin(BOARD_QSPI_IO3, GPIO_PIN_CNF_PULL_Disabled);

    NRF_QSPI->ENABLE   = QSPI_ENABLE_ENABLE_Disabled << QSPI_ENABLE_ENABLE_Pos;
    NRF_QSPI->PSEL.SCK = BOARD_QSPI_SCK;
    NRF_QSPI->PSEL.CSN = BOARD_QSPI_CS;
    NRF_QSPI->PSEL.IO0 = BOARD_QSPI_IO0;
    NRF_QSPI->PSEL.IO1 = BOARD_QSPI_IO1;
    NRF_QSPI->PSEL.IO2 = BOARD_QSPI_IO2;
    NRF_QSPI->PSEL.IO3 = BOARD_QSPI_IO3;

    /* Single data line for everything: no quad-enable bit to set, and the
     * same commands work on both the MX25R and the Feather's GD25Q16C.
     * The peripheral's own deep power-down (DPMENABLE) is NOT used: while
     * the flash is in DPM it holds STATUS.READY = BUSY and never raises
     * READY (seen on hardware). The driver sends B9/AB itself instead. */
    NRF_QSPI->IFCONFIG0 =
        (QSPI_IFCONFIG0_READOC_FASTREAD  << QSPI_IFCONFIG0_READOC_Pos)   |
        (QSPI_IFCONFIG0_WRITEOC_PP       << QSPI_IFCONFIG0_WRITEOC_Pos)  |
        (QSPI_IFCONFIG0_ADDRMODE_24BIT   << QSPI_IFCONFIG0_ADDRMODE_Pos) |
        (QSPI_IFCONFIG0_DPMENABLE_Disable << QSPI_IFCONFIG0_DPMENABLE_Pos) |
        (QSPI_IFCONFIG0_PPSIZE_256Bytes  << QSPI_IFCONFIG0_PPSIZE_Pos);

    NRF_QSPI->IFCONFIG1 =
        (QSPI_SCKDELAY                  << QSPI_IFCONFIG1_SCKDELAY_Pos) |
        (QSPI_IFCONFIG1_DPMEN_Exit      << QSPI_IFCONFIG1_DPMEN_Pos)    |
        (QSPI_IFCONFIG1_SPIMODE_MODE0   << QSPI_IFCONFIG1_SPIMODE_Pos)  |
        (QSPI_SCKFREQ                   << QSPI_IFCONFIG1_SCKFREQ_Pos);

    NRF_QSPI->EVENTS_READY = 0;
    NRF_QSPI->ENABLE = QSPI_ENABLE_ENABLE_Enabled << QSPI_ENABLE_ENABLE_Pos;
    NRF_QSPI->TASKS_ACTIVATE = 1;
    return wait_ready();
}

void qspi_uninit(void)
{
    NRF_QSPI->TASKS_DEACTIVATE = 1;

    /* Errata [122]: without these writes QSPI keeps drawing current after
     * being disabled. Must run before ENABLE = 0. Undocumented registers. */
    *(volatile uint32_t *)0x40029010ul = 1ul;
    *(volatile uint32_t *)0x40029054ul = 1ul;

    NRF_QSPI->ENABLE = QSPI_ENABLE_ENABLE_Disabled << QSPI_ENABLE_ENABLE_Pos;
}

qspi_result_t qspi_cinstr(uint8_t opcode, const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len, bool wren)
{
    if (tx_len + rx_len > 8u)        return QSPI_ERR_ARG;
    if (tx_len > 0u && tx == NULL)   return QSPI_ERR_ARG;
    if (rx_len > 0u && rx == NULL)   return QSPI_ERR_ARG;

    /* Bytes after the opcode: first what we send, then placeholders for
     * what we receive. The peripheral overwrites them with the response. */
    uint8_t data[8] = { 0 };
    if (tx_len > 0u) memcpy(data, tx, tx_len);

    NRF_QSPI->CINSTRDAT0 = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    NRF_QSPI->CINSTRDAT1 = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                           ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);

    NRF_QSPI->EVENTS_READY = 0;

    /* Writing CINSTRCONF sends the instruction. IO2 (WP#) and IO3 (HOLD#)
     * are held high: a low HOLD# makes the flash ignore everything. */
    NRF_QSPI->CINSTRCONF =
        ((uint32_t)opcode                     << QSPI_CINSTRCONF_OPCODE_Pos) |
        ((uint32_t)(1u + tx_len + rx_len)     << QSPI_CINSTRCONF_LENGTH_Pos) |
        (1u                                   << QSPI_CINSTRCONF_LIO2_Pos)   |
        (1u                                   << QSPI_CINSTRCONF_LIO3_Pos)   |
        ((wren ? 1u : 0u)                     << QSPI_CINSTRCONF_WREN_Pos);

    qspi_result_t r = wait_ready();
    if (r != QSPI_OK) return r;

    uint32_t d0 = NRF_QSPI->CINSTRDAT0;
    uint32_t d1 = NRF_QSPI->CINSTRDAT1;
    for (uint32_t i = 0; i < 4u; i++) {
        data[i]      = (uint8_t)(d0 >> (8u * i));
        data[4u + i] = (uint8_t)(d1 >> (8u * i));
    }
    if (rx_len > 0u) memcpy(rx, &data[tx_len], rx_len);
    return QSPI_OK;
}

qspi_result_t qspi_read(uint32_t addr, void *buf, size_t len)
{
    if (!dma_args_ok(addr, buf, len)) return QSPI_ERR_ARG;

    NRF_QSPI->READ.SRC = addr;
    NRF_QSPI->READ.DST = (uint32_t)buf;
    NRF_QSPI->READ.CNT = len;
    NRF_QSPI->EVENTS_READY = 0;
    NRF_QSPI->TASKS_READSTART = 1;
    return wait_ready();
}

qspi_result_t qspi_write(uint32_t addr, const void *buf, size_t len)
{
    if (!dma_args_ok(addr, buf, len)) return QSPI_ERR_ARG;

    NRF_QSPI->WRITE.DST = addr;
    NRF_QSPI->WRITE.SRC = (uint32_t)buf;
    NRF_QSPI->WRITE.CNT = len;
    NRF_QSPI->EVENTS_READY = 0;
    NRF_QSPI->TASKS_WRITESTART = 1;
    return wait_ready();
}

qspi_result_t qspi_erase_4k(uint32_t addr)
{
    if ((addr & 0xFFFu) != 0u) return QSPI_ERR_ARG;

    NRF_QSPI->ERASE.PTR = addr;
    NRF_QSPI->ERASE.LEN = QSPI_ERASE_LEN_LEN_4KB << QSPI_ERASE_LEN_LEN_Pos;
    NRF_QSPI->EVENTS_READY = 0;
    NRF_QSPI->TASKS_ERASESTART = 1;
    return wait_ready();   /* erase STARTED, not finished (PS §6.19.4) */
}
