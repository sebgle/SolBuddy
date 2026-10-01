# Firmware notes

Known limitations and verified hardware details, per module. Update this when
a limitation is fixed or a new one is accepted.

Reference documents (in `docs/datasheets/`):
- AS7343 datasheet DS001046 v6-00
- nRF52840 Product Specification v1.11 (file currently named `NRF52840-DK.pdf`)
- nRF52840 Rev 3 Errata v1.4

---

## hal/i2c — I2C bus layer (TWIM0, polled)

Status: verified on Feather nRF52840 Express + Adafruit AS7343 breakout
(2026-10-01) — bank select, ID read (0x81), bank restore all OK, repeatedly.

### Known limitations

1. **Single user only.** `transfer()` has no lock. Once more than one FreeRTOS
   task touches the bus (e.g. sampling task + battery task), add a mutex inside
   `transfer()`.
2. **Busy-waits during a transfer.** The CPU spins on `EVENTS_STOPPED`
   (~1 ms for the largest AS7343 read). Negligible at one sample per 30 s; can
   become interrupt-driven (TWIM IRQ + semaphore) if needed.
3. **No stuck-bus recovery.** If a device resets mid-transfer it can hold SDA
   low. Standard fix: toggle SCL ~9 times as GPIO, then send a STOP. Add if
   ever observed.
4. **Timeout is a poll count, not a time.** `I2C_POLL_LIMIT` is a stuck-bus
   safety net only; it does not give a precise duration.
5. **Buffers must be in RAM.** EasyDMA cannot read flash, so a `const` array at
   file scope cannot be passed directly. `transfer()` rejects such buffers with
   `I2C_ERR_ARG` rather than sending garbage.

### Verified against documentation

- SCL/SDA pin config = input, input buffer connected, drive S0D1; PSEL written
  only while TWIM is disabled — PS §6.31 pin configuration table.
- A NACK raises EVENTS_ERROR but does not end the transfer; STOP cannot take
  effect while suspended, so RESUME is issued before STOP — PS §6.31.
- ERRORSRC bits are write-1-to-clear; TXD/RXD.AMOUNT include the NACKed byte
  — PS §6.31 registers.
- **TXD/RXD.AMOUNT are not reset by a transfer that doesn't use that
  channel.** A write-only transfer leaves RXD.AMOUNT holding the previous
  read's count. `transfer()` only checks RXD.AMOUNT when `rx_len > 0`.
  Found on hardware 2026-10-01: every write after the first read returned
  `I2C_ERR_SHORT` with TX.AMOUNT correct and RX.AMOUNT stale.
- **Errata [219]: SCL low period is 1.25 µs at 400 kHz; AS7343 requires
  tLOW ≥ 1.3 µs.** Bus runs at 390 kHz (`FREQUENCY = 0x06200000`), Nordic's
  workaround.
- Errata [89] (TWIM static 400 µA with GPIOTE) is not listed for Rev 3 — no
  workaround needed on the module.
- Open: the Feather's nRF52840 may be an older silicon revision than the
  module's Rev 3. Not a problem so far, but keep in mind if behaviour differs
  between the two boards.

---

## drivers/AS7343 — spectral sensor

Status: `as7343_init()` (ID check + full configuration, leaves sensor asleep)
verified on Feather + Adafruit breakout (2026-10-01), including after
deliberately scribbling non-default values first.

### Known limitations

1. **Default exposure is a placeholder.** Gain 256x, ATIME 29, ASTEP 599
   (50 ms, full scale 18000 counts) suits indoor light; daylight will
   saturate. To be addressed with `as7343_set_exposure()` and saturation
   flags.
2. **No retry during the sensor's power-up window.** The AS7343 NACKs for
   ~200-300 µs after power-on (datasheet §8). Not an issue in practice: the
   bootloader runs far longer than that before our code starts.
3. **Init leaves CFG0.LOW_POWER and WLONG untouched.** Irrelevant while WEN
   is off and the sensor sleeps via PON = 0.

### Verified on hardware / against documentation

- Bank 1 (CFG0.REG_BANK = 1) is needed for registers 0x20-0x7F, including
  ID at 0x5A; CFG0 itself is readable and writable from either bank.
- Datasheet §10.2.1 order is followed: PON = 1 and SP_EN = 0 before
  configuring.
- 16-bit ASTEP written and read low byte first in a single burst (§9).
- **CFG20 reads back 0x62 after setting auto_smux = 3: reserved bits 4:0
  hold 0x02.** Undocumented, preserved by read-modify-write — never write
  CFG20 blindly.
- CFG3 reserved low nibble reads 0xC after init, as documented.
