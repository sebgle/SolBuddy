# Firmware TODO

Programming tasks left before the SolBuddy device firmware is complete.
Ordered roughly by dependency. `(Dn)` points to an open item in
`deferred_decisions.md` that the task must settle first; `(HW)` means it can
only be verified on the PCB.

Last updated: 2026-10-01.

---

## Done

- [x] hal/i2c — TWIM on registers, 390 kHz (errata 219)
- [x] drivers/AS7343 — init, exposure, 18-channel measurement, ASTATUS fix
- [x] services/autorange + app/sampler — auto-ranged gain with retry
- [x] services/record + crc16 — 64-byte record, boot_id
- [x] services/ringlog — circular log, power-loss recovery
- [x] hal/qspi + drivers/spi_nor — QSPI, MX25R/GD25, deep power-down, IO1 fix
- [x] app/storage — log on external flash, sector 0 reserved
- [x] drivers/MAX17048 — fuel gauge (unit-tested only) (HW)
- [x] services/timekeeper + app/clock — uptime, UTC, wrap-safe

---

## 1. BLE protocol (write the spec first)

- [x] **docs/ble_protocol.md** (draft v1, 2026-10-02) — the contract the phone app is built against:
      service/characteristic UUIDs and exact byte layouts for Status, Time,
      Log Read, Config, Live; Log Read request/notify framing, batching,
      end-of-log and LOST markers; protocol version field.
- [x] Decide: pairing/bonding — Just Works bonding, encryption required

## 2. BLE firmware

- [x] Bring up the SoftDevice (Bluefruit) on the Feather: advertise + connect
      (verified 2026-10-02: SolBuddy-9E3D, Just Works pairing on encrypted
      Status read, reconnect after disconnect)
- [ ] Hold HFXO during QSPI while the SoftDevice runs — errata 244 (D8)
- [ ] GATT service skeleton with the five characteristics
- [ ] Time: phone writes UTC -> clock_set_utc
- [ ] Status: SoC, VCELL, uptime/UTC/boot_id, oldest/next seq, fault counters
- [ ] Log Read: stream records from seq N; batch flash sessions; skip LOST
- [ ] Config: sample interval, advertising policy, live enable; persist in
      flash sector 0
- [ ] Live: 1 Hz stream, auto-exit after N minutes
- [ ] Advertising policy: 1285 ms default; 20 ms burst for 30 s after reset
      or USB insertion
- [ ] Buttonless DFU (firmware update over BLE)

## 3. Application structure

- [ ] Task layout: sampling task (every 30 s), BLE, battery; priorities
- [ ] Locks for the I2C bus and storage once two tasks share them (D6)
- [ ] State machine: BOOT / NORMAL / SYNC / FAST-ADV / CHARGING / LIVE /
      LOW-BATT, per the spec
- [ ] USB VBUS detection -> CHARGING flag on records + fast advertising
- [ ] Low battery: MAX17048 ALRT on P0.26 -> System OFF; wake on USB or pin
      reset; decide the cut-off voltage (D9) (HW)
- [ ] Reset reason (RESETREAS, mind errata 136) -> fast advertising, faults
- [ ] Fault counters (I2C, flash, sensor timeouts), reported in Status
- [ ] Watchdog (WDT) so a hang recovers instead of going silent
- [ ] Sector 0 format: device ID, calibration coefficients, config; readable
      over BLE (D3 per-gain calibration feeds into this)
- [ ] Real application entry point replacing the bring-up main.cpp;
      optional debug log on the UART test pads (TP3/TP4)

## 4. Power

- [ ] Audit every peripheral is off between samples (TWIM, QSPI, sensor PON,
      flash DPM, USB when unplugged); FreeRTOS tickless idle confirmed
- [ ] Measure average current (best on the PCB) against the 7-22 uA model (HW)
- [ ] Dim-light integration-time extension, decided with the power numbers (D1)

## 5. Custom PCB port

- [ ] Adafruit-core board variant for SolBuddy: pins, LFXO, no LEDs/NeoPixel,
      UICR.PSELRESET = P0.18 (D7)
- [ ] Bootloader build for the board + first-flash procedure over SWD
      (Tag-Connect J2, CTRL-AP ERASEALL on rev 3 silicon)
- [ ] `[env:solbuddy_reva]` with -DBOARD_SOLBUDDY_REVA
- [ ] Bring-up firmware: AS7343 ID, MAX17048 VERSION, MX25R ID C2 28 17,
      one sample logged, BLE advertising (HW)
- [ ] Hardware-verify MAX17048 driver and MX25R timings (HW)

## 6. Quality

- [ ] Move bring-up tests out of main.cpp into a separate `bringup` env
- [ ] Firmware README: build, flash, run unit tests, bring-up procedure
- [ ] Code review pass over the whole tree

---

## Later: phone app (separate milestone)

Builds on docs/ble_protocol.md and the record format in services/record.h.
- Decode records; re-base TIME_UNSET records via boot_id
- Melanopic EDI from raw counts (per-gain calibration, D3)
- Daylight classification (NIR vs visible), TAT250, MLiT250, evening/night
  compliance, consistency
- Discard samples whose three CLEAR slots disagree (D2)
