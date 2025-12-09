"""Number entities for tuning lamp parameters."""

from __future__ import annotations

from typing import Any, Callable

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import QuarzlampeCoordinator
from .entity import QuarzlampeEntity


def _pct_to_unit(val: float | None) -> float | None:
    return None if val is None else float(val)


NUMBER_DEFS: tuple[dict[str, Any], ...] = (
    {
        "key": "cap",
        "name": "Brightness Cap (%)",
        "min": 1,
        "max": 100,
        "step": 1,
        "mode": NumberMode.SLIDER,
        "get": _pct_to_unit,
        "cmd": lambda v: f"bri cap {v:.1f}",
    },
    {
        "key": "bri_min",
        "name": "Brightness Min (%)",
        "min": 0,
        "max": 100,
        "step": 1,
        "mode": NumberMode.SLIDER,
        "get": _pct_to_unit,
        "cmd": lambda v: f"bri min {v/100:.3f}",
    },
    {
        "key": "bri_max",
        "name": "Brightness Max (%)",
        "min": 0,
        "max": 100,
        "step": 1,
        "mode": NumberMode.SLIDER,
        "get": _pct_to_unit,
        "cmd": lambda v: f"bri max {v/100:.3f}",
    },
    {
        "key": "ramp_on_ms",
        "name": "Ramp On (ms)",
        "min": 50,
        "max": 10000,
        "step": 10,
        "cmd": lambda v: f"ramp on {int(v)}",
    },
    {
        "key": "ramp_off_ms",
        "name": "Ramp Off (ms)",
        "min": 50,
        "max": 10000,
        "step": 10,
        "cmd": lambda v: f"ramp off {int(v)}",
    },
    {
        "key": "ramp_amb",
        "name": "Ambient Ramp Factor",
        "min": 0,
        "max": 5,
        "step": 0.05,
        "cmd": lambda v: f"ramp ambient {v:.2f}",
    },
    {
        "key": "idle_min",
        "name": "Idle Off (minutes)",
        "min": 0,
        "max": 240,
        "step": 1,
        "cmd": lambda v: f"idleoff {int(v)}",
    },
    {
        "key": "pattern_speed",
        "name": "Pattern Speed",
        "min": 0.1,
        "max": 5,
        "step": 0.05,
        "cmd": lambda v: f"pat scale {v:.2f}",
    },
    {
        "key": "pattern_fade",
        "name": "Pattern Fade Amount",
        "min": 0,
        "max": 5,
        "step": 0.05,
        "cmd": "pat_fade",  # handled specially
    },
    {
        "key": "gamma",
        "name": "PWM Gamma",
        "min": 0.5,
        "max": 4.0,
        "step": 0.05,
        "cmd": lambda v: f"pwm curve {v:.2f}",
    },
)


async def async_setup_entry(
    hass: HomeAssistant, entry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: QuarzlampeCoordinator = hass.data[DOMAIN]["entries"][entry.entry_id]
    entities: list[QuarzlampeNumber] = [
        QuarzlampeNumber(coordinator, entry.entry_id, definition)
        for definition in NUMBER_DEFS
    ]
    async_add_entities(entities)


class QuarzlampeNumber(QuarzlampeEntity, NumberEntity):
    """Number entity bound to a specific text command."""

    _attr_should_poll = False

    def __init__(
        self,
        coordinator: QuarzlampeCoordinator,
        entry_id: str,
        definition: dict[str, Any],
    ) -> None:
        super().__init__(coordinator, entry_id, definition["name"])
        self._definition = definition
        self._attr_native_min_value = definition["min"]
        self._attr_native_max_value = definition["max"]
        self._attr_native_step = definition["step"]
        if "mode" in definition:
            self._attr_mode = definition["mode"]

    @property
    def available(self) -> bool:
        return self.coordinator.client.available

    @property
    def native_value(self) -> float | None:
        val = self.coordinator.data.get(self._definition["key"])
        if self._definition["key"] == "pattern_fade" and val is None:
            return 0
        if not self.available:
            return None
        getter: Callable[[float | None], float | None] | None = self._definition.get(
            "get"
        )
        return getter(val) if getter else val

    async def async_set_native_value(self, value: float) -> None:
        cmd = self._definition["cmd"]
        if cmd == "pat_fade":
            if value <= 0:
                await self.coordinator.client.async_send_command("pat fade off")
            else:
                await self.coordinator.client.async_send_command("pat fade on")
                await self.coordinator.client.async_send_command(
                    f"pat fade amt {value:.2f}"
                )
        else:
            await self.coordinator.client.async_send_command(cmd(value))
        await self.coordinator.async_request_refresh()
