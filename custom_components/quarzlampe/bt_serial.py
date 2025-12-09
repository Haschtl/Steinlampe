"""Bluetooth Serial (SPP) client using an existing rfcomm/serial device."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

import serial_asyncio
from homeassistant.core import HomeAssistant

from .const import DEFAULT_BT_BAUD, LOGGER
from .status_store import LampStatusStore


class QuarzlampeBtSerialClient:
    """Classic BT serial client. Expects the device to be exposed as a serial port (e.g. /dev/rfcomm0)."""

    def __init__(self, hass: HomeAssistant, port: str, baud: int = DEFAULT_BT_BAUD) -> None:
        self.hass = hass
        self.port = port
        self.baud = baud
        self._store = LampStatusStore()
        self._writer: asyncio.StreamWriter | None = None
        self._reader: asyncio.StreamReader | None = None
        self._task: asyncio.Task | None = None
        self._connected = False
        self._lock = asyncio.Lock()

    @property
    def status(self) -> dict[str, Any]:
        return self._store.data

    @property
    def available(self) -> bool:
        return self._connected

    async def async_connect(self) -> None:
        async with self._lock:
            if self._connected:
                return
            try:
                self._reader, self._writer = await serial_asyncio.open_serial_connection(
                    url=self.port, baudrate=self.baud
                )
            except Exception as err:  # noqa: BLE001
                raise ConnectionError(f"Failed to open serial port {self.port}: {err}") from err
            self._connected = True
            self._task = asyncio.create_task(self._read_loop())
            await self.async_send_command("status")

    async def async_disconnect(self) -> None:
        self._connected = False
        if self._task:
            self._task.cancel()
            self._task = None
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:  # noqa: BLE001
                pass
        self._writer = None
        self._reader = None

    async def _read_loop(self) -> None:
        assert self._reader
        try:
            while True:
                line = await self._reader.readline()
                if not line:
                    raise ConnectionError("serial EOF")
                text = line.decode("utf-8", errors="ignore").strip()
                if text:
                    if not self._store.handle_line(text):
                        LOGGER.debug("Unparsed serial line: %s", text)
        except asyncio.CancelledError:
            return
        except Exception as err:  # noqa: BLE001
            LOGGER.debug("Serial read loop stopped: %s", err)
        finally:
            self._connected = False

    async def async_send_command(self, command: str) -> None:
        await self.async_connect()
        if not self._writer:
            raise ConnectionError("Serial writer not available")
        payload = (command.strip() + "\n").encode("utf-8")
        self._writer.write(payload)
        await self._writer.drain()

    async def async_request_status(self) -> dict[str, Any]:
        self._store.clear_event()
        await self.async_send_command("status")
        await self._store.wait_for_update(timeout=2.0)
        return self.status

