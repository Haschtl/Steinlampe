"""Shared status parser for Quarzlampe notify lines."""

from __future__ import annotations

import asyncio
import re
import time
from typing import Any


def _as_float(val: str | None) -> float | None:
    if val is None:
        return None
    try:
        return float(val)
    except (TypeError, ValueError):
        return None


def _as_int(val: str | None) -> int | None:
    if val is None:
        return None
    try:
        return int(val)
    except (TypeError, ValueError):
        return None


class LampStatusStore:
    """Parses incoming status/notify lines and stores the latest state."""

    def __init__(self) -> None:
        self.data: dict[str, Any] = {}
        self.last_lines: list[str] = []
        self.last_status_ts: float | None = None
        self._event = asyncio.Event()

    def clear_event(self) -> None:
        self._event.clear()

    async def wait_for_update(self, timeout: float = 2.0) -> bool:
        try:
            await asyncio.wait_for(self._event.wait(), timeout)
            return True
        except asyncio.TimeoutError:
            return False

    def _notify_update(self) -> None:
        self.last_status_ts = time.time()
        self._event.set()

    def handle_line(self, line: str) -> bool:
        line = line.strip()
        if not line:
            return False

        self.last_lines.append(line)
        # keep the log short
        self.last_lines = self.last_lines[-40:]

        handled = False
        if line.startswith("STATUS|"):
            handled = True
            parts = line.split("|")[1:]
            kv: dict[str, str] = {}
            for part in parts:
                if "=" in part:
                    key, val = part.split("=", 1)
                    kv[key] = val

            def is_on(key: str) -> bool | None:
                if key not in kv:
                    return None
                val = kv[key].upper()
                if val == "N/A":
                    return None
                return val in {"ON", "1", "TRUE"}

            def int_or_none(key: str) -> int | None:
                return _as_int(kv.get(key))

            def float_or_none(key: str) -> float | None:
                return _as_float(kv.get(key))

            has_light = kv.get("light", "").upper() != "N/A" if "light" in kv else None
            has_music = kv.get("music", "").upper() != "N/A" if "music" in kv else None
            has_poti = kv.get("poti", "").upper() != "N/A" if "poti" in kv else None
            has_push = kv.get("push", "").upper() != "N/A" if "push" in kv else None
            has_presence = kv.get("presence", "").upper() != "N/A" if "presence" in kv else None
            has_touch = kv.get("touch", "").upper() != "N/A" if "touch" in kv else None

            self.data.update(
                {
                    "pattern": int_or_none("pattern"),
                    "pattern_total": int_or_none("pattern_total"),
                    "pattern_name": kv.get("pattern_name"),
                    "pattern_elapsed_ms": int_or_none("pat_ms"),
                    "auto": kv.get("auto") == "1",
                    "brightness": float_or_none("bri"),
                    "lamp": kv.get("lamp"),
                    "switch": kv.get("switch"),
                    "touch_dim": kv.get("touch_dim") == "1" if "touch_dim" in kv else None,
                    "touch_dim_step": float_or_none("touch_dim_step"),
                    "ramp_on_ms": int_or_none("ramp_on_ms"),
                    "ramp_off_ms": int_or_none("ramp_off_ms"),
                    "ramp_on_ease": kv.get("ramp_on_ease"),
                    "ramp_off_ease": kv.get("ramp_off_ease"),
                    "ramp_on_pow": float_or_none("ramp_on_pow"),
                    "ramp_off_pow": float_or_none("ramp_off_pow"),
                    "ramp_amb": float_or_none("ramp_amb"),
                    "idle_min": int_or_none("idle_min"),
                    "pattern_speed": float_or_none("pat_speed"),
                    "pattern_fade": None
                    if kv.get("pat_fade") == "off"
                    else float_or_none("pat_fade"),
                    "pattern_margin_low": float_or_none("pat_lo"),
                    "pattern_margin_high": float_or_none("pat_hi"),
                    "quick": kv.get("quick"),
                    "presence": kv.get("presence"),
                    "custom_len": int_or_none("custom_len"),
                    "custom_step_ms": int_or_none("custom_step_ms"),
                    "demo": kv.get("demo") == "ON" if "demo" in kv else None,
                    "gamma": float_or_none("gamma"),
                    "bri_min": float_or_none("bri_min"),
                    "bri_max": float_or_none("bri_max"),
                    "has_light": has_light,
                    "light_enabled": is_on("light"),
                    "light_gain": float_or_none("light_gain"),
                    "light_min": float_or_none("light_min"),
                    "light_max": float_or_none("light_max"),
                    "light_alpha": float_or_none("light_alpha"),
                    "light_raw": float_or_none("light_raw"),
                    "light_raw_min": float_or_none("light_raw_min"),
                    "light_raw_max": float_or_none("light_raw_max"),
                    "has_music": has_music,
                    "music_enabled": is_on("music"),
                    "music_gain": float_or_none("music_gain"),
                    "music_auto": kv.get("music_auto") == "ON"
                    if "music_auto" in kv
                    else None,
                    "music_thr": float_or_none("music_thr"),
                    "music_mode": kv.get("music_mode"),
                    "music_mod": float_or_none("music_mod"),
                    "music_kick_ms": float_or_none("music_kick_ms"),
                    "music_env": float_or_none("music_env"),
                    "music_level": float_or_none("music_level"),
                    "music_smooth": float_or_none("music_smooth"),
                    "clap": is_on("clap"),
                    "clap_thr": float_or_none("clap_thr"),
                    "clap_cool": int_or_none("clap_cool"),
                    "clap_cmd1": kv.get("clap_cmd1"),
                    "clap_cmd2": kv.get("clap_cmd2"),
                    "clap_cmd3": kv.get("clap_cmd3"),
                    "has_poti": has_poti,
                    "poti_enabled": is_on("poti"),
                    "poti_alpha": float_or_none("poti_alpha"),
                    "poti_delta": float_or_none("poti_delta"),
                    "poti_off": float_or_none("poti_off"),
                    "poti_sample": int_or_none("poti_sample"),
                    "has_push": has_push,
                    "push_enabled": is_on("push"),
                    "push_db": int_or_none("push_db"),
                    "push_dbl": int_or_none("push_dbl"),
                    "push_hold": int_or_none("push_hold"),
                    "push_step_ms": int_or_none("push_step_ms"),
                    "push_step": float_or_none("push_step"),
                    "has_presence": has_presence,
                    "has_touch": has_touch,
                }
            )
        elif line.startswith("SENSORS|"):
            handled = True
            parts = line.split("|")[1:]
            kv: dict[str, str] = {}
            for part in parts:
                if "=" in part:
                    key, val = part.split("=", 1)
                    kv[key] = val
            self.data.update(
                {
                    "touch_base": _as_int(kv.get("touch_base")),
                    "touch_raw": _as_int(kv.get("touch_raw")),
                    "touch_delta": _as_int(kv.get("touch_delta")),
                    "touch_active": kv.get("touch_active") == "1"
                    if "touch_active" in kv
                    else None,
                    "light_raw": _as_float(kv.get("light_raw")),
                    "light_raw_min": _as_float(kv.get("light_min")),
                    "light_raw_max": _as_float(kv.get("light_max")),
                    "light_amb_mult": _as_float(kv.get("light_amb_mult")),
                    "ramp_amb": _as_float(kv.get("ramp_amb"))
                    or self.data.get("ramp_amb"),
                    "music_env": _as_float(kv.get("music_env")),
                    "music_level": _as_float(kv.get("music_level")),
                    "music_smooth": _as_float(kv.get("music_smooth")),
                    "music_kick_ms": _as_float(kv.get("music_kick_ms")),
                }
            )
        elif line.startswith("[Clap]"):
            handled = True
            lower = line.lower()
            thr = re.search(r"thr=([0-9.]+)", lower)
            cool = re.search(r"cool=([0-9]+)", lower)
            self.data.update(
                {
                    "clap": "on" in lower,
                    "clap_thr": float(thr.group(1)) if thr else self.data.get("clap_thr"),
                    "clap_cool": int(cool.group(1)) if cool else self.data.get("clap_cool"),
                }
            )
        elif line.startswith("[Touch]"):
            handled = True
            base = re.search(r"base=([0-9]+)", line)
            raw = re.search(r"raw=([0-9]+)", line)
            delta = re.search(r"delta=([-0-9]+)", line)
            thr_on = re.search(r"thrOn=([0-9]+)", line)
            thr_off = re.search(r"thrOff=([0-9]+)", line)
            self.data.update(
                {
                    "touch_base": int(base.group(1)) if base else self.data.get("touch_base"),
                    "touch_raw": int(raw.group(1)) if raw else self.data.get("touch_raw"),
                    "touch_delta": int(delta.group(1))
                    if delta
                    else self.data.get("touch_delta"),
                    "touch_thr_on": int(thr_on.group(1))
                    if thr_on
                    else self.data.get("touch_thr_on"),
                    "touch_thr_off": int(thr_off.group(1))
                    if thr_off
                    else self.data.get("touch_thr_off"),
                    "touch_active": "active=1" in line,
                }
            )
        elif line.startswith("[Quick]"):
            handled = True
            self.data["quick"] = line.replace("[Quick]", "").strip()
        elif line.startswith("[Custom]"):
            handled = True
            len_match = re.search(r"len=([0-9]+)", line)
            step_match = re.search(r"stepMs=([0-9]+)", line)
            self.data.update(
                {
                    "custom_len": int(len_match.group(1))
                    if len_match
                    else self.data.get("custom_len"),
                    "custom_step_ms": int(step_match.group(1))
                    if step_match
                    else self.data.get("custom_step_ms"),
                }
            )
        elif line.startswith("CUSTOM|"):
            handled = True
            parts = line.split("|")[1:]
            kv: dict[str, str] = {}
            for part in parts:
                if "=" in part:
                    key, val = part.split("=", 1)
                    kv[key] = val
            self.data.update(
                {
                    "custom_len": _as_int(kv.get("len")) or self.data.get("custom_len"),
                    "custom_step_ms": _as_int(kv.get("step"))
                    or self.data.get("custom_step_ms"),
                    "custom_csv": kv.get("vals") or self.data.get("custom_csv"),
                }
            )
        elif line.startswith("Pattern "):
            handled = True
            mode = re.search(r"Pattern\s+([0-9]+)/([0-9]+)", line)
            name_match = re.search(r"'([^']+)'", line)
            auto_match = re.search(r"AutoCycle=(ON|OFF)", line)
            self.data.update(
                {
                    "pattern": int(mode.group(1)) if mode else self.data.get("pattern"),
                    "pattern_total": int(mode.group(2))
                    if mode
                    else self.data.get("pattern_total"),
                    "pattern_name": name_match.group(1) if name_match else self.data.get("pattern_name"),
                    "auto": auto_match.group(1) == "ON" if auto_match else self.data.get("auto"),
                }
            )
        elif line.startswith("Presence="):
            handled = True
            self.data["presence"] = line.replace("Presence=", "").strip()
        elif line.startswith("Device="):
            handled = True
            # Example: Device=XX:YY | Service=... | Cmd=... | Status=...
            mac = line.split("|")[0].replace("Device=", "").strip()
            if mac:
                self.data["ble_mac"] = mac

        if handled:
            self._notify_update()
        return handled
