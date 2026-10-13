# Quarzlampe Desktop Tray App (Rust)

## Context

The user owns several physical "quarzlampe" ESP32 lamps, each controllable over BLE via the same
protocol the existing web frontend (`frontend/`) already uses. They want a small, always-running
desktop tray/menu-bar app (Windows/macOS/Linux) that can see and control *all* of their lamps from
one place, plus a generic "when X happens, do Y to lamp(s) Z" automation system — starting with one
concrete trigger: turn a lamp on when the microphone becomes active (the "on-air light" use case).
A manual multi-select console for direct control, and a future music-reactive visualizer, round out
the vision; notifications-as-a-trigger and the visualizer are explicitly deferred past v1.

Decisions already made with the user:
- **New top-level folder in this repo** (`desktop/`), not a separate repo — keeps BLE-protocol
  knowledge co-located with the firmware that defines it.
- **Pure Rust** (egui/eframe + `tray-icon`), not Tauri/webview.
- **All three OSes architected from day one**, with an explicit caveat: this environment only has
  Linux (Manjaro) hardware to build and verify against. Windows/macOS code will be written against
  documented/community-known approaches but is **unverified** until run on those OSes.

## Protocol facts to reuse, not re-derive

Confirmed from `include/settings.h`, `src/comms.cpp`, `src/command.cpp`, `src/print.cpp`, and the
web client (`frontend/src/hooks/{bleClient,useBle,status}.ts`):

- UUIDs — service `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`, command char
  `4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7` (WRITE/WRITE_NR, plain newline-terminated ASCII, no ack),
  status char `c5ad78b6-9b77-4a96-9a42-8e6e9a40c123` (READ/NOTIFY/INDICATE, chunked, reassemble by
  splitting buffered bytes on `\n`, holding back the trailing partial line — exactly like
  `bleClient.ts`'s `subscribeToLines`).
- **Trust gate**: a never-seen MAC is only accepted within ~60s of lamp boot (`BT_TRUST_GRACE_MS`),
  or if already trusted. Outside that, with a stored token, the client must send `auth <token>\n`
  as its literal *first* write; the lamp holds the connection ~8s waiting for it before dropping.
  Tokens are minted by sending `trust token get` after a healthy connect, and arrive as
  `TRUST_TOKEN|<hex>` over the status notify (never log this line). **`useBle.ts:336-410`'s connect
  sequence must be replicated exactly**: attach the notify listener *before* any write → write
  stored `auth <token>` first if present → send `status raw`, race a `STATUS_END|` line against a
  1.8s timeout with one retry → only then send `trust token get`.
- **Commands**: `on`, `off`, `mode <n>`, `bri <0-100>`, `bri min/max <pct>`,
  `notify [<ms>...] [fade=<ms>]`, `profile load/save/show <1-6>`. (`notify stop` is unrelated
  pre-existing dead code in the firmware — don't rely on it.)
- **Live state**: firmware pushes `STATE|lamp=ON|bri=<pct>|pattern=<n>|out=<pct>|...` on change
  (debounced ~100ms). v1 console parses only this lightweight line for on/off/brightness/pattern —
  the full `STATUS|`/`STATUS1|`/`STATUS2|`/`STATUS_END|` snapshot is used once, during connect, not
  for ongoing polling. (Deliberate scope choice, not an oversight — revisit if later features need
  more fields.)
- **Identity caveat**: BLE peripheral IDs are opaque on macOS (CoreBluetooth hides real MACs) and
  not guaranteed stable. Assign an internal `LampKey(Uuid)` at first pairing; rules and the registry
  reference `LampKey`, never the raw BLE id directly.

## Crates

GUI: `eframe`/`egui` (latest stable, don't pin now). Tray: `tray-icon` + `muda`. BLE: `btleplug`.
Async: `tokio`. Cross-thread: `tokio::sync::{mpsc, watch}`. Config: `serde` + `toml` + `directories`.
IDs: `uuid`. Mic backends: `pulsectl`/`libpulse-binding` (Linux), `winreg` (Windows),
`coreaudio-rs` (macOS). Logging/errors: `tracing`, `anyhow`/`thiserror`.

## Highest-risk unknown: tray + egui integration

**M0 spike completed and verified on the real dev machine (Manjaro, GNOME, Wayland) — findings below
supersede the original speculative plan.**

Verified approach (`desktop/app/src/main.rs`):
- Use `tray-icon` with `default-features = false, features = ["ksni"]` instead of the default
  `libappindicator`/GTK backend — KSNI runs its D-Bus service on its own worker thread, so no manual
  GTK event-loop pumping is needed on Linux. Confirmed: a real tray icon appears in the GNOME top bar
  on this machine with zero extra setup — Manjaro's GNOME spin ships the AppIndicator/
  KStatusNotifierItem Shell extension by default. **Stock/vanilla GNOME (plain Fedora/Ubuntu GNOME
  without that extension) would still show no tray icon at all** — that part of the original risk is
  real and unchanged, just not applicable to this dev machine.
- `ViewportCommand::Visible(false)` on the **root** viewport does **not** reliably hide the window
  under GNOME/Mutter on Wayland — confirmed by testing: `close_requested` kept re-firing every frame
  and the window never actually disappeared. Wayland's xdg-shell has no generic client-side "hide"
  request, so this isn't a bug to fix, it's a protocol gap.
- **Working pattern instead**: make the root viewport a permanent, invisible 1×1, no-decorations
  anchor (`ViewportBuilder::default().with_inner_size([1.0,1.0]).with_visible(false)
  .with_decorations(false)`) that is *never* shown and *never* closed — it just keeps the process
  (and tray icon) alive. Put the actual console UI in a **deferred child viewport**
  (`Context::show_viewport_deferred`, id via `ViewportId::from_hash_of(...)`, since `from_hash_of`
  isn't `const`) that's simply re-registered every frame while a `console_open: bool` flag is true,
  and left un-registered (egui's viewport GC then tears down the OS window) when false. Closing *that*
  viewport's X button only needs `CancelClose` + flipping the flag — never touches the root, so the
  process survives regardless of what Wayland does with "hide." Confirmed working: close → process
  alive (checked via `ps`) → tray-icon click → window reopens, full cycle verified by hand.
  - Caveat: the deferred viewport's `viewport_ui_cb` is a plain `Fn` (not `FnMut`), so it can't hold
    `&mut self` directly — signal "close was requested" back to the app via a shared
    `Arc<AtomicBool>` cloned into the closure, read-and-reset once per frame in `logic()`.
  - Tray/menu event draining and the close-interception logic belong in `eframe::App::logic()`
    (new 0.36.2 API split: `logic()` runs every frame even while hidden/no painting allowed; `ui()`
    only runs when something is actually visible) — not in a single `update()` like older eframe.
- **Known unresolved cosmetic issue**: the always-present invisible root viewport still shows as a
  second, blank taskbar entry on this GNOME/Wayland session. `ViewportBuilder::with_taskbar(false)`
  (and `with_window_type` X11 hints) had no effect — Wayland has no standard "skip taskbar" hint
  either, this is the same category of gap as the hide issue. Living with it for now; the real fix
  would be the Plan B process-split below, which never creates an OS window for the tray-only part at
  all. Not worth that complexity just to remove one blank taskbar row.
- Windows/macOS are expected to piggyback on the same thread's message/run loop "for free," per
  `tray-icon`'s own docs, but this is still unverified — no hardware to test on.
- **Fallback (Plan B), not currently needed**: split into a tray-only process (bare winit loop, no
  windows at all) that spawns/attaches a second process for the egui console via local IPC
  (`interprocess` crate). The verified deferred-viewport pattern above made this unnecessary for M0;
  revisit only if the blank-taskbar-row cosmetic issue turns out to matter more than expected, or if
  Windows/macOS testing surfaces a blocker the single-process pattern can't work around.

## BLE client design

Per-lamp tokio task ("actor"), one `btleplug::platform::Peripheral` each, talking to the rest of the
app only via channels — never `.await` on the egui thread. Core shapes:

```rust
pub struct LampId(pub Uuid); // == registry LampKey

pub enum LampCommand { Connect, Disconnect, SendLine(String), Shutdown }

#[derive(Clone, Debug, Default)]
pub struct LampState {
    pub connection: ConnectionState,
    pub lamp_on: Option<bool>,
    pub brightness_pct: Option<f32>,
    pub pattern: Option<u32>,
    pub last_state_at: Option<Instant>,
    pub last_error: Option<String>,
}
pub enum ConnectionState { Disconnected, Connecting, AwaitingTrustToken, Connected, Reconnecting }
```

`run_lamp_actor(peripheral, cmd_rx, state_tx: watch::Sender<LampState>, tokens: Arc<TokenStore>)`:
connect → discover services → subscribe status char *before any write* → reassemble lines (own
small unit-testable `line_codec.rs`, mirroring `bleClient.ts` exactly — easy to get the
"hold back the trailing partial segment" detail wrong) → replicate the trust/auth dance from
`useBle.ts` step for step → on disconnect, capped-backoff reconnect, but if outside the trust window
with no token, surface `last_error` instead of looping forever.

UI thread reads `watch::Receiver` snapshots per frame (cheap, lock-free "latest value"); sends
`LampCommand`s via `mpsc` senders. `LampManager` is a `HashMap<LampId, LampHandle>` plus a
`BleScanner` (filter on the service UUID) for discovering new lamps — pairing a new one must happen
inside its 60s boot window, and the UI must say so explicitly.

## Mic-active detection

```rust
pub enum MicActivity { Active, Inactive, Unknown(String) }
pub trait MicActivityProvider: Send {
    fn poll(&mut self) -> MicActivity; // cheap, non-blocking, called every ~250-500ms
}
```

- **Linux (verifiable here)**: `pulsectl`, subscribe to `SourceOutput` events or poll
  `list_source_outputs()` for non-corked entries. Manjaro's PipeWire-via-pulse-compat shim should
  work through this; a native `pipewire-rs` backend is a possible v1.1 swap, not a v1 blocker.
- **Windows (unverified)**: poll `HKCU\...\CapabilityAccessManager\ConsentStore\microphone\...`,
  `LastUsedTimeStop == 0` heuristic (the same one Windows' own privacy dashboard uses). Misses
  legacy/driver-level captures — document as a known limitation.
- **macOS (least certain, explicitly flagged)**: `coreaudio-rs`, poll
  `kAudioDevicePropertyDeviceIsRunningSomewhere` on the default input device. The system's own
  orange-dot indicator uses private TCC internals with no public API — not pursued. Mark this
  module `UNVERIFIED — no macOS test hardware`.
- Unsupported/unknown platforms get `Unknown(reason)`, never silently treated as `Inactive`. The
  rule engine must never synthesize a rising edge out of `Unknown`, and the console shows a distinct
  "detection unsupported" badge rather than quietly behaving as if the mic is off.

## Rule engine + config

```rust
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Trigger {
    MicActive,
    // later, same shape, no restructuring needed:
    // Notification { app_filter: Option<String> }, MusicBeat { sensitivity: f32 },
}

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Action {
    On, Off,
    SetBrightness { pct: u8 },                                    // -> "bri <pct>"
    SetMode { pattern: u32 },                                     // -> "mode <n>"
    LoadProfile { slot: u8 },                                     // -> "profile load <n>"
    Notify { pattern_ms: Option<Vec<u32>>, fade_ms: Option<u32> }, // -> "notify ..."
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Rule { pub id: Uuid, pub name: String, pub enabled: bool, pub trigger: Trigger,
                   pub targets: Vec<LampKey>, pub action: Action }
```

One `rules::commands::action_to_command(&Action) -> String` is the single source of truth for
protocol syntax — both manual console quick-actions and the rule engine call it.

**Persistence**: `directories::ProjectDirs` config dir, `config.toml` / `lamps.toml` (registry:
friendly name, last-known BLE id, auth token) / `rules.toml`, atomic save (temp file + rename),
debounced on change. Tokens stored in plain TOML for v1 (no worse than the web app's `localStorage`,
and they're revocable by re-pairing) — OS `keyring` crate is a nice-to-have upgrade, not a blocker.

**Evaluation loop**: poll the mic provider every ~300ms, detect the `Inactive -> Active` **rising
edge only** (deliberate: fire-once-per-activation, not continuously — otherwise a `Notify` action
tied to a still-active mic would re-flash every tick forever). On a rising edge, for every enabled
matching rule, dispatch `action_to_command` to each target lamp's command channel.

## Module layout

Two-crate workspace under `desktop/` — pure logic separated from the GUI binary, so the BLE/rules
core is unit-testable without a display (important on Linux-only dev hardware):

```
desktop/
  Cargo.toml                 # workspace
  core/src/
    ble/{uuids,line_codec,lamp_actor,manager,trust}.rs
    state.rs                 # LampState + STATE| parser
    mic/{mod,linux,windows,macos,unsupported}.rs
    rules/{model,engine,commands}.rs
    config/mod.rs
    app_handle.rs             # wires manager+engine+config, exposes channels to the GUI
  app/src/
    main.rs                   # spawns tokio thread, calls eframe::run_native
    tray.rs                    # TrayIcon setup/menu/event drain, GTK pump (Linux)
    ui/{console,rule_editor,pairing}.rs
    bridge.rs                   # sync-friendly snapshot accessors over core's channels
```

## Milestones (highest-risk unknowns first)

1. **M0 — Tray+egui spike. ✅ DONE**, verified by hand on the real Manjaro/GNOME/Wayland session —
   see the updated "Highest-risk unknown" section above for what actually worked (KSNI tray backend,
   deferred-viewport close/reopen instead of `Visible(false)`) vs. the known residual blank-taskbar-
   row cosmetic issue. Code lives in `desktop/app/src/main.rs`. Still outstanding: same test on
   Windows/macOS (no hardware available here).
2. **M1 — BLE core against real hardware. 🟡 PARTLY DONE.** `core::ble` now has real modules
   (`line_codec.rs` with unit tests, `uuids.rs`, `trust.rs` - a simple per-lamp file token cache,
   `handshake.rs` - the connect/auth/status-wait/token-remint dance extracted out of the probe) plus
   `core/examples/probe.rs`, a terminal-only client with no GUI dependency.
   - **Verified live, twice** (once before the refactor, once after, identical behavior): connecting
     to the real "ON-AIR Lampe" with no cached token, outside its boot grace window, correctly hits
     `HandshakeOutcome::PendingToken` - the firmware holds the connection, we time out waiting for
     `STATUS_END|`, and a subsequent write then fails cleanly ("Not connected") instead of hanging.
     This is the "untrusted" path from the original milestone text, confirmed sane.
   - **Scan quirk found and worked around**: `ScanFilter { services: vec![uuids::SERVICE] }` missed
     the real lamp entirely in testing (same category of issue the web frontend already has a
     documented fallback for - see the `acceptAllDevices` comment in `bleClient.ts`). Fixed by
     scanning unfiltered and matching the service UUID against `properties.services` afterward,
     falling back to "any named device" if nothing matches by service (with an interactive pick
     prompt when there's more than one candidate).
   - **Still outstanding, deferred by the user to a later session**: the happy path (reboot the lamp,
     connect inside its 60s window, get a real `STATUS_END|`, mint and persist a token, then
     reconnect later using that stored token outside the window and confirm it succeeds). The code
     path exists (`HandshakeOutcome::Trusted { fresh_token }`) but hasn't been exercised against a
     freshly-booted lamp yet - do that before considering M1 fully closed.
3. **M2 — Fuse M0+M1.** Real `app` binary: persistent hide-on-close window + tray menu, console
   showing lamps with on/off only. Verify BLE survives window hide/show cycles.
4. **M3 — Console polish.** Live STATE-driven display, multi-lamp selection, brightness/mode/profile
   quick actions — via the same `action_to_command` the rule engine reuses later.
5. **M4 — Rule engine + persisted config, manually fired.** Rule editor UI (trigger picker already a
   match over the enum, even with only `MicActive` selectable), `lamps.toml`/`rules.toml`
   persistence, a manual "fire now" per rule to validate the dispatch pipeline independent of real
   trigger detection.
6. **M5 — Mic trigger end-to-end on Linux.** Real `mic::linux`, plus compiled-but-unverified
   Windows/macOS stubs (`cfg(target_os)`-gated so they never block Linux work), wired into the
   evaluation loop with edge detection; validated by actually toggling mic capture against a real
   lamp.
7. **M6 — Pairing/trust UX + multi-lamp hardening.** Scan/add flow with explicit 60s-window
   messaging, per-lamp reconnect-failure surfacing, config import/export, stress test with as many
   physical lamps as available connected at once.

## Verification

- M0/M1 each have their own standalone, concrete pass/fail test described above — don't skip either.
- From M2 onward, verification is "run the app against real lamps on the Manjaro dev machine" —
  there is no automated BLE hardware-in-the-loop test here.
- `line_codec.rs` and `rules::engine`'s edge-detection get real unit tests (fixed byte-chunk inputs /
  synthetic mic-state sequences) since they're pure logic and the highest-value place for bugs to
  hide quietly.
- Windows/macOS code paths compile (CI can cross-compile) but are explicitly unverified until run on
  real hardware — don't claim "supported" for them before that happens.

## Explicitly out of scope for v1

Notification trigger (per-OS detection is its own hard problem — Linux via D-Bus eavesdropping,
Windows needs app-identity/MSIX packaging, macOS is heavily locked down) and the music-reactive
visualizer. Both fit the existing `Trigger`/`Action` enum shape without rework when picked up later.
