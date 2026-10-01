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

Status (verified on Feather + Adafruit breakout, 2026-10-01):
- `as7343_init()` — ID check + full configuration, leaves sensor asleep;
  verified after deliberately scribbling non-default values first.
- `as7343_start_measurement()` / `data_ready()` / `read()` / `sleep()` —
  18-channel measurement with correct gain and saturation reporting, in
  room light and under a phone flashlight.

### Known limitations

1. **Only gain is auto-ranged; integration time is fixed at 50 ms.**
   Indoors (a dim room) the peak sits at ~1-2 % of full scale even at
   2048x (~300 counts; ~10 when covered). The < 10 lx / < 1 lx night
   thresholds will be single counts. Next lever: lengthen integration time
   once gain is at 2048x. Trade-off is sensor-on time vs battery — decide
   with the power budget.
2. **No retry during the sensor's power-up window.** The AS7343 NACKs for
   ~200-300 µs after power-on (datasheet §8). Not an issue in practice: the
   bootloader runs far longer than that before our code starts.
3. **Init leaves CFG0.LOW_POWER and WLONG untouched.** Irrelevant while WEN
   is off and the sensor sleeps via PON = 0.
4. **Open, not reproduced: one 59 ms measurement.** The very first
   measurement after flashing the chunk-3 firmware reported AVALID after
   59 ms (≈ one cycle) instead of ~161 ms (three cycles). Did not recur in
   later runs or after a reset. If it did, slots 6-17 of that sample would
   be stale. Watch for it; a defensive check would be a minimum elapsed
   time before accepting AVALID.
5. **Light changing mid-measurement makes slots inconsistent.** The three
   cycles are ~50 ms apart, so a sudden change (flashlight arriving) gives
   e.g. CLEAR 262 in cycle 1 but 1084 in a later cycle. Not a firmware bug.
   For the app: compare the three CLEAR slots (4/10/16); if they disagree
   beyond noise, flag or discard the sample.

---

## services/autorange — auto-ranging decision (pure logic)

Status: 15 unit tests pass on PC (`pio test -e native`); verified on
hardware through app/sampler (2026-10-01).

Rule (full scale fs = min(65535, (ATIME+1)(ASTEP+1)); peak = max over the
15 non-FD slots):
- peak >= fs-1 (clipped): drop 3 gain steps, retry now.
- peak > 80 % fs, or saturated flag not explained by FD at full scale:
  drop enough steps to reach <= 50 % (min 1), retry now.
- peak < 20 % fs: accept; raise gain enough to approach 50 % next sample.
- otherwise accept, same gain. Gain clamped to 0.5x..2048x.

## app/sampler — one loggable sample

Status: verified on hardware (2026-10-01). Room → flashlight took 3
attempts (2048x → 256x → 32x, 488 ms); flashlight → room logged one dim
sample then jumped back up in one step; FD-only saturation did not cause
back-off. Uses FreeRTOS `vTaskDelay` while waiting (CPU sleeps; tickless
idle, 1024 Hz tick). Max 5 attempts; timeout = 3 cycles + 100 ms.

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
- **Slot naming:** six ADCs → six slots per cycle (§8.1), so CFG20's
  "2xVIS" is one CLEAR slot, not two. Slots 4/10/16 = CLEAR, 5/11/17 = FD.
- **AVALID in 18-channel mode is set only after all three cycles:**
  ~161 ms from start at ATIME 29 / ASTEP 599 (3 × 50 ms + overhead).
  It is 0 right after start, and reading the data clears it.
- **Undocumented: the first ASTATUS read after a measurement returns 0x00.**
  That read performs the latch; a second read returns the real status
  (e.g. 0x09 = gain 256x, 0x89 = gain 256x + saturated). Stopping the
  measurement first (SP_EN = 0) does not help. `as7343_read()` therefore
  does one discarded 1-byte ASTATUS read, then the 37-byte burst. Data
  bytes are fresh on the first read; only the status byte lags.
- STATUS2 reads 0x44 normally and 0x4C when saturated: AVALID (0x40) +
  ASAT_ANALOG (0x08) + **reserved bit 2 (0x04), always set.**
- ASAT_DIGITAL (STATUS2 bit 4) did not set with counts at 17999 against a
  full scale of 18000; ASTATUS.ASAT_STATUS did. Use ASTATUS for saturation.
- **Exposure scaling is linear (set_exposure verified):** relative to
  256x/50 ms, 64x gives 0.23-0.25x, 2048x gives 8.06-8.39x, 100 ms gives
  1.97-2.10x. Each gain step is a few % off ideal -> per-gain calibration
  needed later. 100 ms exposure takes ~312 ms (3 cycles).
- **The FD (flicker) channel saturates long before the spectral channels.**
  FD reads ~13-25x CLEAR (larger photodiode, same gain in 18-ch mode). At
  2048x: FD = 18000 (full scale) while max non-FD ~2000. ASTATUS has one
  saturation flag for all slots, so FD sets it first. Auto-ranging must
  judge saturation from non-FD counts and treat the flag as explained when
  FD >= full scale - 1. FD reached exactly 18000 here (digital limit =
  full scale).

---

## hal/qspi — QSPI bus layer (single-line SPI, 8 MHz, polled)

Status: verified on Feather (GD25Q16C), 2026-10-01.

### Known limitations
1. **Single-line SPI only** (FASTREAD 0x0B / PP 0x02). Quad mode would
   need chip-specific quad-enable bits; throughput (64 B / 30 s) doesn't
   need it.
2. **Busy-waits on READY** with a poll-count safety net, like hal/i2c.
3. **No HFXO request yet** — needed once the SoftDevice runs (errata [244],
   see deferred_decisions.md #8).

### Verified on hardware / against documentation
- PS §6.19.1 configuration order; QSPI pins at high drive (H0H1).
- EasyDMA: word-aligned flash address and RAM buffer, length multiple of 4.
- Erase READY means *started*, not finished (PS §6.19.4) — poll WIP.
- Custom instructions: LENGTH counts the opcode; IO2/IO3 (WP#/HOLD#) held
  high via LIO2/LIO3.
- Errata [122] current fix applied in qspi_uninit (before ENABLE = 0).
- **Do not use the peripheral's own deep power-down (IFCONFIG0.DPMENABLE /
  IFCONFIG1.DPMEN).** On hardware, entering DPM left STATUS = 0x06
  (DPM = 1, READY = BUSY) and no READY event ever came; custom
  instructions are refused while BUSY. Sending B9/AB as custom
  instructions works and is verified (chip stops answering RDID: FF FF FF).

## drivers/spi_nor — SPI NOR flash (MX25R6435F, GD25Q16C)

Status: verified on Feather GD25Q16C, 2026-10-01: ID C8 40 15, 4 KB erase
~38-41 ms, 4 x 64 B program ~1 ms, misaligned source buffers, cross-page
program refused, deep power-down proven (no RDID answer) and wake.
MX25R6435F not yet tested (needs the PCB).

- Timings use the stricter chip: program timeout 20 ms, erase 400 ms,
  DPM settle 1 ms (needs 20 us enter / 35 us exit).
- MX25R standby is 5 uA typ / 24 uA max vs 0.007 uA in deep power-down:
  the flash must be powered down between writes.
- spi_nor_init wakes the chip and waits for any interrupted program/erase:
  the flash keeps its state across an MCU reset.

## app/storage — sample log on external flash

Status: verified end to end on Feather, 2026-10-01: sensor -> sampler ->
record -> ringlog -> spi_nor; every record read back and matched; after a
reset the log resumed at the next seq (6) instead of restarting. Mount of
511 sectors: 49 ms. Append: 2 ms (sector-start appends add the erase).

- Layout: sector 0 reserved (calibration / metadata), log from sector 1.
  Feather: 511 log sectors; PCB (8 MB): 2047 sectors = 131008 records.
- Power: each call is a session — QSPI on, chip woken and identified,
  work, chip to deep power-down, QSPI off (errata [122] fix).

### Known limitations / open
1. **Open: first append after boot takes 257-280 ms** (later ones 2 ms),
   even when it is not a sector start. Cause not yet identified — time each
   step of session_begin (qspi_init, wake, wait_while_busy, RDID).
   Impact small (once per boot) but should be understood.
2. **No lock.** Sampling and (future) BLE sync must not use storage at the
   same time — same as hal/i2c; see deferred_decisions.md #6.
3. **Reads are one session per record.** Batch for BLE sync later.

## drivers/MAX17048 — fuel gauge

Status: **not hardware-tested** (no breakout; first test on the PCB).
13 unit tests pass against a simulated gauge (test/test_max17048, attached
to the PC-only bus in src/hal/host).

- 16-bit registers, **MSB first** on the wire (opposite of the AS7343).
- I2C logic: VIH min 1.4 V; our bus idles at 1.8 V -> 0.4 V margin.
- init: VERSION & 0xFFF0 == 0x0010, CONFIG written explicitly (0x971C:
  RCOMP 0x97, ATHD 4 %, ALRT clear), VALRT = low_alert_mv / 20 with high
  alert off (0xFF), then STATUS.RI cleared last.
- Conversions: VCELL mV = raw * 5 / 64 (78.125 uV/LSb); SOC kept raw
  (1/256 %); CRATE signed, 0.208 %/h per LSb.
- Not used: quick-start (can corrupt SOC), custom model (TABLE), sleep mode.
- Datasheet note 6: the gauge enters shutdown if SDA and SCL are both low
  > 2.5 s — keep the I2C pull-ups powered whenever the gauge should run.
