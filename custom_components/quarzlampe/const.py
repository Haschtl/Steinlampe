"""Constants for the Quarzlampe Home Assistant integration."""

from __future__ import annotations

from logging import getLogger

DOMAIN = "quarzlampe"
DEFAULT_NAME = "Quarzlampe"
MANUFACTURER = "Quarzlampe"
MODEL = "ESP32 PWM Lamp"

TRANSPORT_BLE = "ble"
TRANSPORT_BT_SERIAL = "bt_serial"

BLE_SERVICE_UUID = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b"
BLE_COMMAND_CHAR = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7"
BLE_STATUS_CHAR = "c5ad78b6-9b77-4a96-9a42-8e6e9a40c123"

PLATFORMS: list[str] = ["light", "switch", "number", "sensor", "button"]

# Keep update interval short enough to stay in sync but not spam the lamp.
DEFAULT_UPDATE_INTERVAL = 30
DEFAULT_BT_BAUD = 115200

# Pattern labels mirror frontend/src/data/patterns.ts
PATTERN_LABELS = [
    "Konstant",
    "Atmung",
    "Atmung Warm",
    "Atmung 2",
    "Sinus",
    "Zig-Zag",
    "Saegezahn",
    "Pulsierend",
    "Heartbeat",
    "Heartbeat Alarm",
    "Comet",
    "Aurora",
    "Strobo",
    "Polizei DE",
    "Camera",
    "TV Static",
    "HAL-9000",
    "Funkeln",
    "Kerze Soft",
    "Kerze",
    "Lagerfeuer",
    "Stufen",
    "Zwinkern",
    "Gluehwuermchen",
    "Popcorn",
    "Leuchtstoffroehre",
    "Weihnacht",
    "Saber Idle",
    "Saber Clash",
    "Emergency Bridge",
    "Arc Reactor",
    "Warp Core",
    "KITT Scanner",
    "Tron Grid",
    "Oellaterne",
    "Gaslicht",
    "Neon",
    "Dimmer Glow",
    "Fackel",
    "Gewitter",
    "Distant Storm",
    "Rolling Thunder",
    "Heat Lightning",
    "Strobe Front",
    "Sheet Lightning",
    "Mixed Storm",
    "Sonnenuntergang",
    "Gamma Probe",
    "Alert",
    "SOS",
    "Custom",
    "Musik",
]

LOGGER = getLogger(DOMAIN)
