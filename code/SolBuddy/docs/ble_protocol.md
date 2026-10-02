# SolBuddy BLE protocol — version 1

The contract between the SolBuddy device firmware and the phone app. The
firmware and the app are both built against this document; change it first,
then the code, and bump the protocol version for incompatible changes.

Status: **draft**, 2026-10-02. Not yet implemented.

---

## 1. Conventions

- All multi-byte fields are **little-endian**.
- Offsets are in bytes from the start of the value.
- "u8/u16/u32" are unsigned, "i16" signed (two's complement).
- Every multi-field value starts with a **protocol version** byte (= 1).
  The app must reject versions it does not know.
- Records use the 64-byte format defined in `src/services/record.h`
  (version 1). The same format is used for stored records and for the
  live stream, so the app needs one decoder.

## 2. Connection requirements

### 2.1 Security: Just Works bonding

- The device has no screen or button, so pairing uses **LE Secure
  Connections, Just Works** (no passkey). The phone bonds once; later
  connections are encrypted with the stored keys.
- **Every characteristic in the SolBuddy service requires an encrypted
  link.** Reading or writing any of them on an unencrypted link returns
  *Insufficient Authentication*, which makes the phone start pairing.
- Limitation, accepted: Just Works does not protect against an active
  attacker present during the very first pairing.

### 2.2 MTU

- The app must negotiate an **ATT MTU of at least 67** (one 64-byte record
  + 3 bytes ATT header) before starting a log transfer or live stream.
  iOS typically negotiates 185; Android apps should request 247.
- Records are **never split** across notifications. Each notification
  carries `floor((MTU - 3) / 64)` whole records at most.
- If a transfer is started with MTU < 67, the device ends it immediately
  with END reason `MTU_TOO_SMALL` (§5.3).

## 3. Advertising

- Name: `SolBuddy-XXXX`, XXXX = last two bytes of the device address in hex.
- The SolBuddy service UUID is in the advertising or scan-response data.
- Interval: `adv_interval_ms` from Config (default 1285 ms). For 30 s after
  a reset or USB insertion the device advertises every 20 ms (FAST-ADV) so
  the phone finds it immediately.
- Not advertising while connected (one connection at a time) or in
  LOW-BATT (System OFF).

## 4. Service and characteristics

UUID base: `a222xxxx-40cd-4ab3-a79e-6157aec9630e`

| Characteristic | UUID xxxx | Properties | Length | Section |
|---|---|---|---|---|
| SolBuddy service | `0001` | — | — | |
| Status | `0002` | read, notify | 36 | §5.1 |
| Time | `0003` | read, write | 4 | §5.2 |
| Log Read | `0004` | write, notify | var. | §5.3 |
| Config | `0005` | read, write | 8 | §5.4 |
| Live | `0006` | notify | var. | §5.5 |
| Calibration | `0007` | read | TBD | §5.6 |

Writes are *write with response*. Invalid values are rejected with ATT
error `0xFF` (Out of Range); wrong lengths with `0x0D` (Invalid Attribute
Value Length). (`0x13` Value Not Allowed is newer than the SoftDevice
version in use, which treats it as reserved.)

## 5. Characteristic details

### 5.1 Status (read, notify) — 36 bytes

| Off | Size | Field | Notes |
|---|---|---|---|
| 0 | u8 | protocol_version | 1 |
| 1 | u8 | state | 0 NORMAL, 1 SYNC, 2 FAST_ADV, 3 CHARGING, 4 LIVE |
| 2 | u8 | flags | bit0 UTC set, bit1 USB power present, bit2 low-battery alert |
| 3 | u8 | reserved | 0 |
| 4 | u16 | fw_version | major << 8 \| minor |
| 6 | u16 | boot_id | this boot's number (as in records) |
| 8 | u32 | uptime_s | seconds since boot |
| 12 | u32 | utc_s | current UTC, 0 if not set |
| 16 | u16 | vcell_mv | cell voltage, mV |
| 18 | u16 | soc_x256 | state of charge, 1/256 % |
| 20 | u32 | oldest_seq | oldest record still stored |
| 24 | u32 | next_seq | sequence number the next record will get |
| 28 | u16 | err_i2c | fault counters since boot (saturate at 0xFFFF) |
| 30 | u16 | err_flash | |
| 32 | u16 | err_sensor | |
| 34 | u16 | reserved | 0 |

- Stored records are `[oldest_seq, next_seq)`; empty when equal.
- **Re-basing TIME_UNSET records:** records with flag TIME_UNSET and
  `boot_id == Status.boot_id` have `timestamp` = seconds since boot. Once
  `utc_s != 0`, their UTC is `timestamp + (utc_s - uptime_s)`. Records from
  other boots with TIME_UNSET stay relative (ordered, spaced, no absolute
  time).
- Notified when `state` or `flags` change while subscribed.

### 5.2 Time (read, write) — 4 bytes

| Off | Size | Field |
|---|---|---|
| 0 | u32 | utc_s — seconds since 1970-01-01T00:00:00Z |

- Write: sets the device clock. Values before 2024-01-01 (1704067200) are
  rejected (`0xFF`): they indicate an unset phone clock.
- Read: current UTC, 0 if never set this boot.
- The app should write Time on every connection.

### 5.3 Log Read (write, notify)

Transfers stored records, oldest first. Subscribe to notifications first.

**Commands (write):**

| Opcode | Name | Payload |
|---|---|---|
| `0x01` | START | u32 from_seq, u32 max_count (0 = no limit) — 9 bytes total |
| `0x02` | STOP | — 1 byte total |

- START streams records from `from_seq` (or from `oldest_seq` if
  `from_seq` is older) until caught up or `max_count` records were sent.
  A START during a transfer restarts it.
- Damaged records (power lost mid-write) are skipped: the app sees a gap
  in `seq`. Notifications are acknowledged at the link layer, so within a
  connection a gap always means a record lost on the device, never one
  lost in transit.

**Notifications** — the first byte tells the type:

| First byte | Type | Content |
|---|---|---|
| `0x01` | records | 1 or more whole 64-byte records (version 1), concatenated |
| `0xE0` | END | 6 bytes: `0xE0`, u8 reason, u32 next_seq |

END `reason`: 0 CAUGHT_UP, 1 STOPPED (STOP command or max_count reached),
2 MTU_TOO_SMALL, 3 BAD_REQUEST, 4 STORAGE_ERROR.
`next_seq` = the first seq *not* sent; the app resumes from here next time.

**App procedure:** remember the last seq stored on the phone; START from
`last + 1`; verify each record's CRC (re-request from the first bad seq if
one fails); stop at END.

### 5.4 Config (read, write) — 8 bytes

| Off | Size | Field | Range | Default |
|---|---|---|---|---|
| 0 | u8 | protocol_version | 1 | 1 |
| 1 | u8 | live_timeout_min | 1-60 | 5 |
| 2 | u16 | sample_interval_s | 10-3600 | 30 |
| 4 | u16 | adv_interval_ms | 20-10240 | 1285 |
| 6 | u16 | reserved | 0 | 0 |

- Writes must be all 8 bytes; any field out of range rejects the whole
  write (`0xFF`) and nothing changes.
- Persisted in flash (sector 0); survives resets and power loss.

### 5.5 Live (notify)

- **Subscribing starts LIVE mode**: the device takes one auto-ranged sample
  per second and notifies it as a 64-byte record (first byte `0x01`) with
  `seq = 0xFFFFFFFF`. Live samples are **not** stored; normal logging
  continues on its own schedule.
- LIVE ends on unsubscribe, disconnect, or after `live_timeout_min`. On
  timeout the device notifies `0xE1` (1 byte, LIVE_END).

### 5.6 Calibration (read) — reserved

Per-device calibration coefficients (including per-gain factors, see
deferred decision D3). Format to be defined; the first byte will be a
calibration format version.

## 6. Typical sync (app side)

1. Scan for the service UUID; connect; bond if not bonded.
2. Negotiate MTU (request 247).
3. Read Status; write Time.
4. Subscribe to Log Read; write START(last_seq + 1, 0).
5. Receive records until END; store them; remember END.next_seq - 1.
6. Disconnect (the device returns to NORMAL advertising).
