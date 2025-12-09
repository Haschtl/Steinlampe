"""Coordinator binding BLE client to Home Assistant entities."""

from __future__ import annotations

from datetime import timedelta
from typing import Any

from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .ble import QuarzlampeClient
from .const import DEFAULT_UPDATE_INTERVAL, DOMAIN, LOGGER


class QuarzlampeCoordinator(DataUpdateCoordinator[dict[str, Any]]):
    """DataUpdateCoordinator wrapping the BLE client."""

    def __init__(self, hass: HomeAssistant, address: str, name: str) -> None:
        self.client = QuarzlampeClient(hass, address, name)
        super().__init__(
            hass,
            LOGGER,
            name=f"{DOMAIN}-{address}",
            update_interval=timedelta(seconds=DEFAULT_UPDATE_INTERVAL),
        )

    async def _async_update_data(self) -> dict[str, Any]:
        """Poll the lamp for status."""
        try:
            return await self.client.async_request_status()
        except Exception as err:  # noqa: BLE001
            raise UpdateFailed(f"Failed to refresh Quarzlampe status: {err}") from err

