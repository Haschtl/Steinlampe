"""Config flow for the Quarzlampe integration."""

from __future__ import annotations

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import HomeAssistant
from homeassistant.helpers import config_validation as cv

from .const import (
    DEFAULT_NAME,
    DOMAIN,
    TRANSPORT_BLE,
    TRANSPORT_BT_SERIAL,
)


class QuarzlampeConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow."""

    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors: dict[str, str] = {}
        if user_input is not None:
            address = user_input.get("address", "").strip()
            name = user_input.get("name") or DEFAULT_NAME
            transport = user_input.get("transport", TRANSPORT_BLE)
            serial_port = user_input.get("serial_port")
            unique = address or serial_port or name
            await self.async_set_unique_id(unique.lower())
            self._abort_if_unique_id_configured()
            return self.async_create_entry(
                title=name,
                data={
                    "address": address,
                    "name": name,
                    "transport": transport,
                    "serial_port": serial_port,
                },
            )

        data_schema = vol.Schema(
            {
                vol.Required("transport", default=TRANSPORT_BLE): vol.In(
                    {TRANSPORT_BLE: "BLE", TRANSPORT_BT_SERIAL: "BT Serial (/dev/rfcomm...)"}
                ),
                vol.Optional("address"): cv.string,
                vol.Optional("serial_port", default="/dev/rfcomm0"): cv.string,
                vol.Optional("name", default=DEFAULT_NAME): cv.string,
            }
        )
        return self.async_show_form(step_id="user", data_schema=data_schema, errors=errors)

    async def async_step_import(self, user_input):
        """Handle import from YAML (for completeness)."""
        return await self.async_step_user(user_input)
