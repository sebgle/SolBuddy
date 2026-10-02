"""
SolBuddy BLE reference client (docs/ble_protocol.md, version 1).

Talks to a SolBuddy over the PC's Bluetooth: reads Status, sets the time,
checks that bad writes are rejected, and pulls the log, verifying every
record's CRC and sequence number. It is both a test tool for the firmware
and the reference decoder for the phone app.

Usage (from code/SolBuddy):
    tools/.venv/Scripts/python tools/solbuddy_sync.py status
    tools/.venv/Scripts/python tools/solbuddy_sync.py time
    tools/.venv/Scripts/python tools/solbuddy_sync.py reject-test
    tools/.venv/Scripts/python tools/solbuddy_sync.py pull [--from N] [--max M] [--csv FILE]
    tools/.venv/Scripts/python tools/solbuddy_sync.py all

Only one central can be connected at a time: disconnect the phone first.
"""

import argparse
import asyncio
import csv
import struct
import sys
import time

from bleak import BleakClient, BleakScanner

# ---- UUIDs (protocol §4) -----------------------------------------------------

def uuid(xxxx: int) -> str:
    return f"a222{xxxx:04x}-40cd-4ab3-a79e-6157aec9630e"

UUID_SERVICE  = uuid(0x0001)
UUID_STATUS   = uuid(0x0002)
UUID_TIME     = uuid(0x0003)
UUID_LOG_READ = uuid(0x0004)

# ---- record format (src/services/record.h) -----------------------------------

RECORD_SIZE = 64
RECORD_VERSION = 1
FLAG_SATURATED, FLAG_CHARGING, FLAG_TIME_UNSET = 0x01, 0x02, 0x04
SLOT_NAMES = ["FZ", "FY", "FXL", "NIR", "CLEAR", "FD",
              "F2", "F3", "F4", "F6", "CLEAR", "FD",
              "F1", "F7", "F8", "F5", "CLEAR", "FD"]


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF. '123456789' -> 0x29B1."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


assert crc16_ccitt(b"123456789") == 0x29B1


def decode_record(raw: bytes) -> dict:
    """Decode one 64-byte record; raises ValueError on a bad CRC or version."""
    if len(raw) != RECORD_SIZE:
        raise ValueError(f"record is {len(raw)} bytes, expected 64")
    (stored_crc,) = struct.unpack_from("<H", raw, 62)
    if crc16_ccitt(raw[:62]) != stored_crc:
        raise ValueError("bad CRC")
    if raw[0] != RECORD_VERSION:
        raise ValueError(f"unknown record version {raw[0]}")
    flags, gain, atime, astep = struct.unpack_from("<BBBH", raw, 1)
    seq, timestamp = struct.unpack_from("<II", raw, 8)
    counts = struct.unpack_from("<18H", raw, 16)
    (boot_id,) = struct.unpack_from("<H", raw, 52)
    return {
        "seq": seq, "timestamp": timestamp, "boot_id": boot_id,
        "flags": flags, "saturated": bool(flags & FLAG_SATURATED),
        "charging": bool(flags & FLAG_CHARGING), "time_unset": bool(flags & FLAG_TIME_UNSET),
        "gain": gain, "atime": atime, "astep": astep, "counts": counts,
    }

# ---- Status (§5.1) -------------------------------------------------------------

STATES = {0: "NORMAL", 1: "SYNC", 2: "FAST_ADV", 3: "CHARGING", 4: "LIVE"}


def decode_status(b: bytes) -> dict:
    if len(b) != 36:
        raise ValueError(f"Status is {len(b)} bytes, expected 36")
    (version, state, flags, _res, fw, boot_id, uptime, utc, vcell, soc,
     oldest, nxt, e_i2c, e_flash, e_sensor, _res2) = struct.unpack("<BBBBHHIIHHIIHHHH", b)
    return {
        "protocol_version": version, "state": STATES.get(state, f"?{state}"),
        "utc_set": bool(flags & 1), "usb": bool(flags & 2), "low_batt": bool(flags & 4),
        "fw_version": f"{fw >> 8}.{fw & 0xFF}", "boot_id": boot_id,
        "uptime_s": uptime, "utc_s": utc, "vcell_mv": vcell, "soc_pct": soc / 256,
        "oldest_seq": oldest, "next_seq": nxt,
        "errors": {"i2c": e_i2c, "flash": e_flash, "sensor": e_sensor},
    }

# ---- connection ----------------------------------------------------------------

async def connect() -> BleakClient:
    print("scanning for SolBuddy ...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: UUID_SERVICE in [u.lower() for u in adv.service_uuids]
                       or (d.name or "").startswith("SolBuddy-"),
        timeout=15.0)
    if device is None:
        sys.exit("no SolBuddy found (is the phone still connected to it?)")
    print(f"found {device.name} [{device.address}]")
    client = BleakClient(device)
    for attempt in range(1, 4):              # Windows connects flakily: retry
        try:
            await client.connect(timeout=20.0)
            break
        except (TimeoutError, OSError) as e:
            print(f"  connect attempt {attempt} failed ({type(e).__name__}); retrying")
            if attempt == 3:
                sys.exit("could not connect (is another phone/PC connected to it?)")
            await asyncio.sleep(2.0)
    try:
        await client.pair()          # Just Works; harmless if already bonded
    except Exception as e:           # some stacks pair implicitly on first secure access
        print(f"  (pair: {e})")
    print(f"connected, ATT MTU {client.mtu_size}")
    return client


async def read_status(client: BleakClient) -> dict:
    return decode_status(bytes(await client.read_gatt_char(UUID_STATUS)))

# ---- commands --------------------------------------------------------------------

async def cmd_status(client: BleakClient):
    s = await read_status(client)
    for k, v in s.items():
        print(f"  {k:17} {v}")


async def cmd_time(client: BleakClient):
    now = int(time.time())
    await client.write_gatt_char(UUID_TIME, struct.pack("<I", now), response=True)
    (back,) = struct.unpack("<I", bytes(await client.read_gatt_char(UUID_TIME)))
    print(f"  wrote UTC {now}, device reads back {back} (diff {back - now:+d} s)")


async def expect_rejected(client, label, char, payload) -> bool:
    try:
        await client.write_gatt_char(char, payload, response=True)
    except Exception as e:
        print(f"  ok    {label}: rejected ({e})")
        return True
    print(f"  FAIL  {label}: was ACCEPTED")
    return False


async def cmd_reject_test(client: BleakClient) -> bool:
    ok = True
    ok &= await expect_rejected(client, "Time before 2024", UUID_TIME, struct.pack("<I", 1700000000))
    ok &= await expect_rejected(client, "Time 2 bytes", UUID_TIME, b"\x00\x00")
    ok &= await expect_rejected(client, "Log Read unknown opcode", UUID_LOG_READ, b"\x7f")
    ok &= await expect_rejected(client, "Log Read short START", UUID_LOG_READ, b"\x01\x00\x00")
    print("  reject-test", "PASS" if ok else "FAIL")
    return ok


END_REASONS = {0: "CAUGHT_UP", 1: "STOPPED", 2: "MTU_TOO_SMALL", 3: "BAD_REQUEST", 4: "STORAGE_ERROR"}


async def cmd_pull(client: BleakClient, from_seq: int, max_count: int, csv_path: str | None) -> bool:
    records, bad = [], []
    packets = 0
    done = asyncio.Event()
    end = {}
    last_rx = time.monotonic()

    def on_notify(_char, data: bytearray):
        nonlocal packets, last_rx
        last_rx = time.monotonic()
        data = bytes(data)
        if data[0] == 0xE0 and len(data) == 6:
            end["reason"] = END_REASONS.get(data[1], f"?{data[1]}")
            (end["next_seq"],) = struct.unpack_from("<I", data, 2)
            done.set()
            return
        packets += 1
        if len(data) % RECORD_SIZE:
            bad.append(f"packet of {len(data)} bytes (not a multiple of 64)")
            return
        for i in range(0, len(data), RECORD_SIZE):
            try:
                records.append(decode_record(data[i:i + RECORD_SIZE]))
            except ValueError as e:
                bad.append(str(e))

    await client.start_notify(UUID_LOG_READ, on_notify)
    t0 = time.monotonic()
    await client.write_gatt_char(UUID_LOG_READ, struct.pack("<BII", 0x01, from_seq, max_count), response=True)

    while not done.is_set():                       # stop if the stream goes silent
        await asyncio.sleep(0.2)
        if time.monotonic() - last_rx > 10:
            print("  FAIL  no END after 10 s of silence")
            break
    elapsed = time.monotonic() - t0
    await client.stop_notify(UUID_LOG_READ)

    seqs = [r["seq"] for r in records]
    gaps = [(a, b) for a, b in zip(seqs, seqs[1:]) if b != a + 1]
    print(f"  {len(records)} records in {packets} notifications, {elapsed:.1f} s "
          f"({len(records) * RECORD_SIZE / max(elapsed, 1e-6) / 1024:.1f} KiB/s)")
    if seqs:
        print(f"  seq {seqs[0]} .. {seqs[-1]}, gaps: {gaps if gaps else 'none'}")
    print(f"  END: {end if end else 'not received'}")
    print(f"  bad records: {bad if bad else 'none'}")
    if records:
        r = records[-1]
        print(f"  newest: seq {r['seq']} boot {r['boot_id']} ts {r['timestamp']}"
              f" ({'uptime' if r['time_unset'] else 'UTC'}) gain {r['gain']}"
              f" CLEAR {r['counts'][4]} NIR {r['counts'][3]}")

    if csv_path and records:
        with open(csv_path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["seq", "boot_id", "timestamp", "time_unset", "saturated", "charging",
                        "gain", "atime", "astep"] + [f"{n}_{i}" for i, n in enumerate(SLOT_NAMES)])
            for r in records:
                w.writerow([r["seq"], r["boot_id"], r["timestamp"], int(r["time_unset"]),
                            int(r["saturated"]), int(r["charging"]), r["gain"], r["atime"],
                            r["astep"], *r["counts"]])
        print(f"  wrote {csv_path}")

    expected_next = (seqs[-1] + 1) if seqs else None
    ok = bool(end) and not bad and (expected_next is None or end["next_seq"] >= expected_next)
    print("  pull", "PASS" if ok else "FAIL")
    return ok

# ---- main ------------------------------------------------------------------------

async def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=["status", "time", "reject-test", "pull", "all"])
    p.add_argument("--from", dest="from_seq", type=int, default=0)
    p.add_argument("--max", dest="max_count", type=int, default=0, help="0 = no limit")
    p.add_argument("--csv", help="write pulled records to this CSV file")
    a = p.parse_args()

    client = await connect()
    try:
        if a.command in ("status", "all"):
            print("Status:"); await cmd_status(client)
        if a.command in ("time", "all"):
            print("Time:"); await cmd_time(client)
        if a.command in ("reject-test", "all"):
            print("Reject test:"); await cmd_reject_test(client)
        if a.command in ("pull", "all"):
            print(f"Log Read from {a.from_seq}, max {a.max_count or 'all'}:")
            await cmd_pull(client, a.from_seq, a.max_count, a.csv)
        if a.command == "all":
            print("Status after:"); await cmd_status(client)
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
