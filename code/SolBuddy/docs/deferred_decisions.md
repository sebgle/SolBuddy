# Deferred decisions

Questions deliberately left open until a later section needs them. Before
building a new section, check here for anything it must answer. When a
decision is made, move it to "Decided" with the date and the answer.

Format: **question** — context, options, what it blocks.

---

## Open

### Before ordering the PCB

0. **QSPI pin choice — confirm SCK is on P0.19.**
   Verified in PS Table 61 (aQFN73, PDF p.928-929): recommended QSPI pins
   are CSN P0.18, SCK P0.19, data P0.21 / P0.22 / P0.23 / P1.00, all at
   high drive. Nordic's set can't be used as-is here: P0.18 is the only
   pin-reset pin (our nRESET) and P1.00 is SWO (J2 pin 6).
   Our board uses P0.19-P0.24, so **P0.20 and P0.24 are non-recommended**.
   Assessment: not a blocker. PSEL lets QSPI use any GPIO; "recommended"
   concerns signal quality at 32 MHz. The Feather runs QSPI on
   non-recommended P0.17/P0.20 without issue, and SolBuddy's throughput
   (48 B / 30 s) lets the driver run QSPI slowly (<= 8 MHz).
   **Actual mapping (from revA.kicad_pcb, U3 pads):** CS P0.19 (pad 42),
   SCK P0.20 (pad 44), IO0 P0.21 (43), IO1 P0.22 (46), IO2 P0.23 (45),
   IO3 P0.24 (48). The clock sits on a non-recommended pin while the
   recommended SCK pin carries CS.
   **Recommendation: swap CS and SCK before ordering** (SCK -> P0.19,
   CS -> P0.20). Puts the clock on the recommended pin and matches the
   Feather Express exactly (SCK P0.19, CS P0.20). Likely works either way
   at a slow QSPI clock; the swap is free insurance pre-fab.
   *Blocks:* PCB order (minor), board_pins.h QSPI entries.
   *Raised:* 2026-10-01. *Awaiting:* decision on the swap.

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

4. **Timestamp before the phone has set the clock.** After first boot or
   a full power loss the RTC has no UTC. Options: log uptime + boot counter
   and let the app re-base later; or don't log until time is set.
   *Blocks:* record format, RTC/time service.

### Firmware structure

6. **I2C bus lock.** `hal/i2c` has no mutex. Needed once two FreeRTOS
   tasks use the bus (sampling + battery). *Blocks:* app task layout.

7. **Board definition for the custom PCB.** The Adafruit core needs a
   board variant for the SolBuddy pins (QSPI, LFXO, no NeoPixel, etc.).
   *Blocks:* first flash of the PCB.

---

## Decided

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
