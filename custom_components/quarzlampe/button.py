"""Button entities for one-shot commands."""

from __future__ import annotations

from typing import Any

from homeassistant.components.button import ButtonEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import QuarzlampeCoordinator
from .entity import QuarzlampeEntity

BUTTON_DEFS = (
    {"name": "Next Pattern", "cmd": "next"},
    {"name": "Previous Pattern", "cmd": "prev"},
    {"name": "Status Refresh", "cmd": "status"},
    {"name": "Sync Switch", "cmd": "sync"},
)


async def async_setup_entry(
    hass: HomeAssistant, entry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: QuarzlampeCoordinator = hass.data[DOMAIN]["entries"][entry.entry_id]
    entities = [
        QuarzlampeButton(coordinator, entry.entry_id, definition)
        for definition in BUTTON_DEFS
    ]
    async_add_entities(entities)


class QuarzlampeButton(QuarzlampeEntity, ButtonEntity):
    """Stateless control mapped to a single command."""

    _attr_should_poll = False

    def __init__(
        self,
        coordinator: QuarzlampeCoordinator,
        entry_id: str,
        definition: dict[str, Any],
    ) -> None:
        super().__init__(coordinator, entry_id, definition["name"])
        self._cmd = definition["cmd"]

    @property
    def available(self) -> bool:
        return self.coordinator.client.available

    async def async_press(self) -> None:
        await self.coordinator.client.async_send_command(self._cmd)
        await self.coordinator.async_request_refresh()

