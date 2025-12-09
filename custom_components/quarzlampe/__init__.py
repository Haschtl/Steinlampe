"""Home Assistant integration for the Quarzlampe BLE lamp."""

from __future__ import annotations

import voluptuous as vol

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr

from .const import (
    DEFAULT_NAME,
    DOMAIN,
    MANUFACTURER,
    PLATFORMS,
    LOGGER,
    TRANSPORT_BLE,
    TRANSPORT_BT_SERIAL,
)
from .coordinator import QuarzlampeCoordinator

SEND_COMMAND = "send_command"
SERVICE_WAKE = "wake"
SERVICE_SLEEP = "sleep"
SERVICE_NOTIFY = "notify"
SERVICE_MORSE = "morse"
SERVICE_PRESENCE_SET = "presence_set"
SERVICE_CUSTOM_PATTERN = "custom_pattern"
SERVICE_QUICK = "quick_modes"
SERVICE_CFG_IMPORT = "config_import"
SERVICE_CFG_EXPORT = "config_export"
SERVICE_DEMO = "demo"

SERVICE_SCHEMAS = {
    SEND_COMMAND: vol.Schema(
        {vol.Required("command"): str, vol.Optional("config_entry_id"): str}
    ),
    SERVICE_WAKE: vol.Schema(
        {
            vol.Required("duration"): vol.Coerce(int),
            vol.Optional("mode"): vol.Coerce(int),
            vol.Optional("brightness"): vol.Coerce(int),
            vol.Optional("soft", default=False): bool,
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_SLEEP: vol.Schema(
        {
            vol.Optional("minutes", default=15): vol.Coerce(int),
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_NOTIFY: vol.Schema(
        {
            vol.Required("sequence"): [vol.Coerce(int)],
            vol.Optional("fade"): vol.Coerce(int),
            vol.Optional("repeat", default=1): vol.Coerce(int),
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_MORSE: vol.Schema(
        {vol.Required("text"): str, vol.Optional("config_entry_id"): str}
    ),
    SERVICE_PRESENCE_SET: vol.Schema(
        {
            vol.Optional("mac"): str,
            vol.Optional("clear", default=False): bool,
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_CUSTOM_PATTERN: vol.Schema(
        {
            vol.Optional("values", default=[]): [vol.Coerce(float)],
            vol.Optional("step_ms"): vol.Coerce(int),
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_QUICK: vol.Schema(
        {
            vol.Required("modes"): [vol.Coerce(int)],
            vol.Optional("config_entry_id"): str,
        }
    ),
    SERVICE_CFG_IMPORT: vol.Schema(
        {vol.Required("payload"): str, vol.Optional("config_entry_id"): str}
    ),
    SERVICE_CFG_EXPORT: vol.Schema({vol.Optional("config_entry_id"): str}),
    SERVICE_DEMO: vol.Schema(
        {
            vol.Optional("seconds"): vol.Coerce(int),
            vol.Optional("stop", default=False): bool,
            vol.Optional("config_entry_id"): str,
        }
    ),
}


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up a Quarzlampe config entry."""
    hass.data.setdefault(DOMAIN, {"entries": {}, "services_registered": False})
    domain_data = hass.data[DOMAIN]

    address: str = entry.data.get("address", "")
    transport: str = entry.data.get("transport", TRANSPORT_BLE)
    serial_port: str | None = entry.data.get("serial_port")
    name: str = entry.data.get("name") or DEFAULT_NAME

    coordinator = QuarzlampeCoordinator(hass, address, name, transport, serial_port)
    await coordinator.async_config_entry_first_refresh()
    domain_data["entries"][entry.entry_id] = coordinator

    # Register devices for the entry so HA shows them even before entities load.
    dev_reg = dr.async_get(hass)
    dev_reg.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, entry.entry_id)},
        name=name,
        manufacturer=MANUFACTURER,
    )

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    if not domain_data["services_registered"]:
        _register_services(hass)
        domain_data["services_registered"] = True

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    data = hass.data.get(DOMAIN, {})
    coordinator: QuarzlampeCoordinator | None = data.get("entries", {}).pop(
        entry.entry_id, None
    )
    if coordinator:
        await coordinator.client.async_disconnect()
    return unload_ok


def _get_coordinator(hass: HomeAssistant, call: ServiceCall) -> QuarzlampeCoordinator:
    domain_data = hass.data.get(DOMAIN, {})
    entries: dict[str, QuarzlampeCoordinator] = domain_data.get("entries", {})
    entry_id: str | None = call.data.get("config_entry_id")
    if entry_id:
        if entry_id not in entries:
            raise HomeAssistantError(f"No Quarzlampe entry with id {entry_id}")
        return entries[entry_id]
    if len(entries) == 1:
        return next(iter(entries.values()))
    raise HomeAssistantError(
        "Multiple Quarzlampe entries are configured; please provide config_entry_id"
    )


def _register_services(hass: HomeAssistant) -> None:
    async def handle_send_command(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        await coordinator.client.async_send_command(call.data["command"])

    async def handle_wake(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        parts: list[str] = ["wake"]
        if call.data.get("soft"):
            parts.append("soft")
        if "mode" in call.data and call.data["mode"]:
            parts.append(f"mode={int(call.data['mode'])}")
        if "brightness" in call.data and call.data["brightness"]:
            parts.append(f"bri={int(call.data['brightness'])}")
        parts.append(str(max(1, int(call.data["duration"]))))
        await coordinator.client.async_send_command(" ".join(parts))

    async def handle_sleep(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        minutes = int(call.data.get("minutes", 15))
        if minutes <= 0:
            await coordinator.client.async_send_command("sleep stop")
        else:
            await coordinator.client.async_send_command(f"sleep {minutes}")

    async def handle_notify(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        seq = [int(v) for v in call.data.get("sequence", []) if int(v) > 0]
        repeat = max(1, int(call.data.get("repeat", 1)))
        expanded = seq * repeat
        if not expanded:
            raise HomeAssistantError("sequence must contain at least one positive value")
        cmd = f"notify {' '.join(str(v) for v in expanded)}"
        fade = call.data.get("fade")
        if fade:
            cmd += f" fade={int(fade)}"
        await coordinator.client.async_send_command(cmd)

    async def handle_morse(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        await coordinator.client.async_send_command(f"morse {call.data['text']}")

    async def handle_presence(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        if call.data.get("clear"):
            await coordinator.client.async_send_command("presence clear")
            return
        mac = call.data.get("mac")
        if mac:
            await coordinator.client.async_send_command(f"presence set {mac}")
        else:
            await coordinator.client.async_send_command("presence set me")

    async def handle_custom(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        values = call.data.get("values") or []
        cleaned = [min(1.0, max(0.0, float(v))) for v in values]
        if cleaned:
            csv = ",".join(f"{v:.3f}".rstrip("0").rstrip(".") for v in cleaned)
            await coordinator.client.async_send_command(f"custom {csv}")
        if call.data.get("step_ms"):
            await coordinator.client.async_send_command(
                f"custom step {int(call.data['step_ms'])}"
            )

    async def handle_quick(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        modes = [int(v) for v in call.data.get("modes", []) if int(v) > 0]
        if not modes:
            raise HomeAssistantError("modes must not be empty")
        await coordinator.client.async_send_command(f"quick {','.join(map(str, modes))}")

    async def handle_cfg_import(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        await coordinator.client.async_send_command(f"cfg import {call.data['payload']}")

    async def handle_cfg_export(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        await coordinator.client.async_send_command("cfg export")

    async def handle_demo(call: ServiceCall) -> None:
        coordinator = _get_coordinator(hass, call)
        if call.data.get("stop"):
            await coordinator.client.async_send_command("demo off")
            return
        seconds = call.data.get("seconds")
        if seconds is None:
            await coordinator.client.async_send_command("demo")
        else:
            await coordinator.client.async_send_command(f"demo {int(seconds)}")

    hass.services.async_register(
        DOMAIN, SEND_COMMAND, handle_send_command, schema=SERVICE_SCHEMAS[SEND_COMMAND]
    )
    hass.services.async_register(
        DOMAIN, SERVICE_WAKE, handle_wake, schema=SERVICE_SCHEMAS[SERVICE_WAKE]
    )
    hass.services.async_register(
        DOMAIN, SERVICE_SLEEP, handle_sleep, schema=SERVICE_SCHEMAS[SERVICE_SLEEP]
    )
    hass.services.async_register(
        DOMAIN, SERVICE_NOTIFY, handle_notify, schema=SERVICE_SCHEMAS[SERVICE_NOTIFY]
    )
    hass.services.async_register(
        DOMAIN, SERVICE_MORSE, handle_morse, schema=SERVICE_SCHEMAS[SERVICE_MORSE]
    )
    hass.services.async_register(
        DOMAIN,
        SERVICE_PRESENCE_SET,
        handle_presence,
        schema=SERVICE_SCHEMAS[SERVICE_PRESENCE_SET],
    )
    hass.services.async_register(
        DOMAIN,
        SERVICE_CUSTOM_PATTERN,
        handle_custom,
        schema=SERVICE_SCHEMAS[SERVICE_CUSTOM_PATTERN],
    )
    hass.services.async_register(
        DOMAIN, SERVICE_QUICK, handle_quick, schema=SERVICE_SCHEMAS[SERVICE_QUICK]
    )
    hass.services.async_register(
        DOMAIN,
        SERVICE_CFG_IMPORT,
        handle_cfg_import,
        schema=SERVICE_SCHEMAS[SERVICE_CFG_IMPORT],
    )
    hass.services.async_register(
        DOMAIN,
        SERVICE_CFG_EXPORT,
        handle_cfg_export,
        schema=SERVICE_SCHEMAS[SERVICE_CFG_EXPORT],
    )
    hass.services.async_register(
        DOMAIN, SERVICE_DEMO, handle_demo, schema=SERVICE_SCHEMAS[SERVICE_DEMO]
    )
    LOGGER.debug("Registered Quarzlampe services")
