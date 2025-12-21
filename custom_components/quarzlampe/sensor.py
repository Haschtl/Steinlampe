"""Sensors for telemetry coming from the lamp."""

from __future__ import annotations

from homeassistant.components.sensor import SensorEntity, SensorEntityDescription
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import DOMAIN
from .coordinator import QuarzlampeCoordinator
from .entity import QuarzlampeEntity

SENSORS: tuple[SensorEntityDescription, ...] = (
    SensorEntityDescription(key="pattern", name="Pattern Index"),
    SensorEntityDescription(key="pattern_name", name="Pattern Name"),
    SensorEntityDescription(key="light_raw", name="Light Raw"),
    SensorEntityDescription(key="music_level", name="Music Level"),
    SensorEntityDescription(key="touch_delta", name="Touch Delta"),
    SensorEntityDescription(key="presence", name="Presence"),
    SensorEntityDescription(key="host_ble_available", name="Host BLE Available"),
)


async def async_setup_entry(
    hass: HomeAssistant, entry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator: QuarzlampeCoordinator = hass.data[DOMAIN]["entries"][entry.entry_id]
    entities = [
        QuarzlampeSensor(coordinator, entry.entry_id, description)
        for description in SENSORS
    ]
    async_add_entities(entities)


class QuarzlampeSensor(QuarzlampeEntity, SensorEntity):
    """Read-only view of lamp telemetry."""

    entity_description: SensorEntityDescription

    def __init__(
        self,
        coordinator: QuarzlampeCoordinator,
        entry_id: str,
        description: SensorEntityDescription,
    ) -> None:
        super().__init__(coordinator, entry_id, description.name)
        self.entity_description = description

    @property
    def available(self) -> bool:
        return self.coordinator.client.available

    @property
    def native_value(self):
        return self.coordinator.data.get(self.entity_description.key)
