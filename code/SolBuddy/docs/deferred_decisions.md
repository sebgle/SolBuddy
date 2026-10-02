# Deferred decisions

Questions deliberately left open until a later section needs them. Before
building a new section, check here for anything it must answer. When a
decision is made, move it to "Decided" with the date and the answer.

Format: **question** — context, options, what it blocks.

---

## Open

### Sensing

1. **Lengthen integration time in dim light?**
   At 2048x / 50 ms a dim room reads only ~1-2 % of full scale (~300
   counts; ~10 when covered), so the < 10 lx and < 1 lx night thresholds
   will be single counts. Option: once gain is at 2048x, auto-range
   continues into longer ATIME/ASTEP (up to ~180 ms cycles, full scale
   65535). Cost: sensor-on time per sample (battery).
   *Blocks:* night-time accuracy. *Decide with:* the power budget.
   *Raised:* 2026-10-01, app/sampler hardware test.

2. **App-side check for light changing mid-measurement.**
   The three cycles are ~50 ms apart; a sudden change makes slots
   inconsistent (seen: CLEAR 262 in cycle 1, 1084 in a later cycle).
   Proposed: app compares the three CLEAR slots (4/10/16) and flags or
   discards the sample if they disagree beyond noise. Threshold TBD.
   *Blocks:* app data processing. No firmware change needed.
   *Raised:* 2026-10-01, app/sampler hardware test.

3. **Per-gain calibration.** Gain steps are a few % off ideal (64x ->
   0.23-0.25x of 256x; 2048x -> 8.06-8.39x). The app needs a measured
   factor per gain code, stored with the device calibration.
   *Blocks:* absolute accuracy; calibration sector layout.

### Logging

### Firmware structure

6. **I2C bus lock.** `hal/i2c` has no mutex. Needed once two FreeRTOS
   tasks use the bus (sampling + battery). *Blocks:* app task layout.

7. **Board definition for the custom PCB.** The Adafruit core needs a
   board variant for the SolBuddy pins (QSPI, LFXO, no NeoPixel, etc.).
   *Blocks:* first flash of the PCB.

8. **Hold HFXO during QSPI once BLE is running (errata [244]).** QSPI data
   is corrupted if the HF clock switches between HFXO and HFINT mid-transfer,
   which the SoftDevice does. Workaround: `sd_clock_hfclk_request()` around
   flash operations while the SoftDevice is enabled; without it, start HFXO
   via NRF_CLOCK directly. *Blocks:* BLE bring-up, flash driver clock hooks.
   *Raised:* 2026-10-01.

### Power

9. **Low-battery cut-off voltage.** The spec's LOW-BATT state sends the
   device to System OFF "below cut-off" but gives no number. The MAX17048
   driver takes it as a parameter (VALRT.MIN, 20 mV steps) and pulls ALRT
   (P0.26) low below it. Candidates: ~3.3-3.4 V leaves margin for the
   TPS62740 (needs VIN above its 1.8 V output plus dropout) and for cell
   health. Also decide: act on voltage alone, or on SOC (ATHD alert)?
   *Blocks:* app LOW-BATT state. *Raised:* 2026-10-01.

---

## Decided

- **2026-10-02 — BLE security: LE Secure Connections, Just Works bonding.**
  Every SolBuddy characteristic requires an encrypted link. No passkey is
  possible (no screen/button); accepted risk: an active attacker during the
  very first pairing. See docs/ble_protocol.md §2.1.

- **2026-10-01 — Timestamps before the phone sets UTC:** log seconds since
  boot with RECORD_FLAG_TIME_UNSET, plus a 16-bit boot_id (record bytes
  52-53) = newest stored record's boot_id + 1 (no extra flash writes).
  At sync the device reports (boot_id, uptime, UTC) so the app can re-base
  that boot's records; records from a boot that never saw the phone stay
  relative but ordered.

- **2026-10-01 — QSPI pins stay as routed (no CS/SCK swap).** PCB: CS
  P0.19, SCK P0.20, IO0 P0.21, IO1 P0.22, IO2 P0.23, IO3 P0.24. Nordic
  recommends SCK on P0.19 (PS Table 61), so the clock is on a
  non-recommended pin; mitigated by running QSPI slowly (<= 8 MHz).
  The Feather differs (SCK P0.19, CS P0.20) — board_pins.h selects per board.

- **2026-10-01 — Log full of unsynced data: overwrite the oldest anyway.**
  Recent behaviour is what goals/nudges act on, and a device that silently
  stops recording looks broken. The device therefore does not track
  "synced"; the phone tracks which sequence numbers it already has.

- **2026-10-01 — Record size: 64 bytes** (spec said 48). The listed
  fields need 51 bytes; 64 divides the 256-byte flash page exactly (4 per
  page) and leaves ~13 spare bytes. Capacity: 8 MB / 64 B = 131072
  records ≈ 45 days at 30 s (spec said ~57 days at 48 B).

- **2026-10-01 — Exposure policy:** firmware auto-ranges gain with
  immediate retry when too bright (max 5 attempts); FD-only saturation
  ignored. See `services/autorange`, `app/sampler`.
- **2026-10-01 — Driver approach:** own register-level drivers on own
  register-level bus layer (TWIM directly, no Arduino Wire); Adafruit core
  kept for BLE, USB, bootloader, FreeRTOS.
