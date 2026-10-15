# Multi-Channel Feature Plan

Status: draft / not started
Scope: firmware (`src/`, `include/`), persistence, status protocol, Home Assistant integration (`custom_components/quarzlampe`), web frontend (`frontend/`)

## 1. Goal

Support N independently controllable LED output channels per lamp (brightness, pattern, ramp/easing all per channel), instead of the current single global output. Driven by two things:

- New lamp variants will carry multiple constant-current driver stages (as many boost circuits as the board design calls for — this is a per-variant hardware decision, not a firmware constraint).
- Inputs (potis, switches) need to be routable to one, several, or all channels, including fading between channels from a single physical control.

This is an **ongoing, multi-lamp project** — the design must support different channel counts per hardware variant, not one fixed number.

## 2. Current State (baseline, for reference)

Single global output, confirmed by architecture review (see conversation history / commit history around 2026-10):

- One PWM output per board (`applyPwmLevel()` in `src/lamp_state.cpp`), `LEDC_CH` hardcoded to `0`.
- No LED driver IC, no color/channel abstraction anywhere in the codebase.
- Poti, switches, touch, push-button all write directly into global scalars (`masterBrightness`, `lampEnabled`) — no routing/mapping layer.
- Functions (light sensor, music, radar presence, BLE presence, notifications) are scalar multipliers/overrides combined into one `combined` value per loop iteration (`updatePatternEngine()` in `src/main.cpp`).
- Pattern selection (`currentPattern`) and ramp/easing state (`rampActive`, `rampEaseOnType`, …) are flat globals, one instance for the whole lamp.
- Persistence is one NVS key per scalar (`src/persistence.cpp`), no indexed/array scheme.
- Status protocol (`emitLiveState()` in `src/print.cpp`) and the HA integration (`custom_components/quarzlampe/light.py`) both model the lamp as a single `ColorMode.BRIGHTNESS` light with one `brightness` field.

## 3. Hardware Direction

- **ESP32-WROOM is no longer the baseline for new variants.** Future multi-channel boards are S3-based (or newer); WROOM-specific constraints (BT Classic flash cost, fewer free GPIOs on compact layouts) are no longer a design input. Existing WROOM-based lamps keep running today's single-channel firmware/boards as-is — no migration required for them.
- Each channel gets its own constant-current driver stage on the PCB (inductor/FET/sense resistor per channel, as today's single-channel schematic already does) — however many a given lamp variant's design calls for. Firmware must not assume a fixed channel count; it should be configurable per board variant (build flag or runtime config), since different lamps will ship with different N.
- GPIO budget (ADC-capable pins for virtual inputs, PWM-capable pins for channels) is decided per board layout, not centrally — out of scope for this doc beyond "verify free pins when laying out a new variant."

## 4. Resource Headroom (verified 2026-10 via `pio run`)

| Env | Chip | RAM used | Flash used (of 1.9MB OTA slot) |
|---|---|---|---|
| `quarzlampe_rd03d` | ESP32-WROOM | 49.1KB / 320KB (15%) | 1343KB (68.3%) |
| `on_air` | ESP32-S3 | 52.4KB / 320KB (16%) | 1065KB (54.2%) |

- **RAM is not a constraint.** ~270KB free. Estimated per-channel state (brightness/ramp/pattern index/`FilterState`) is ~150–300 bytes; even a few dozen channels cost single-digit KB.
- **Flash is the resource to watch, but has headroom.** The 1.9MB effective app size (not the full 4/8MB flash) comes from the dual OTA-slot partition (`min_spiffs.csv`) needed for firmware updates. Multi-channel logic is written once and looped over an array of channel structs — it is **not** duplicated per channel, so its flash cost is a one-time generalization cost (expect low tens of KB), not something that scales with channel count. S3 variants have more headroom (54% used) than WROOM (66–68%) since they drop the BT-Classic stack.
- **Stack is not a concern** — channel iteration is flat, no recursion/deep call chains.

Conclusion: channel count is bounded by hardware (driver stages, GPIOs) and by UI/config complexity, not by the ESP32's memory.

## 5. Software Architecture

### 5.1 Channel data model

Introduce a `Channel` struct holding everything that is currently a global scalar, duplicated per channel:

- Output state: `brightness`, `enabled`, `lastAppliedLevel`, `outputScale`.
- Ramp/easing state: `rampActive`, `rampStartLevel`, `rampTargetLevel`, `rampStartMs`, `rampDurationMs`, `rampEaseOnType`/`OffType`, `rampEaseOnPower`/`OffPower`.
- Pattern state: `currentPattern` index, `patternStartMs`, custom-pattern buffer (if per-channel custom patterns are wanted — open question, see §7).
- `FilterState` instance (trem/comp/env/delay/spark) — already a self-contained struct, becomes one-per-channel instead of one-per-lamp.
- A stable channel ID/name (e.g. `0`, `1`, … or a short label) for command/status/persistence addressing.

Channel count `N` is a per-build/per-config constant (compile-time define, consistent with how other optional features are gated today via `platformio.ini` build flags), with firmware loops (`updatePatternEngine()`, input handling, persistence, status) written against `N` rather than assuming `1`.

The easing math (`applyEase()`) and pattern generators are already pure functions of `(t)`/`elapsedMs` — no changes needed there, they just get called once per channel instead of once per lamp.

### 5.2 Virtual input abstraction (poti / switch routing)

Avoid building a generic many-to-many mapping engine. Instead, generalize the existing single poti/switch into **virtual input instances**, reusing today's per-input calibration pipeline (`potiCalibMin/Max`, `invert`, `alpha`):

- A physical ADC pin (poti) or GPIO (switch) can back **multiple virtual inputs**, each with its own calibration curve and exactly **one** target channel (or the broadcast target "all channels").
- "Fade between channel A and B with one physical poti" = two virtual potis on the same pin: one with a rising curve targeting channel A, one with a complementary falling curve targeting channel B. No crossfade-specific code path needed — it's two ordinary 1:1 mappings.
- "One poti controls all channels together" = one virtual poti targeting the broadcast target.
- Same pattern for switches/buttons: a virtual switch instance per physical pin + action, with a channel (or broadcast) target instead of a hardcoded global call.
- Config for virtual inputs (which physical pin, curve params, target channel) needs a persistence + command-parser representation (see §5.4) and ideally a frontend UI (see §5.6).

**Groundwork already done (2026-10), ahead of §5.1's `Channel` struct existing:** the multi-button (`BUTTONS[]`/`ButtonConfig` in `include/pinout.h`) and poti-role (`potiRole`/`PotiRole` in `include/inputs.h`) work added this session each carry a `target` field (`uint8_t`, 0..254 = channel index, `CHANNEL_TARGET_ALL` = 255 = broadcast - the same sentinel convention decided for the wire format in §5.5/§7). It's wired through persistence, commands (`poti target <n>|all`), and status (`buttons=label:type:role:target:state`, `poti_target=`) but has **no behavioral effect yet** - there's only one channel to target. When §5.1 lands, dispatch just needs to start reading a value that's already there end-to-end, instead of needing another round of struct/persistence/wire-format changes.

### 5.3 Functions / notifications

Per the original requirement: most sensor-driven functions do **not** need to scale per channel — light sensor, music mode, radar/presence, BLE presence stay **global scalar multipliers**, applied identically to all channels (as today, just applied once per channel in the loop instead of once per lamp).

**Notifications are the one function that needs optional per-channel targeting** — a notification sequence should be able to either override all channels (today's behavior) or a specified subset. This means `notifySeq`/`notifyActive` needs an optional channel-mask field; default (unset) = all channels, matching current behavior with zero config changes for existing lamps.

### 5.4 Persistence

Flat one-key-per-scalar NVS storage (`src/persistence.cpp`) doesn't scale cleanly to per-channel + per-virtual-input state. Options to evaluate (decision needed, see §7):

- Indexed keys (`ch0_bri`, `ch1_bri`, …) — minimal change to existing pattern, but gets verbose for N virtual inputs × channels.
- A single serialized blob per channel (struct-to-bytes) under one NVS key per channel — fewer keys, requires a migration path and versioning if the struct shape changes later.

Either way, this needs backward-compatible defaults so existing single-channel lamps (which only ever had the flat keys) come up correctly as "1 channel, default routing" without a manual migration step.

### 5.5 Command parser & status protocol

**Decided (2026-10), not yet applied to any existing single-channel field** (changing e.g. `bri`'s syntax today would break every current consumer for zero present benefit - see §8):

- **Commands**: `<cmd> <target> <args...>` - `target` is a 0-based channel index or the literal `all` (e.g. `bri 0 80` / `bri all 80`). Matches the `poti target <n>|all` command already added as groundwork (see §5.2) for the inert per-input target field.
- **Status wire format**: comma-joined `index:value` pairs for any per-channel field, e.g. `bri=0:80,1:40` - no repeated per-channel blocks, no separate array-length field. `all`, not a numeric pseudo-channel, is how broadcast state is represented when it ever needs to appear here (resolves the §7 "real pseudo-channel-ID or special-cased flag" question in favor of the flag). This is the exact convention already in use for `buttons=label:type:role:target:state,...` (`print.cpp`) - multi-channel fields extend it rather than inventing a second one.
- Both of these are additive to the existing flat fields (`bri=`, `pattern=`, …), not a replacement, until N>1 channels actually exist for a build - see §8's non-goal about not forcing WROOM-based single-channel lamps onto new wire shapes they have no use for.
- `src/command.cpp`: every global today is an individually named command (`poti …`, `bri …`, `pattern …`, `ramp …`) and will need the `<target>` argument added per-command when §5.1 lands, plus new commands for virtual-input routing config.
- `src/print.cpp` (`emitLiveState()`/`printStatusStructured()`): add the array-shaped fields per-channel-aware field when §5.1 lands; this is still a wire-format *addition*, so needs the usual versioning/back-compat handling for existing consumers (HA integration, frontend, anything else parsing today's format) only insofar as those consumers must keep tolerating unknown new fields, which `frontend/src/hooks/status.ts`'s key-value parsing already does.

### 5.6 Home Assistant integration & frontend

- `custom_components/quarzlampe/light.py` currently models the lamp as one `ColorMode.BRIGHTNESS` light entity. Multi-channel lamps need either N light entities (one per channel) or a different HA representation — needs a decision (see §7).
- `frontend/` (`LedConfigCard.tsx`, `PotiCard.tsx`, `ModesCard.tsx`, etc.) needs UI for: per-channel brightness/pattern/ramp controls, and virtual-input routing config (assign a poti/switch instance to a channel, pick a curve). This is the largest "new surface" piece of the plan, not a mechanical refactor.

## 6. Rollout Phases

1. **Hardware**: define channel count + GPIO map for the first multi-channel variant (S3-based); confirm free pins for the planned virtual inputs.
2. **Core data model**: introduce `Channel` struct + array, migrate `lamp_state.cpp`/`main.cpp` loop to iterate over channels, keep `N=1` behavior identical for existing single-channel builds (regression check against current lamps).
3. **Pattern/ramp migration**: move `currentPattern`/ramp globals into the per-channel struct; verify all 62 existing patterns and all 17 easing curves work unchanged per-channel.
4. **Virtual input layer**: implement virtual poti/switch instances + routing config (in-memory first, no persistence yet).
5. **Persistence**: pick and implement the per-channel + per-virtual-input storage scheme (§5.4), with a tested upgrade path from existing single-channel NVS layouts.
6. **Command parser + status protocol**: add channel-selector syntax, update the status wire format, update anything that parses it.
7. **Notifications per-channel targeting**: optional channel mask on notification sequences.
8. **HA integration + frontend**: surface channels and virtual-input routing in both.
9. **Validation per hardware variant**: build/flash/test each shipped multi-channel board; confirm flash/RAM headroom holds with real channel counts in use.

## 7. Open Questions

- Persistence scheme: indexed keys vs. serialized blob per channel (§5.4)?
- HA representation: N separate light entities per lamp, or one entity with a custom multi-channel attribute model?
- Should custom patterns (`customPattern[32]` buffer) be per-channel or still shared/global?
- Soft limit on channel count and virtual-input count for UI/command-parser sanity (not a hardware limit, but a usability one)?
- Naming/addressing convention for channels in commands, status, and persistence (numeric index vs. user-assignable labels)?
- ~~Should "all channels" broadcast targeting for virtual inputs be a real pseudo-channel-ID, or a special-cased flag?~~ **Decided (2026-10, §5.5): a special-cased `all` flag**, not a numeric pseudo-ID - matches `CHANNEL_TARGET_ALL` (`include/pinout.h`) and the `poti target <n>|all` command already shipped as inert groundwork.

## 8. Non-Goals

- No change to the easing/pattern math itself — it is already channel-agnostic.
- No requirement to keep WROOM-based variants capable of running multi-channel firmware — they stay on today's single-channel firmware.
- No generic many-to-many routing engine — routing is deliberately kept to "one virtual input → one target (channel or broadcast)" to avoid combinatorial config complexity.
