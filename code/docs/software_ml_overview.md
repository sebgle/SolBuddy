# SolBuddy — software and AI/ML overview

Written 2026-10-01 from a read-through of the whole repo at commit `8a7c56b`.
Purpose: summarise what the hardware and firmware do today, what the app and
ML side needs from them, and a plan for getting from logger to product.

Product goal (from Sebastian): learn the user's routine and send smart
notifications — "go outside within X minutes", "start winding down, too much
blue light" — to improve circadian rhythm.

---

## 1. Bottom line

- The repo is a solid light **logger**: sample → auto-gain → 64-byte record →
  circular log on flash.
- It is **light-only** (no motion, no temperature, no LED or haptic).
- **Bluetooth is not started** and no protocol is defined yet.
- The device stores **raw sensor counts**. Turning counts into lux and "blue
  light" numbers is app-side work, and it comes before any ML.

---

## 2. Hardware (rev A PCB, about 17 × 60 mm, 4-layer)

| Part | Role |
|---|---|
| nRF52840 module (Raytac MDBT50Q-1MV2) | CPU, Bluetooth, USB |
| AS7343 | Light sensor: 12 colour bands from 405 to 855 nm, plus clear and flicker channels |
| MX25R6435F | 8 MB NOR flash for the light log |
| MAX17048 | Battery fuel gauge |
| MCP73831 charger, ~100 mAh LiPo, TPS62740 1.8 V buck | Power |
| USB-C, ESD protection, 32.768 kHz crystal, reset button, SWD header | Service |

Not on the board: accelerometer, temperature sensor, LED, vibration motor,
user button. (The TMP117 temperature sensor was only in the breadboard demo.)

---

## 3. Firmware status

Everything verified so far ran on a Feather nRF52840 dev board with an
AS7343 breakout, not on the PCB.

| Piece | State |
|---|---|
| I2C layer, AS7343 driver, 18-slot measurement | Works on dev board |
| Auto-gain (0.5x to 2048x, retries when too bright) | Works on dev board |
| 64-byte record with CRC, circular log on flash, resumes after reset | Works on dev board |
| Timekeeping (uptime until the phone sets UTC, boot counter) | Works on dev board |
| Flash deep power-down between writes | Works on dev board |
| Fuel gauge driver | Written, tested against a simulation, never run on hardware |
| Bluetooth (GATT service, sync) | Not started |
| 30-second sampling tasks, power policy, low-battery handling | Not started; `main.cpp` is a bring-up loop sampling every 3 s and printing over USB |
| Charging flag | Defined in the record format, nothing sets it yet |
| Board definition for the PCB, first run on the PCB | Not yet |

The host-side unit tests (`pio test -e native`) were not run during this
review (PlatformIO was not installed on the reviewing machine).

---

## 4. What the app receives

One record every 30 s (planned): 2,880 per day, about 184 KB per day.
Format is defined in `code/SolBuddy/src/services/record.h`.

| Field | Notes |
|---|---|
| `seq` | Sequence number; record `seq` lives in flash slot `seq % capacity` |
| `timestamp` | UTC seconds, or seconds since boot if `TIME_UNSET` is flagged |
| `boot_id` | Separates boots so uptime-stamped records can be re-based |
| `flags` | `SATURATED`, `CHARGING`, `TIME_UNSET` |
| `gain`, `atime`, `astep` | Exposure used for this sample |
| `counts[18]` | Raw counts: 12 spectral bands, 3 × clear, 3 × flicker |
| `crc16` | CRC-16/CCITT-FALSE over the first 62 bytes |

Behaviour the app must handle:

- The device buffers about 45 days (131,008 records on the 8 MB flash) and
  overwrites the oldest data when full.
- The device does **not** track what has been synced. The phone remembers
  which sequence numbers it already has.
- The phone sets UTC on every connect. Records logged before that carry
  uptime; the app re-bases them using (boot_id, uptime, UTC).

---

## 5. App-side responsibilities that are easy to miss

1. **Calibration.** Raw counts → lux and melanopic EDI (light weighted for the
   eye's clock-setting receptors; the unit the consensus thresholds use).
   Needs: normalising by gain and integration time, a measured correction per
   gain step (steps are a few percent off ideal), and a regression fitted
   against a reference instrument under several light sources, done through
   the final enclosure and any diffuser.
2. **Data cleaning.** Drop or flag: saturated samples; samples where the
   three clear slots (4/10/16) disagree, meaning the light changed
   mid-measurement; samples taken while charging (probably not worn).
3. **Dim-light accuracy.** At maximum gain a dim room reads about 300 counts
   and a covered sensor about 10. So 10 lux and 1 lux are single-digit
   counts — exactly the evening and night thresholds the wind-down feature
   depends on. Deferred decision #1 (longer integration in the dark) is
   really a product requirement.
4. **Nudge delivery.** The device cannot notify and does not stream. Nudges
   are phone notifications, scheduled ahead from the user's predicted day and
   cancelled or updated when a sync shows the goal was met.
5. **Sleep and activity.** With no motion sensor, "dark" could mean asleep,
   in a pocket, or under a sleeve. Sleep and activity have to come from
   HealthKit / Health Connect.

---

## 6. Notes on the ChatGPT research write-up

The circadian science in it is broadly sound (light timing as the main
clock input, sleep pressure as a separate process, meals and exercise as
secondary cues, a physiological model with personalisation on top). Only the
claims this plan relies on were checked; its specific 2024–2026 citations
were not.

Where it does not match this project:

- It assumes an accelerometer, temperature sensing and live JSON streaming.
  Rev A has none of these; the design is store-and-forward binary records.
- It scopes meals, caffeine, menstrual cycle, stress and fasting. All need
  manual logging. Cut from v1; build around the one thing only this device
  has: real spectral light.
- It under-weights calibration and wear position, which everything else
  depends on.
- Reinforcement learning is premature until there are real users.

---

## 7. What the app answers and shows

Targets come from the Brown et al. 2022 consensus, in melanopic EDI at the
eye: at least 250 lux in the daytime, at most 10 lux in the 3 hours before
bed, at most 1 lux during sleep.

Questions:

1. Did I get enough bright light, early enough? (minutes above 250, time of
   first bright light after waking)
2. Is my evening too bright? (level in the 3 hours before bed, and overnight)
3. What should I do right now? (one nudge, not six)
4. Is my rhythm regular or drifting? (week view of light and sleep timing)

Screens:

- 24-hour light timeline, log scale, with sunrise/sunset and target bands
- Today's progress toward the daytime light goal
- The current recommendation
- Weekly view
- Device battery and last sync

Suggested stack: native SwiftUI with CoreBluetooth and HealthKit. Background
Bluetooth and notifications are the hard parts and are native-first.

---

## 8. Where AI/ML fits, in order

1. **Calibration regression** (counts → melanopic EDI). Small, supervised,
   essential.
2. **Rule-based nudges** on the 250 / 10 / 1 thresholds. No ML. Ships the two
   features in the product goal and starts collecting data.
3. **Circadian phase model.** Published math, not ML: takes a light history
   and estimates body-clock time, typically within about an hour. The
   open-source `circadian` Python package implements several such models.
   This lets nudges follow the person's clock rather than the wall clock, and
   it is the real differentiator — most wearables infer light from motion.
4. **Routine model.** Per-user statistics by time of week (usual wake,
   commute, light, bedtime) to predict when someone is falling behind and
   when they are able to act.
5. **Indoor/outdoor and light-source classifier** from the spectrum (daylight
   has strong near-infrared; LEDs have almost none).
6. **Nudge timing by contextual bandit**, rewarded by whether light exposure
   rose after the nudge. Only meaningful with real users.
7. **Fitting personal clock parameters** (light sensitivity, natural period).
   Long term.

Avoid for now: end-to-end deep learning and reinforcement learning. With a
handful of users, strong priors beat big models. An LLM is fine for phrasing
a weekly summary from numbers the code already computed.

---

## 9. Roadmap

1. **Now, no PCB needed:** agree the Bluetooth protocol (set time, read
   status, read a range of records in batches). Add a USB dump of the flash
   log on the Feather so real data can be collected immediately. Write a
   Python decoder and a fake-data generator from the record format.
2. **Calibration and pipeline:** counts → melanopic EDI, cleaning, a day
   timeline in a notebook.
3. **App MVP:** sync, timeline, the two rule-based nudges, HealthKit sleep.
4. **Phase and routine models** for predictive nudges.
5. **Personalisation** once there are beta users.

### Next steps this week (Emir)

1. One conversation with Sebastian: get the product spec, ask for the USB
   log-dump command, start the Bluetooth protocol on paper.
2. Write the Python decoder and fake-data generator for the 64-byte record.
3. Carry the Feather rig for a few days once the dump exists; plot one day of
   light on a log scale, using datasheet channel sensitivities for a rough
   conversion at first.
4. In parallel, line up a reference instrument for calibration. A
   spectrometer is ideal; a lux meter alone gives ordinary lux, not the
   melanopic number.
5. Hold off on the app until the protocol is agreed and real data has been
   looked at.

---

## 10. Open questions for Sebastian

- Where is the product spec? The firmware notes reference one (48-byte
  record, LOW-BATT state, night thresholds, "goals/nudges") that is not in
  the repo.
- Where is the device worn, and what sits over the sensor? Light at the chest
  or wrist differs from light at the eye, and a cover changes calibration.
- Can the phone hold a low-power connection and be notified of new records?
  That decides how close to real time nudges can be.
- Can rev B add an accelerometer? It is the most valuable hardware change for
  the software side (wear detection, sleep estimation).
- Can the firmware lengthen integration time in dim light (deferred
  decision #1)?
- What is the battery-life target?

---

## Sources

- Brown et al. 2022, recommendations for daytime, evening and nighttime
  indoor light exposure: https://pubmed.ncbi.nlm.nih.gov/35298459/
- `circadian` Python package: https://pypi.org/project/circadian
- Low-cost spectral sensor calibration for melanopic EDI (Sensors):
  https://doi.org/10.3390/s25237269
- Wrist-worn sensor node for equivalent daylight illuminance:
  https://www.innerscene.com/papers/a-wrist-worn-internet-of-things-sensor-node-for-wearable-equivalent-daylight-illuminance-monitoring
