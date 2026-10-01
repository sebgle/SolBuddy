#include "hal/i2c.h"
#include "board/board_pins.h"

#include <stdbool.h>
#include <nrf.h>

/* TWIM0 is ours: the Arduino core's Wire object also points at it,
 * but never touches the hardware unless Wire.begin() is called. */
#define I2C_TWIM            NRF_TWIM0

/* Safety net for a stuck bus, not a precise timeout. A 37-byte read at
 * 400 kHz takes ~1 ms; this many polls is far longer than that. */
#define I2C_POLL_LIMIT      200000u

/* 390 kHz instead of 400 kHz. Errata [219]: at 400 kHz the nRF52840's SCL
 * low period is 1.25 us, but the AS7343 requires >= 1.3 us (datasheet tLOW).
 * Nordic's workaround value; the MDK header has no named constant for it. */
#define I2C_FREQUENCY_K390  0x06200000u

/* EasyDMA can only read/write data RAM (256 KB starting at 0x20000000). */
#define RAM_START           0x20000000u
#define RAM_END             0x20040000u

static bool in_ram(const void *p, size_t len)
{
    uintptr_t start = (uintptr_t)p;
    return start >= RAM_START && start + len <= RAM_END;
}

static void configure_bus_pin(uint32_t pin)
{
    NRF_GPIO_Type *port = (pin >> 5) ? NRF_P1 : NRF_P0;

    port->PIN_CNF[pin & 0x1Fu] =
        (GPIO_PIN_CNF_DIR_Input      << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Connect  << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled  << GPIO_PIN_CNF_PULL_Pos)  |
        (GPIO_PIN_CNF_DRIVE_S0D1     << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

void i2c_init(void)
{
    configure_bus_pin(BOARD_I2C_SCL);
    configure_bus_pin(BOARD_I2C_SDA);

    I2C_TWIM->ENABLE    = TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos;
    I2C_TWIM->PSEL.SCL  = BOARD_I2C_SCL;
    I2C_TWIM->PSEL.SDA  = BOARD_I2C_SDA;
    I2C_TWIM->FREQUENCY = I2C_FREQUENCY_K390;
}

static i2c_result_t transfer(uint8_t addr,
                             const uint8_t *tx, size_t tx_len,
                             uint8_t *rx, size_t rx_len)
{
    NRF_TWIM_Type *twim = I2C_TWIM;

    if (addr > 0x7Fu) return I2C_ERR_ARG;
    if (tx == NULL || tx_len == 0 || tx_len > 0xFFFFu || !in_ram(tx, tx_len)) return I2C_ERR_ARG;
    if (rx_len > 0 && (rx == NULL || rx_len > 0xFFFFu || !in_ram(rx, rx_len))) return I2C_ERR_ARG;

    twim->ADDRESS    = addr;
    twim->TXD.PTR    = (uint32_t)tx;
    twim->TXD.MAXCNT = tx_len;
    twim->RXD.PTR    = (uint32_t)rx;
    twim->RXD.MAXCNT = rx_len;
    twim->SHORTS     = rx_len
                     ? (TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk)
                     : TWIM_SHORTS_LASTTX_STOP_Msk;

    twim->EVENTS_STOPPED = 0;
    twim->EVENTS_ERROR   = 0;
    twim->ERRORSRC       = twim->ERRORSRC;   /* write-1-to-clear */

    twim->ENABLE = TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos;
    twim->TASKS_STARTTX = 1;

    bool timed_out = false;
    uint32_t polls = 0;
    while (!twim->EVENTS_STOPPED) {
        if (twim->EVENTS_ERROR) {
            /* A NACK does not end the transfer by itself; we must stop it.
             * RESUME first, as Nordic's reference driver does, in case the
             * controller suspended itself. */
            twim->EVENTS_ERROR = 0;
            twim->TASKS_RESUME = 1;
            twim->TASKS_STOP   = 1;
        }
        if (++polls > I2C_POLL_LIMIT) {
            twim->TASKS_STOP = 1;
            timed_out = true;
            break;
        }
    }

    uint32_t errors  = twim->ERRORSRC;
    uint32_t tx_done = twim->TXD.AMOUNT;
    uint32_t rx_done = twim->RXD.AMOUNT;

    twim->ERRORSRC       = errors;
    twim->EVENTS_STOPPED = 0;
    twim->SHORTS         = 0;
    twim->ENABLE = TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos;

    if (timed_out)                         return I2C_ERR_TIMEOUT;
    if (errors & TWIM_ERRORSRC_ANACK_Msk)  return I2C_ERR_ADDR_NACK;
    if (errors & TWIM_ERRORSRC_DNACK_Msk)  return I2C_ERR_DATA_NACK;
    if (errors)                            return I2C_ERR_BUS;
    /* RXD.AMOUNT is not reset by a write-only transfer; it keeps the count
     * from the last read. Only check it when this transfer received. */
    if (tx_done != tx_len)                     return I2C_ERR_SHORT;
    if (rx_len > 0 && rx_done != rx_len)       return I2C_ERR_SHORT;
    return I2C_OK;
}

i2c_result_t i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return transfer(addr, data, len, NULL, 0);
}

i2c_result_t i2c_write_read(uint8_t addr,
                            const uint8_t *tx, size_t tx_len,
                            uint8_t *rx, size_t rx_len)
{
    if (rx_len == 0) return I2C_ERR_ARG;
    return transfer(addr, tx, tx_len, rx, rx_len);
}
