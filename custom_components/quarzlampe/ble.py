"""BLE client and status parser for the Quarzlampe lamp."""

from __future__ import annotations

import asyncio
from typing import Any, Callable

from bleak_retry_connector import BleakClientWithServiceCache, establish_connection
from homeassistant.components.bluetooth import async_ble_device_from_address
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryNotReady

from .const import BLE_COMMAND_CHAR, BLE_STATUS_CHAR, LOGGER
from .status_store import LampStatusStore


class QuarzlampeClient:
    """Handles BLE connection, notifications, and command writes."""

    def __init__(self, hass: HomeAssistant, address: str, name: str) -> None:
        self.hass = hass
        self.address = address
        self.name = name
        self._client: BleakClientWithServiceCache | None = None
        self._store = LampStatusStore()
        self._connect_lock = asyncio.Lock()
        self._connected = False

    @property
    def status(self) -> dict[str, Any]:
        return self._store.data

    @property
    def available(self) -> bool:
        return self._connected

    async def async_connect(self) -> None:
        """Connect to the lamp and subscribe to notifications."""
        async with self._connect_lock:
            if self._client and self._client.is_connected:
                return
            ble_device = async_ble_device_from_address(
                self.hass, self.address, connectable=True
            )
            if not ble_device:
                raise ConfigEntryNotReady(
                    f"BLE device {self.address} not seen by Home Assistant yet"
                )
            LOGGER.debug("Connecting to Quarzlampe at %s", ble_device.address)
            self._client = await establish_connection(
                BleakClientWithServiceCache,
                ble_device,
                ble_device.address,
                self._handle_disconnect,
            )
            await self._client.start_notify(BLE_STATUS_CHAR, self._handle_notification)
            self._connected = True
            # Kick off a status request to arm feedback and fill the store.
            await self.async_send_command("status")

    async def async_disconnect(self) -> None:
        """Disconnect the BLE client."""
        if self._client and self._client.is_connected:
            try:
                await self._client.stop_notify(BLE_STATUS_CHAR)
            except Exception:  # noqa: BLE001
                pass
            await self._client.disconnect()
        self._connected = False
        self._client = None

    def _handle_disconnect(self, client: BleakClientWithServiceCache) -> None:
        LOGGER.debug("BLE disconnected from %s", self.address)
        self._connected = False

    def _handle_notification(self, _char: Any, data: bytearray) -> None:
        try:
            text = data.decode("utf-8", errors="ignore")
        except Exception:  # noqa: BLE001
            return
        for raw_line in text.replace("\r", "\n").split("\n"):
            line = raw_line.strip()
            if line:
                if not self._store.handle_line(line):
                    LOGGER.debug("Unparsed notify line: %s", line)

    async def async_send_command(self, command: str) -> None:
        """Send a text command terminated by newline."""
        await self.async_connect()
        if not self._client or not self._client.is_connected:
            raise ConfigEntryNotReady("BLE client is not connected")
        payload = (command.strip() + "\n").encode("utf-8")
        await self._client.write_gatt_char(BLE_COMMAND_CHAR, payload, response=True)

    async def async_request_status(self) -> dict[str, Any]:
        """Trigger a status snapshot and wait briefly for notify."""
        self._store.clear_event()
        await self.async_send_command("status")
        await self._store.wait_for_update(timeout=2.0)
        return self.status
