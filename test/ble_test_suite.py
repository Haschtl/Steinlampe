#!/usr/bin/env python3
"""
Automated BLE regression suite for the quarzlampe custom GATT protocol.

Connects over real BLE (not Serial - this exercises the actual path a phone
uses, which is where this project has repeatedly found bugs that static code
review or a Serial-only test missed). Starts with a pre-flight check that the
lamp is actually discoverable AND connectable at all (see check_connectable()'s
own doc comment - this is what would have caught the 2026-11-27 regression
where a self-made Mesh Provisioner lamp lost its only connectable BLE path),
then fires a battery of commands and checks that each one's expected response
line actually arrives within a timeout.
Exists specifically to catch "a write or a notification was silently dropped,
with no error raised on either end" - the root cause behind several bugs
found in this codebase (profile show/load over a write-without-response
burst, the BLE Mesh encryption/bond-cleanup gap, and the long STATUS/STATUS1/
STATUS2 lines vanishing over plain NOTIFY). A passing run here is not proof
those are fixed forever - BLE reliability can vary with phone/OS/radio
conditions - but a FAILING run is a real, reproducible regression worth
investigating immediately rather than waiting for the next manual bug report.

Usage:
    python3 test/ble_test_suite.py --scan              # list candidate lamps
    python3 test/ble_test_suite.py <MAC_ADDRESS>        # run the full suite
    python3 test/ble_test_suite.py <MAC_ADDRESS> -v     # also print every received line

Exit code is 0 only if every test case passed - safe to use as a CI-style gate.

Requires: bleak (pip install --user bleak)
"""

import argparse
import asyncio
import re
import sys
import time
from dataclasses import dataclass, field
from typing import List, Optional

from bleak import BleakClient, BleakScanner

SERVICE = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b"
CMD_CHAR = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7"
STATUS_CHAR = "c5ad78b6-9b77-4a96-9a42-8e6e9a40c123"
# Standard BLE Mesh service UUIDs - a mesh-enabled lamp advertises under these,
# not the custom SERVICE above (see frontend/src/hooks/bleClient.ts's own
# requestDevice() filters, which this mirrors for the same reason).
MESH_PROVISIONING_SERVICE = "00001827-0000-1000-8000-00805f9b34fb"
MESH_PROXY_SERVICE = "00001828-0000-1000-8000-00805f9b34fb"


@dataclass
class TestCase:
    name: str
    cmd: str
    # Every one of these regexes must be matched by some received line within `timeout`.
    expect: List[str]
    timeout: float = 2.5
    with_response: bool = True


@dataclass
class CaseResult:
    name: str
    cmd: str
    ok: bool
    detail: str


@dataclass
class ReceivedLog:
    lines: List[str] = field(default_factory=list)
    timestamps: List[float] = field(default_factory=list)

    def append(self, line: str) -> None:
        self.lines.append(line)
        self.timestamps.append(time.time())

    def since(self, index: int) -> List[str]:
        return self.lines[index:]


def build_sequential_cases() -> List[TestCase]:
    """One command at a time, each awaited to a fixed timeout before the next fires."""
    return [
        # STATUS1/STATUS2 are retired (status protocol redesign) - the core
        # snapshot is now just STATUS|; the former STATUS1/STATUS2 fields live
        # in their own on-demand groups, tested individually below. STATUS_END|
        # (a pure-duplicate completion marker) was retired 2026-11-28 - see
        # memory: status protocol redesign.
        TestCase("status raw - core snapshot", "status raw",
                 [r"^STATUS\|"], timeout=30.0),
        # New per-page status groups (status protocol redesign, step 1: pure
        # additions alongside the still-intact STATUS/STATUS1/STATUS2 above).
        TestCase("status radar", "status radar", [r"^RADAR\|"], timeout=15.0),
        TestCase("status light", "status light", [r"^LIGHT\|"], timeout=15.0),
        TestCase("status music", "status music", [r"^MUSIC\|"], timeout=15.0),
        TestCase("status poti", "status poti", [r"^POTI\|"], timeout=15.0),
        TestCase("status push", "status push", [r"^PUSH\|"], timeout=15.0),
        TestCase("status presence", "status presence", [r"^PRESENCE\|"], timeout=15.0),
        TestCase("status filters", "status filters", [r"^FILTERS\|"], timeout=15.0),
        TestCase("status io", "status io", [r"^IO\|"], timeout=15.0),
        TestCase("status mesh", "status mesh", [r"^MESHSTATUS\|"], timeout=15.0),
        TestCase("status timers", "status timers", [r"^TIMERS\|"], timeout=15.0),
        TestCase("status misc", "status misc", [r"^MISC\|"], timeout=15.0),
        TestCase("trust list", "trust list", [r"^\[Trust\] BLE:"], timeout=15.0),
        TestCase("trust ble bonds", "trust ble bonds", [r"^\[Trust\] BLE bonds:"], timeout=15.0),
        TestCase("profile show 1", "profile show 1", [r"^\[Profile 1\]"], timeout=15.0),
        TestCase("profile show 4 (likely empty)", "profile show 4", [r"^\[Profile 4\]"], timeout=15.0),
        TestCase("filter iir on", "filter iir on 0.2", [r"^\[Filter\] IIR"], timeout=15.0),
        TestCase("filter iir off", "filter iir off", [r"^\[Filter\] IIR"], timeout=15.0),
        TestCase("name query", "name", [r"^\[Name\] BLE="], timeout=15.0),
        # Regression test for a real bug found while extracting command.cpp's
        # dispatch table (see memory: command-dispatch-table-refactor): "notify
        # stop" used to be swallowed by the broader "notify" prefix match (checked
        # earlier), so it never reached the dedicated exact-match stop handler -
        # instead it turned the lamp ON and started a default blink. Fire a plain
        # "notify" first (confirms the normal blink still runs and self-finishes),
        # then "notify stop" and require the real stop feedback - if the ordering
        # regresses, this instead sees "[Lamp] ON (notify)" and a new blink here.
        TestCase("notify - plain blink", "notify",
                 [r"^\[Notify\] \d+/\d+/\d+/\d+", r"^\[Lamp\] OFF \(notify done\)"], timeout=3.0),
        TestCase("notify stop - must actually stop, not restart a blink", "notify stop",
                 [r"^\[Notify\] stopped"], timeout=2.0),
    ]


def build_burst_cases() -> List[TestCase]:
    """
    Fired back to back with NO await between writes and no artificial delay -
    the exact shape of the original "profile show 1..6" bug. with_response
    controls whether each write uses GATT write-with-response (should always
    pass) or write-without-response (the historically unreliable path - a
    regression here means something regressed back to fire-and-forget).
    """
    return [
        TestCase(f"burst profile show {n}", f"profile show {n}", [rf"^\[Profile {n}\]"], timeout=20.0, with_response=True)
        for n in range(1, 7)
    ]


def build_burst_cases_unreliable() -> List[TestCase]:
    """Same as build_burst_cases() but write-without-response, to demonstrate/detect the original bug class."""
    return [
        TestCase(f"burst (no-response) profile show {n}", f"profile show {n}", [rf"^\[Profile {n}\]"], with_response=False)
        for n in range(1, 7)
    ]


async def scan(timeout: float) -> None:
    print(f"Scanning for {timeout:.0f}s...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    found = False
    for addr, (dev, adv) in devices.items():
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        name = dev.name or adv.local_name or ""
        is_candidate = (
            SERVICE.lower() in uuids
            or MESH_PROVISIONING_SERVICE.lower() in uuids
            or MESH_PROXY_SERVICE.lower() in uuids
        )
        if is_candidate:
            found = True
            print(f"  {addr}  {name!r}  rssi={adv.rssi}  uuids={uuids}")
    if not found:
        print("  (no candidate lamp found - try again, or check it's powered on)")


async def check_connectable(address: str, scan_timeout: float, connect_timeout: float) -> Optional[str]:
    """Pre-flight check: is the lamp actually advertising right now, and can a
    fresh BLE central connect to it at all? Returns None if both hold, or an
    error string describing what failed.

    Exists because every other check in this suite assumes you already have
    an open, working connection - none of them would have caught the real
    2026-11-27 regression where a self-made BLE Mesh Provisioner lamp stopped
    advertising a connectable GATT path entirely (it still showed up in a
    plain scan via leftover non-connectable mesh beacon traffic, but nothing
    could connect to it - see comms.cpp's bleShouldAdvertiseClassic() for the
    fix). A failure here means "nobody can reach this lamp over BLE at all",
    which is a more fundamental problem than any single command's response
    being slow or dropped.

    Needs a BlueZ pairing agent to be registered system-wide (true on an
    ordinary desktop with its Bluetooth applet running; NOT true in a bare
    headless/CI shell with no `bluetoothctl agent on` or equivalent run first)
    - without one, a *fresh* (never-bonded) connection's pairing handshake can
    be rejected by BlueZ itself with "Pairing Not Supported", which looks
    identical to a real firmware bug from this script's point of view. This
    is a real, confirmed gotcha (hit directly while building this check), not
    a hypothetical one - if this fails only in a stripped-down shell/CI
    container, check for a registered agent before suspecting the firmware.
    """
    print(f"Pre-flight: scanning for {address} ({scan_timeout:.0f}s)...", flush=True)
    found = False

    def cb(dev, _adv):
        nonlocal found
        if dev.address.lower() == address.lower():
            found = True

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    deadline = time.time() + scan_timeout
    while time.time() < deadline and not found:
        await asyncio.sleep(0.1)
    await scanner.stop()
    if not found:
        return f"not seen in a {scan_timeout:.0f}s scan - lamp may be off, out of range, or not advertising at all"

    print("Pre-flight: seen in scan, attempting connect...", flush=True)
    try:
        async with BleakClient(address, timeout=connect_timeout):
            pass
    except Exception as e:
        return f"scan found it, but connect failed: {type(e).__name__}: {e}"
    return None


async def run_sequential(client: BleakClient, cases: List[TestCase], log: ReceivedLog, verbose: bool) -> List[CaseResult]:
    results = []
    for case in cases:
        start_idx = len(log.lines)
        t0 = time.time()
        print(f"  -> writing {case.cmd!r} (response={case.with_response})...", flush=True)
        try:
            await asyncio.wait_for(
                client.write_gatt_char(CMD_CHAR, (case.cmd + "\n").encode(), response=case.with_response),
                timeout=5.0,
            )
        except asyncio.TimeoutError:
            results.append(CaseResult(case.name, case.cmd, False, "write_gatt_char itself hung past 5s"))
            print(f"     write hung past 5s for {case.cmd!r}", flush=True)
            continue
        except Exception as e:
            results.append(CaseResult(case.name, case.cmd, False, f"write raised: {e}"))
            continue
        print(f"     write returned after {time.time() - t0:.2f}s", flush=True)

        patterns = [re.compile(p) for p in case.expect]
        seen = [False] * len(patterns)
        deadline = time.time() + case.timeout
        while time.time() < deadline and not all(seen):
            await asyncio.sleep(0.05)
            for line in log.since(start_idx):
                for i, pat in enumerate(patterns):
                    if not seen[i] and pat.search(line):
                        seen[i] = True
        elapsed = time.time() - t0
        if all(seen):
            results.append(CaseResult(case.name, case.cmd, True, f"all {len(patterns)} pattern(s) matched in {elapsed:.2f}s"))
        else:
            missing = [p for p, s in zip(case.expect, seen) if not s]
            results.append(CaseResult(case.name, case.cmd, False,
                                       f"missing {missing} after {elapsed:.2f}s ({len(log.lines) - start_idx} line(s) received)"))
        if verbose:
            for line in log.since(start_idx):
                print(f"    < {line}")
    return results


async def run_burst(client: BleakClient, cases: List[TestCase], log: ReceivedLog, verbose: bool) -> List[CaseResult]:
    """Fire every write immediately, with no waiting between them, then check all responses afterward."""
    start_idx = len(log.lines)
    t0 = time.time()
    write_errors = {}
    for case in cases:
        try:
            await client.write_gatt_char(CMD_CHAR, (case.cmd + "\n").encode(), response=case.with_response)
        except Exception as e:
            write_errors[case.name] = str(e)

    max_timeout = max((c.timeout for c in cases), default=2.5)
    deadline = time.time() + max_timeout
    patterns = {c.name: [re.compile(p) for p in c.expect] for c in cases}
    seen = {c.name: [False] * len(c.expect) for c in cases}
    while time.time() < deadline and not all(all(s) for s in seen.values()):
        await asyncio.sleep(0.05)
        for line in log.since(start_idx):
            for name, pats in patterns.items():
                for i, pat in enumerate(pats):
                    if not seen[name][i] and pat.search(line):
                        seen[name][i] = True

    results = []
    for case in cases:
        if case.name in write_errors:
            results.append(CaseResult(case.name, case.cmd, False, f"write raised: {write_errors[case.name]}"))
            continue
        if all(seen[case.name]):
            results.append(CaseResult(case.name, case.cmd, True, f"matched within the {max_timeout:.1f}s burst window"))
        else:
            results.append(CaseResult(case.name, case.cmd, False, "never matched - likely dropped in the burst"))
    if verbose:
        for line in log.since(start_idx):
            print(f"    < {line}")
    print(f"  (burst took {time.time() - t0:.2f}s wall time for {len(cases)} commands)")
    return results


def print_results(title: str, results: List[CaseResult]) -> bool:
    print(f"\n=== {title} ===")
    all_ok = True
    for r in results:
        mark = "PASS" if r.ok else "FAIL"
        if not r.ok:
            all_ok = False
        print(f"  [{mark}] {r.name:<40} ({r.cmd!r}) - {r.detail}")
    return all_ok


async def main_async(args: argparse.Namespace) -> int:
    if args.scan:
        await scan(args.scan_timeout)
        return 0

    connect_failure = await check_connectable(args.address, args.scan_timeout, connect_timeout=20.0)
    if connect_failure:
        print(f"\n[FAIL] Pre-flight connectivity check: {connect_failure}")
        print("Nothing else in this suite can run without a working connection - stopping here.")
        return 1
    print("Pre-flight: connect succeeded.\n", flush=True)

    log = ReceivedLog()

    # Buffered across packets, not split per-notification-packet: a multi-line
    # response (or even one long line) commonly spans more than one ATT
    # indicate/notify packet, and splitting each packet independently on its
    # own tears a line in half at the packet boundary - the fragments then
    # land as bogus separate "lines" (confirmed while adding the notify/notify
    # stop regression cases below: a late-arriving tail fragment of the
    # previous case's response bled into the next case's match window).
    notify_buf = ""

    def on_notify(_char, data: bytearray) -> None:
        nonlocal notify_buf
        notify_buf += bytes(data).decode(errors="replace")
        while "\n" in notify_buf:
            line, notify_buf = notify_buf.split("\n", 1)
            line = line.strip()
            if line:
                log.append(line)

    print(f"Connecting to {args.address} ...", flush=True)
    async with BleakClient(args.address, timeout=20.0) as client:
        print(f"Connected: {client.is_connected}", flush=True)
        try:
            await asyncio.wait_for(client.start_notify(STATUS_CHAR, on_notify), timeout=10.0)
        except asyncio.TimeoutError:
            print("start_notify() timed out after 10s - CCCD subscribe never completed", flush=True)
            return 1
        print("Subscribed to status notifications.", flush=True)

        all_ok = True
        seq_results = await run_sequential(client, build_sequential_cases(), log, args.verbose)
        all_ok &= print_results("Sequential (one command at a time)", seq_results)

        burst_results = await run_burst(client, build_burst_cases(), log, args.verbose)
        all_ok &= print_results("Burst, write-with-response (should all pass)", burst_results)

        if args.include_unreliable_burst:
            unreliable_results = await run_burst(client, build_burst_cases_unreliable(), log, args.verbose)
            print_results(
                "Burst, write-WITHOUT-response (demonstrates the original bug class - failures here are expected, not a regression)",
                unreliable_results,
            )

        await client.stop_notify(STATUS_CHAR)

    print(f"\n{'ALL TESTS PASSED' if all_ok else 'SOME TESTS FAILED'}")
    return 0 if all_ok else 1


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", nargs="?", help="Lamp's BLE MAC address (omit with --scan)")
    parser.add_argument("--scan", action="store_true", help="List candidate lamps instead of running tests")
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("-v", "--verbose", action="store_true", help="Print every received line as it arrives")
    parser.add_argument(
        "--include-unreliable-burst",
        action="store_true",
        help="Also run the write-without-response burst, to see the original bug class reproduced (not part of pass/fail)",
    )
    args = parser.parse_args()

    if not args.scan and not args.address:
        parser.error("either an address or --scan is required")

    sys.exit(asyncio.run(main_async(args)))


if __name__ == "__main__":
    main()
