"""Switch entities mapping to lamp toggles."""

from __future__ import annotations

from typing import Any, Callable

from homeassistant.components.switch import SwitchEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import QuarzlampeCoordinator
from .entity import QuarzlampeEntity

SWITCH_DEFS: tuple[dict[str, Any], ...] = (
    {
        "key": "auto",
        "name": "Auto Cycle",
        "cmd_on": "auto on",
        "cmd_off": "auto off",
    },
    {
        "key": "touch_dim",
        "name": "Touch Dim",
        "cmd_on": "touchdim on",
        "cmd_off": "touchdim off",
    },
    {
        "key": "presence",
        "name": "Presence",
        "cmd_on": "presence on",
        "cmd_off": "presence off",
        "available_key": "has_presence",
        "is_on": lambda v: isinstance(v, str)
        and v.upper().startswith("ON"),
    },
    {
        "key": "light_enabled",
        "name": "Ambient Light Sensor",
        "cmd_on": "light on",
        "cmd_off": "light off",
        "available_key": "has_light",
    },
    {
        "key": "clap",
        "name": "Clap Detection",
        "cmd_on": "clap on",
        "cmd_off": "clap off",
        "available_key": "has_music",
    },
)


async def async_setup_entry(
    hass: HomeAssistant, entry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: QuarzlampeCoordinator = hass.data[DOMAIN]["entries"][entry.entry_id]
    entities: list[QuarzlampeSwitch] = [
        QuarzlampeSwitch(coordinator, entry.entry_id, definition)
        for definition in SWITCH_DEFS
    ]
    async_add_entities(entities)


class QuarzlampeSwitch(QuarzlampeEntity, SwitchEntity):
    """Switch for a single lamp toggle command."""

    def __init__(
        self, coordinator: QuarzlampeCoordinator, entry_id: str, definition: dict[str, Any]
    ) -> None:
        super().__init__(coordinator, entry_id, definition["name"])
        self._definition = definition

    @property
    def available(self) -> bool:
        if not self.coordinator.client.available:
            return False
        key = self._definition.get("available_key")
        if key is None:
            return True
        return self.coordinator.data.get(key) is not False

    @property
    def is_on(self) -> bool | None:
        val = self.coordinator.data.get(self._definition["key"])
        custom = self._definition.get("is_on")
        if custom:
            return custom(val)
        if val is None:
            return None
        if isinstance(val, str):
            return val.upper() in {"ON", "1", "TRUE"}
        return bool(val)

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self.coordinator.client.async_send_command(self._definition["cmd_on"])
        await self.coordinator.async_request_refresh()

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.coordinator.client.async_send_command(self._definition["cmd_off"])
        await self.coordinator.async_request_refresh()

