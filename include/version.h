#pragma once

/**
 * @file version.h
 * @brief Single source of truth for the firmware version, read by both the firmware
 * (STATUS| fw_ver/fw_env) and scripts/copy-firmware.mjs (via regex, no codegen).
 * Bump FIRMWARE_VERSION manually per release. FIRMWARE_ENV is set per PlatformIO
 * environment via build_flags (see platformio.ini) and identifies which variant this
 * build is, so the web UI can match it against the right bundled OTA image.
 */

#define FIRMWARE_VERSION "1.0.0"

#ifndef FIRMWARE_ENV
#define FIRMWARE_ENV "unknown"
#endif
