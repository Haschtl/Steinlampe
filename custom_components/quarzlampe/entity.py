"""Shared entity mixin for the Quarzlampe integration."""

from __future__ import annotations

from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DEFAULT_NAME, DOMAIN, MANUFACTURER, MODEL, TRANSPORT_BT_SERIAL
from .coordinator import QuarzlampeCoordinator


class QuarzlampeEntity(CoordinatorEntity[QuarzlampeCoordinator]):
    """Base entity providing device metadata and unique IDs."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: QuarzlampeCoordinator, entry_id: str, name: str) -> None:
        super().__init__(coordinator)
        self._entry_id = entry_id
        self._attr_name = f"{DEFAULT_NAME} {name}"
        self._attr_unique_id = f"{entry_id}-{name.lower().replace(' ', '-')}"

    @property
    def device_info(self) -> dict:
        connections = set()
        if self.coordinator.transport == TRANSPORT_BT_SERIAL and self.coordinator.serial_port:
            connections.add(("serial", self.coordinator.serial_port))
        elif self.coordinator.address:
            connections.add(("bluetooth", self.coordinator.address))
        return {
            "identifiers": {(DOMAIN, self._entry_id)},
            "name": DEFAULT_NAME,
            "manufacturer": MANUFACTURER,
            "model": MODEL,
            "connections": connections,
        }
