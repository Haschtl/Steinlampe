"""Light entity for the Quarzlampe lamp."""

from __future__ import annotations

from typing import Any

from homeassistant.components.light import (
    ATTR_BRIGHTNESS,
    ATTR_EFFECT,
    ColorMode,
    LightEntity,
    LightEntityFeature,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN, PATTERN_LABELS
from .coordinator import QuarzlampeCoordinator
from .entity import QuarzlampeEntity


async def async_setup_entry(
    hass: HomeAssistant, entry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: QuarzlampeCoordinator = hass.data[DOMAIN]["entries"][entry.entry_id]
    async_add_entities([QuarzlampeLight(coordinator, entry.entry_id)])


class QuarzlampeLight(QuarzlampeEntity, LightEntity):
    """Represents the lamp as a dimmable light with effects."""

    _attr_supported_features = LightEntityFeature.EFFECT
    _attr_supported_color_modes = {ColorMode.BRIGHTNESS}
    _attr_color_mode = ColorMode.BRIGHTNESS
    _attr_effect_list = PATTERN_LABELS

    def __init__(self, coordinator: QuarzlampeCoordinator, entry_id: str) -> None:
        super().__init__(coordinator, entry_id, "Lamp")

    @property
    def available(self) -> bool:
        return self.coordinator.client.available

    @property
    def is_on(self) -> bool | None:
        lamp = self.coordinator.data.get("lamp")
        if lamp is None:
            return None
        return str(lamp).upper() == "ON"

    @property
    def brightness(self) -> int | None:
        bri = self.coordinator.data.get("brightness")
        if bri is None:
            return None
        # firmware reports 0..100 percent
        return int(max(1, min(255, round(float(bri) * 2.55))))

    @property
    def effect(self) -> str | None:
        pat = self.coordinator.data.get("pattern")
        name = self.coordinator.data.get("pattern_name")
        if isinstance(pat, int):
            idx = pat - 1
            if 0 <= idx < len(PATTERN_LABELS):
                return name or PATTERN_LABELS[idx]
        return name

    async def async_turn_on(self, **kwargs: Any) -> None:
        commands: list[str] = []
        if ATTR_BRIGHTNESS in kwargs:
            percent = round(int(kwargs[ATTR_BRIGHTNESS]) / 2.55)
            commands.append(f"bri {percent}")
        if ATTR_EFFECT in kwargs:
            effect = kwargs[ATTR_EFFECT]
            idx = None
            if isinstance(effect, str) and effect in PATTERN_LABELS:
                idx = PATTERN_LABELS.index(effect) + 1
            elif isinstance(effect, str) and effect.isdigit():
                idx = int(effect)
            if idx:
                commands.append(f"mode {idx}")
        if not commands:
            commands.append("on")
        for cmd in commands:
            await self.coordinator.client.async_send_command(cmd)
        await self.coordinator.async_request_refresh()

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.coordinator.client.async_send_command("off")
        await self.coordinator.async_request_refresh()

