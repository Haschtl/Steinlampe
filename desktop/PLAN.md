# Quarzlampe Desktop Tray App (Rust)

## Context

The user owns several physical "quarzlampe" ESP32 lamps, each controllable over BLE via the same
protocol the web frontend (`frontend/`) already uses. Goal: a small, always-running desktop
tray/menu-bar app (Windows/macOS/Linux) that sees and controls *all* lamps from one place, plus a
generic "when X happens, do Y to lamp(s) Z" automation system (rules), a manual multi-select
console, and a music-reactive visualizer.

**Platform verification status (applies throughout this whole document, stated once instead of
per-item): everything is written for all three OSes from day one, but this dev machine only has
Linux (Manjaro, GNOME, Wayland) to build and run on. "Done" below always means "built, and verified
live on Linux" unless stated otherwise. Windows/macOS code is written from documented APIs (read
from real source/docs where flagged, not guessed) and compiles via CI's cross-platform matrix build,
but has never been *run* on real Windows/macOS hardware — don't read "CI is green" as "verified" for
those two.**

Core decisions:
- **New top-level folder in this repo** (`desktop/`), not a separate repo — keeps BLE-protocol
  knowledge co-located with the firmware that defines it.
- **Pure Rust** (egui/eframe + `tray-icon`), not Tauri/webview — one codebase, accepted ceiling on
  native look (see "Console UI" below).
- **Two-process architecture**: BLE connections, the rule engine, and mic/camera/audio detection all
  live in a window-less daemon (`desktop/app`), not the console — the whole point of rule-based
  automation is that it keeps working whether or not the console window is open. The console
  (`desktop/console`) is a thin IPC client with no BLE/tokio logic of its own.
- **Token storage: plain file, not the OS keyring.** Same reasoning the web frontend already applies
  to its `localStorage` token — it only buys reconnect convenience, not a security boundary, and is
  revocable by re-pairing. Implemented as `core::ble::trust::FileTokenStore`.
- **Console UI: egui, not native-per-OS.** Redesigned twice after user feedback (amber dark theme →
  cyan theme matching the web frontend's real tokens, `frontend/tailwind.config.cjs`/`index.css`:
  `bg #0b0f1a`, `panel #11182a`, `border #1f2b44`, `text #eef2ff`, `muted #9fb2cc`, `accent #5be6ff`).
  Explicitly **not** a claim egui renders native OS widgets — the user accepted that tradeoff for one
  Rust codebase. A true native-per-OS rewrite of just the console stays a deferred option *because*
  the console is already nothing but an IPC client — swapping it later wouldn't touch `core` or the
  daemon. Two-process split means closing the console window never leaves a blank taskbar entry
  behind (see M0 below for why that took two attempts). Sidebar nav (Connections/Rules/Music), one
  card per lamp with inline quick-actions, brightness slider commits on release (not every drag
  frame) so dragging doesn't flood the lamp with BLE writes.

## Protocol facts to reuse, not re-derive

Confirmed from `include/settings.h`, `src/comms.cpp`, `src/command.cpp`, `src/print.cpp`, and the
web client (`frontend/src/hooks/{bleClient,useBle,status}.ts`):

- UUIDs — service `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`, command char
  `4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7` (WRITE/WRITE_NR, plain newline-terminated ASCII, no ack),
  status char `c5ad78b6-9b77-4a96-9a42-8e6e9a40c123` (READ/NOTIFY/INDICATE, chunked, reassemble by
  splitting buffered bytes on `\n`, holding back the trailing partial line — exactly like
  `bleClient.ts`'s `subscribeToLines`).
- **Trust gate**: a never-seen MAC is only accepted within ~60s of lamp boot (`BT_TRUST_GRACE_MS`),
  or if already trusted. Outside that, with a stored token, the client must send `auth <token>\n` as
  its literal *first* write; the lamp holds the connection ~8s waiting for it before dropping. Tokens
  are minted by sending `trust token get` after a healthy connect, arriving as `TRUST_TOKEN|<hex>`
  over the status notify (never log this line). **`useBle.ts:336-410`'s connect sequence must be
  replicated exactly**: attach the notify listener *before* any write → write stored `auth <token>`
  first if present → send `status raw`, race a `STATUS_END|` line against a 1.8s timeout with one
  retry → only then send `trust token get`.
- **Commands**: `on`, `off`, `mode <n>`, `bri <0-100>`, `bri min/max <pct>`,
  `notify [<ms>...] [fade=<ms>]`, `profile load/save/show <1-6>`, plus (added M9) `wake`/`morse`
  matching `src/command.cpp` exactly.
- **Live state**: firmware pushes `STATE|lamp=ON|bri=<pct>|pattern=<n>|out=<pct>|...` on change
  (debounced ~100ms). The console parses only this lightweight line for ongoing display — the full
  `STATUS|` snapshot is used once, during connect, not for polling (deliberate scope choice).
- **Identity caveat**: BLE peripheral IDs are opaque on macOS (CoreBluetooth hides real MACs) and not
  guaranteed stable. Assign an internal `LampKey(Uuid)` at first pairing; rules and the registry
  reference `LampKey`, never the raw BLE id directly.

## Crates

GUI: `eframe`/`egui`. Tray: `tray-icon` + `muda`. BLE: `btleplug`. Async: `tokio`. Cross-thread:
`tokio::sync::{mpsc, watch}`. Config: `serde` + `toml` + `directories`. IDs: `uuid`. Mic:
`libpulse-binding` (Linux — MIT/Apache, chosen over GPL-3 `pulsectl-rs`), registry polling
(Windows), `objc2-core-audio` (macOS). Logging/errors: `tracing`, `anyhow`/`thiserror`.

## Architecture: tray + egui (M0)

**Two-process** (`desktop/app` = daemon, `desktop/console` = UI):
- `desktop/app` (`quarzlampe-tray`) is a tiny, **window-less** daemon: `tray-icon` (KSNI backend —
  runs its own D-Bus worker thread, no event loop needed, so this binary has no eframe/egui/winit
  dependency) + `muda` for the menu, polling events in a plain `loop { ...; sleep(100ms) }`. On "Show
  console" it spawns `quarzlampe-console` as a child process if one isn't already running; on "Quit"
  it kills that child and exits. Console binary located as a sibling of the daemon's own exe.
- `desktop/console` (`quarzlampe-console`) is an ordinary `eframe` app — one real, visible window, no
  hide tricks. Closing it just ends that process; the daemon only tracks whether the child is alive.
- Confirmed live: tray icon appears (needs GNOME's AppIndicator extension or equivalent — stock
  vanilla GNOME won't show one, a real but machine-specific caveat), clicking spawns the console,
  closing it exits only that process, no blank taskbar entry.
- An earlier single-process design (one eframe process with an invisible anchor viewport + a
  deferred child viewport for the console) worked but left a permanent blank taskbar entry with no
  Wayland fix available, plus per-viewport repaint-scheduling bugs — the two-process split sidesteps
  both rather than chasing them further.

## Daemon/console IPC

Plain **loopback TCP** (`127.0.0.1`, OS-assigned port), not a platform-specific transport — works
identically via `tokio` on all three OSes with no extra crate, which matters given none of this can
be tested on Windows/macOS here. The daemon writes `{port}\n{local_auth_token}` to a file in the OS
runtime/config dir (`directories::ProjectDirs`) so the console can find and authenticate to it — the
token is local hygiene only, not a real security boundary. Framing: newline-delimited JSON
(`serde_json`), one `IpcRequest`/`IpcEvent` enum pair defined once in `core` and shared by both sides.

## BLE client design

Per-lamp tokio task ("actor"), one `btleplug::platform::Peripheral` each, living in the daemon and
talking to the rest of it only via channels.

```rust
pub struct LampId(pub Uuid); // == registry LampKey
pub enum LampCommand { Connect, Disconnect, SendLine(String), Shutdown }
pub struct LampState { connection: ConnectionState, lamp_on: Option<bool>, brightness_pct: Option<f32>,
                        pattern: Option<u32>, last_state_at: Option<Instant>, last_error: Option<String> }
pub enum ConnectionState { Disconnected, Connecting, AwaitingTrustToken, Connected, Reconnecting,
                            ReconnectFailed /* M16 */ }
```

`run_lamp_actor`: connect → discover services → subscribe status char *before any write* → reassemble
lines (`line_codec.rs`, unit-tested, mirrors `bleClient.ts`'s "hold back the trailing partial
segment" detail) → replicate the trust/auth dance from `useBle.ts` step for step → on disconnect,
capped-backoff reconnect (500ms→30s), giving up after 5 consecutive failures (M16) rather than
retrying forever. `LampManager` is a `HashMap<LampId, LampHandle>` plus a `BleScanner` (filter on the
service UUID, falling back to unfiltered scan + post-hoc UUID match since filtered scans missed real
devices in testing — same issue `bleClient.ts` already has a documented fallback for).

## Mic/camera/audio detection

```rust
pub trait MicActivityProvider { fn poll(&mut self) -> Result<MicActivity, String>; } // Active/Inactive
```
`Err` means unsupported/failed and must never be treated as "inactive" — the rule engine must never
synthesize a rising edge out of it, and the console shows a distinct "unsupported" state.

- **Mic — Linux**: `libpulse-binding`, subscribe/poll `list_source_outputs()` for non-corked entries.
- **Mic — Windows**: poll `HKCU\...\CapabilityAccessManager\ConsentStore\microphone\...`,
  `LastUsedTimeStop == 0` (the same heuristic Windows' own privacy dashboard uses). Misses
  legacy/driver-level captures.
- **Mic — macOS**: `objc2-core-audio` (swapped in M14 from raw `coreaudio-sys` FFI for published,
  versioned bindings), polling `kAudioDevicePropertyDeviceIsRunningSomewhere` on the default input
  device.
- **Camera — Linux**: scans `/proc/<pid>/fd/*` for a `/dev/video*` node (same technique as `fuser`).
  **Windows**: same `ConsentStore` registry mechanism, under `\webcam`. **macOS**:
  `objc2-core-media-io` (M14), enumerating CMIO devices via `kCMIOHardwarePropertyDevices` and
  checking `kCMIODevicePropertyDeviceIsRunningSomewhere`.
- **Audio loudness (visualizer, M12)**: RMS only, ~10Hz, no beat-detection/frequency bands (declined
  as pricier alternatives). Linux: a real PulseAudio/PipeWire recording stream on
  `"<default_sink_name>.monitor"`. Windows: WASAPI loopback via `cpal` (its backend adds
  `AUDCLNT_STREAMFLAGS_LOOPBACK` automatically for an output-device input stream). **macOS: no
  implementation** — CoreAudio has no loopback mode; the real workarounds (BlackHole,
  `ScreenCaptureKit`) are each their own project.

## Rule engine + config

```rust
pub enum Trigger { MicActive, MicInactive, CameraActive, CameraInactive, OnBoot, OnBeforeSleep,
                    BeforeShutdown, OnNotification { app_name_filter: Option<String> },
                    OnAppOpen { app_name: String }, OnKeyPress { key: String },
                    AtTime { hour: u8, minute: u8 }, ScreenLocked, ScreenUnlocked, Webhook { id: Uuid } }
pub enum Action { On, Off, SetBrightness { pct: u8 }, SetMode { pattern: u32 }, LoadProfile { slot: u8 },
                  Notify { pattern_ms: Option<Vec<u32>>, fade_ms: Option<u32> },
                  StartWake { seconds: u32, soft: bool, mode: Option<u32>, brightness_pct: Option<u8> },
                  SendMorse { text: String } }
pub struct Rule { id: Uuid, name: String, enabled: bool, triggers: Vec<Trigger>, targets: Vec<String>,
                   action: Action, quick_rule: Option<QuickRuleKind>, recheck_secs: Option<u32> }
```

One `rules::commands::action_to_command(&Action) -> String` is the single source of truth for
protocol syntax — manual console quick-actions, the tray menu, and the rule engine all call it.

**Platform support per trigger** (the one place platform gaps are genuinely load-bearing, so stated
precisely instead of per-milestone):
- `MicActive`/`MicInactive`, `CameraActive`/`CameraInactive`: cross-platform except macOS camera (see
  above — now implemented, M14) and macOS mic (implemented, M14).
- `OnBoot`, `OnAppOpen` (via `sysinfo`), `AtTime`: genuinely cross-platform, no OS-specific code.
- `OnBeforeSleep`/`BeforeShutdown`: Linux via `logind` D-Bus signals. Windows/macOS: **also now
  covered** — `app::platform::windows`'s hidden window already handles `WM_POWERBROADCAST`/
  `WM_QUERYENDSESSION`/`WM_WTSSESSION_CHANGE`, and `app::platform::macos::run()` already observes
  `NSWorkspaceWillSleepNotification`/`NSWorkspaceWillPowerOffNotification` — both added later (for the
  tray icon and for `OnKeyPress`, see below) but never cross-referenced here until this cleanup pass.
  Not run-time verified on real Windows/Mac hardware, same caveat as everything else cross-platform.
- `OnNotification`: Linux-only, no equivalent gap-closing is possible — no portable "a notification
  was shown" API exists on Windows or macOS at all (not harder, actually absent).
- `OnKeyPress` (`global-hotkey`, `"control+alt+KeyL"`-style combo strings): needs a native message/run
  loop pumped on the registering thread. X11 gets this for free (the crate's own backend thread).
  Windows/macOS now get it too, via the same `platform::windows::pump()`/`platform::macos::run()`
  infra as the sleep/shutdown triggers above. **Wayland (no XWayland) is the one real, unfixable-by-us
  gap** — `global-hotkey` 0.8 has no Wayland backend at all. A real fix means switching to/adding
  `ashpd` (XDG `GlobalShortcuts` portal — a structurally different session+dialog UX, not a drop-in;
  evaluated 2026-11-27, not pursued: ~150-300 LOC, app can only *suggest* a default combo via
  `preferred_trigger`, silent-reconnect-on-relaunch behavior unconfirmed) or raw evdev
  (`kbd-global`/`hotkey-listener` — works on X11/Wayland/TTY alike, but needs input-device
  permissions, more invasive for a non-sandboxed app). Not attempted.
- `ScreenLocked`/`ScreenUnlocked`: Linux only, best-effort `org.freedesktop.ScreenSaver` watch.
- `Webhook`: cross-platform (local HTTP server), no OS dependency.

**Persistence**: `directories::ProjectDirs` config dir, `config.toml`/`lamps.toml`/`rules.toml`,
atomic save (temp file + rename), debounced on change. Tokens in plain TOML (same reasoning as
above).

**Evaluation loop**: poll providers every ~300ms, fire on rising/falling edges only (not
continuously). `recheck_secs` (optional, used by quick-rule `OnAir`) re-sends the action periodically
while the condition stays true — needed for the on-air multi-device case, where one machine's mic
going inactive would otherwise fight another machine's still-active mic.

## Module layout

```
desktop/
  Cargo.toml                  # workspace (members: core, app, console)
  core/src/
    ble/{uuids,line_codec,handshake,trust,lamp_actor,manager}.rs
    state.rs                  # LampState + STATE| parser
    mic/{mod,linux,windows,macos,unsupported}.rs
    camera/{mod,linux,windows,macos}.rs
    audio/{mod,linux,windows}.rs
    rules/{model,engine,commands}.rs
    config/mod.rs
    ipc/{protocol,server,client}.rs
    i18n/mod.rs
    update/mod.rs
  app/src/
    main.rs                    # window-less tray daemon: BLE/rules/mic/IPC server/tray menu
    platform/{windows,macos}.rs # native message/run-loop pump (tray icon, OnKeyPress, sleep/shutdown)
    autostart.rs
  console/src/
    main.rs                    # ipc::client, no BLE/tokio logic of its own
  packaging/{linux,macos,windows}/   # AppImage / .dmg / .msi build scripts
```

## Milestones

Numbered items are in build order; "ad hoc" items were user-requested follow-ups that didn't map to
a pre-planned milestone number. All ✅-marked items are built and Linux-verified; platform gaps are
called out inline only where they differ from the "Platform verification status" note at the top.

1. **M0 — Tray+egui integration. ✅** Two-process architecture, see above.
2. **M1 — BLE core against real hardware. ✅** `core::ble` (`line_codec`, `uuids`, `trust`,
   `handshake`) + `core/examples/probe.rs`. Verified: untrusted-connection path (`PendingToken`,
   correct timeout, no hang) confirmed live twice. Scan-filter fallback added (see BLE client design
   above). The full happy path (boot lamp → connect in window → mint token → reconnect later using
   it) was deferred by the user to a later session and is still not separately exercised — every
   later milestone's live BLE tests happened to go through other paths instead.
3. **M2 — Daemon owns BLE/rules, console is a thin IPC client. ✅** `core::ble::{lamp_actor, manager}`
   (tokio-task-per-lamp, capped-backoff reconnect), `core::state` (`STATE|` parser, unit-tested),
   `core::ipc::{protocol, server, client}`. Verified end to end against a real lamp: scan, connect,
   `on`/`off` both took effect and round-tripped through `STATE|`. Confirmed the daemon's tray-polling
   thread and its tokio-runtime thread don't conflict over D-Bus (`tray-icon`'s `zbus` vs.
   `btleplug`'s `dbus`/`dbus-tokio`) — an apparent early "hang" turned out to be a red herring (the
   daemon's discovery file was in `/run/user/<uid>/...`, a separate tmpfs mount a `find / -xdev`
   search had silently skipped).
4. **M3 — Console polish. ✅** `core::rules::{model, commands}` (`Action` + `action_to_command`).
   Console: multi-select action bar (On/Off, brightness, mode, profile slot). Verified live.
   - **Gap found (fixed in M16)**: `LampCommand::Disconnect` didn't distinguish "stay disconnected"
     from a dropped connection — both auto-reconnected.
5. **M4 — Rule engine + persisted config, manually fired. ✅** `core::rules::model` (`Trigger::MicActive`
   only), `core::config` (`rules.toml`, atomic save). `ipc::server` gained `ListRules`/`SaveRule`/
   `DeleteRule`/`FireRule`. Console: Rules section with enable/fire/delete + a new-rule form. Verified
   live end to end via a raw IPC script against a real lamp: save → fire → `on`, change action to
   `SetBrightness{35}` → fire → brightness 35% confirmed via `STATE|`, delete confirmed in
   `rules.toml`.
5a. **Tray menu lamp controls. ✅** Tray dropdown lists every connected lamp with live state + quick
   controls (toggle, 4 brightness presets), rebuilt from `DaemonState` every ~1.5s (native menu APIs
   have no in-place patching). A `LampIndex` maps opaque BLE keys to stable small ints for menu item
   ids. Verified live.
5b. **Tray icon reflects state; console gets a real taskbar icon. ✅** Tray icon is a procedurally-drawn
   dot recolored on refresh (grey/dim-cyan/bright-cyan). Console window icon needed
   `.with_app_id(...)` + a matching `.desktop` file — on GNOME/Wayland, `with_icon`'s pixel buffer
   alone does nothing; the shell resolves taskbar icons from the `.desktop` file's `app_id` match, not
   from anything the app draws. Verified live.
6. **M5 — Mic trigger end-to-end on Linux. ✅ (mostly)** `core::mic` + `PulseMicProvider`. Verified in
   isolation (`core/examples/mic_probe.rs` tracked a real `parecord` capture correctly) and the
   `Rule → action_to_command → manager.send` dispatch path was already proven in M4 — but the full
   "speak near a connected lamp with an enabled rule" end-to-end pass was never done (both lamps hit
   unrelated BLE issues that session); the user accepted the isolated evidence as sufficient.
7. **M6 — Pairing/trust UX + multi-lamp hardening.** Not started as a numbered milestone — most of its
   intended scope (reconnect-failure surfacing, per-lamp error visibility) landed piecemeal via later
   ad hoc items (M16, the pairing-confirmation-warning audit) instead.
8. **CI + download offer. ✅** `.github/workflows/desktop-build.yml`: matrix build
   (ubuntu/windows/macos-latest) on every push/PR touching `desktop/**`; a separate `publish-release`
   job (now auto-runs on push to `main`, see CI/CD history — originally manual-dispatch-only) publishes
   a `desktop-latest` Release. "Desktop App (Beta)" card added to the frontend's Help page with the
   Linux-tested/Windows-macOS-unverified caveat stated directly in the UI text.
9. **Six more triggers, two more actions, start-on-boot. ✅** `OnBoot`, `OnAppOpen`, `OnBeforeSleep`/
   `BeforeShutdown` (`logind` D-Bus), `OnNotification` (D-Bus monitor mode,
   `BecomeMonitor` — the modern replacement for the deprecated `eavesdrop=true` flag), `OnKeyPress`
   (`global-hotkey`) — see the Rule engine section above for current platform-support status per
   trigger (corrected 2026-11-27; this milestone's original text claimed Windows/macOS couldn't run
   `OnKeyPress` at all, which a later milestone's `platform::windows`/`platform::macos` infra quietly
   fixed without this doc being updated until now). `StartWake`/`SendMorse` actions added, matching
   firmware syntax read directly from `src/command.cpp`. `autostart` module, one impl per OS
   (XDG `.desktop` / registry `Run` key / LaunchAgents plist), surfaced as a tray menu toggle.
   Verified live (Linux): `OnBoot`, `OnAppOpen`, `OnNotification` (fired twice for one `notify-send`
   once — a real quirk, not chased further), autostart toggle confirmed on disk both ways.
   `OnBeforeSleep`/`BeforeShutdown` confirmed to subscribe without error; not fired for real (didn't
   want to suspend the dev machine). `OnKeyPress`: manager init confirmed to succeed (presumably via
   XWayland), actual firing never verified.
10. **Windows/macOS mic detection, and a real installer. ✅ (Linux; written-only elsewhere)**
   `core::mic::windows`/`core::mic::macos` (see Mic/camera/audio section above). A real installer,
   not just loose binaries: `quarzlampe-tray --install`/`--uninstall` — Linux copies binaries + icon
   to XDG dirs and writes a visible `.desktop` launcher (separate from `autostart.rs`'s hidden one);
   Windows copies to `%LOCALAPPDATA%` + a PowerShell-scripted Start Menu shortcut; macOS builds a
   minimal `.app` bundle. Verified live end to end on Linux (install → run installed binary → found
   its sibling console → uninstall → clean).
11. **i18n: English/German, derived from OS locale. ✅** `core::i18n`, `Lang` detected via
   `sys-locale`, a `dict!` macro over a flat static-string table (same spirit as the frontend's
   `i18n.tsx`). No in-app switcher — purely locale-derived. Verified live both directions (`$LANG=de_DE`
   and a forced `en_US` relaunch).
12. **Mic-inactive, camera trigger, music visualizer. ✅ (mostly)** `MicInactive` (the missing falling
   edge). `CameraActive`/`CameraInactive` — macOS deliberately skipped this round (CoreMediaIO
   constants not confidently recallable without a Mac; fixed in M14). Music visualizer (RMS →
   brightness, see Mic/camera/audio section). MQTT was considered as a daemon-side bridge for a
   future "music/notification → lamp" idea and declined outright — belongs on the lamp itself (WiFi)
   if it ever happens, not here. Verified live on Linux (camera via `ffmpeg`, visualizer via a sine
   tone's RMS tracked correctly end to end, though not against an actually-connected lamp — the user
   accepted that as sufficient rather than rebooting a lamp for a final visual check).
13. **Quick Rules. ✅** `QuickRuleKind::OnAir` (`MicActive→On` + `MicInactive→Off`, one toggle per lamp
   card) — console-only packaging over the existing `Rule`/`SaveRule` machinery, no new IPC. Verified
   live via direct IPC script (toggle on/off produces/removes exactly the two tagged rules).
14. **macOS camera trigger + trustworthy mic backend, auto-update checks, code-signing scaffolding.
   ✅ (Linux; macOS pieces unrun)**
   - macOS camera (`objc2-core-media-io`) and mic (`objc2-core-audio`, replacing raw `coreaudio-sys`
     FFI) rebuilt on published, versioned bindings instead of build-time-generated ones — see
     Mic/camera/audio section above.
   - **Auto-update checks, notify-only** — `core::update::check_for_update`, compares the running
     build's embedded git SHA (`"dev"` for local builds, short-circuits the check) against a
     `BUILD_SHA.txt` the release workflow publishes. Deliberately not a self-replacing updater: no
     code signing existed yet, and swapping a running executable is a different operation per OS.
     Verified live (dev-build short-circuit, a forced real GitHub API call, deserialization checked
     against a real existing release).
   - **Code signing — free paths chosen over paid ones.** macOS: ad-hoc signing (`codesign ... -`,
     free, satisfies Apple Silicon's "must carry some signature" requirement) with the Gatekeeper
     "unidentified developer" warning documented as a bypass rather than paid away (real removal
     needs a $99/yr Developer Program membership). Windows: SignPath Foundation's free OSS signing —
     conditional on the repo actually qualifying, so the repo gained a real **GPL-3.0-or-later**
     `LICENSE` and `CODE_SIGNING_POLICY.md`; CI submits a signing request via
     `signpath/github-action-submit-signing-request@v3`, gated on `SIGNPATH_API_TOKEN` being set.
     **Still not active** — applying to SignPath (signpath.io/apply) is an identity-linked step only
     the repo owner can take; until then Windows binaries stay unsigned with a documented SmartScreen
     bypass, same pattern as macOS's Gatekeeper note.
   - **~~Why `OnBeforeSleep`/`BeforeShutdown`/`OnNotification` stay Linux-only~~ — corrected
     2026-11-27**: this was written before `platform::windows`/`platform::macos`'s message-pump infra
     existed. `OnBeforeSleep`/`BeforeShutdown` are now very likely cross-platform too — see the Rule
     engine section above for the current, accurate status. `OnNotification` genuinely has no
     Windows/macOS equivalent; that part was correct and stays true.
15. **Real native installers, not loose executable pairs.** Per-platform packaging scripts under
   `desktop/packaging/`:
   - **Linux: AppImage.** Verified live (ran the resulting `.AppImage`, confirmed normal startup and
     that `console_binary_path()`'s sibling lookup resolves correctly from inside the mounted
     squashfs). **Real bug found and fixed**: `autostart`'s `set_enabled` used `current_exe()`, which
     resolves to AppImage's temporary mount path (gone after exit) — fixed to prefer `$APPIMAGE`
     (the permanent path AppImage's runtime sets) when present.
   - **macOS: a `.dmg`** containing a real `.app` bundle, ad-hoc signed, built entirely with tools
     that ship with macOS (`sips`/`iconutil`/`hdiutil`, no third-party packaging tool). Unverified —
     no macOS hardware.
   - **Windows: a real `.msi` via WiX Toolset v3** (not v4+, whose schema is still actively changing;
     v3 is preinstalled on `windows-latest` and has been stable for a decade). Adapted from
     `cargo-wix`'s real v3 template, built directly via `candle.exe`/`light.exe`. Unverified — no
     Windows hardware. **First real CI run found a real bug**: `-dVersion=0.1.0` arrived mangled
     (missing its leading `0`) — fixed by quoting every `-d`/path argument as its own PowerShell
     string rather than a bare re-tokenizable token.
   - Signing (M14) now targets the packaged installer itself, not the loose binaries.
16. **Stop retrying forever after 5 failures. ✅** `ConnectionState::ReconnectFailed` +
   `MAX_CONSECUTIVE_FAILURES = 5` (resets on success) — after 5, the actor blocks on `cmd_rx.recv()`
   instead of retrying on a timer, until a manual `Reconnect` IPC request. Every failure cause counts,
   including trust rejections. Console/tray both get a "Reconnect" button once failed. Verified live
   against a real lamp: 5 failures in ~8s matching the backoff curve, then genuinely zero further
   attempts for a 20s watch, then `Reconnect` correctly resumed a fresh cycle.
17. **Console layout: discovered-lamps moved up, Music given its own tab.** `discovered_lamps_ui` now
   sits directly under the lamp grid (was buried below the whole visualizer panel). Visualizer moved
   to its own sidebar tab. Pure layout change, verified via build + clippy only — no input-simulation
   tool available in this sandbox to click through and screenshot.
18. **Tray icon: a hexagon with up to 4 per-lamp status segments. ✅** Point-in-convex-polygon fill
   test, horizontal bands per lamp (bright/dim cyan/grey), ordered by the same stable `LampIndex` the
   tray submenu uses so segments don't swap position between rebuilds. Verified by a standalone
   Python reimplementation of the exact pixel math (no screenshot tool available for the live tray);
   real Rust code separately confirmed to build/clippy/run.
19. **Auto-reconnect for rule/visualizer-relevant lamps. ✅** `spawn_auto_reconnect_loop`: every 2
   minutes, periodically re-scans for lamps targeted by an enabled rule or the live visualizer that
   aren't currently connected or mid-retry — distinct from M16's give-up-after-5 (that caps retries
   for an already-spawned actor; this finds lamps never even scanned this session). Verified live:
   saved a rule targeting a never-connected lamp, watched the loop find and connect it on its first
   tick.
20. **Filterable notification trigger + Teams-notification quick rule.** Requested as "Teams
   integration" for call/notification blinking. Researched first: no real-time "incoming call" event
   exists anywhere, and "call active" needs Microsoft Graph presence (Azure AD app + OAuth + a
   work/school account, polling only) — asked the user, who chose to drop call-ringing/call-active
   and ship only the notification-filter piece, reusing the existing D-Bus-eavesdrop `OnNotification`
   trigger. `app_name_filter` field added (case-insensitive substring match); `QuickRuleKind::
   TeamsNotification`. Verified live with real D-Bus notifications (`Discord` correctly ignored,
   `Microsoft Teams` correctly matched). **Documented, not fixed**: on this dev machine's desktop,
   every notification fires the rule twice (two genuinely distinct D-Bus calls, not a double-delivered
   one) — deliberately not deduplicated, since doing so by serial/sender risks dropping an unrelated
   notification arriving in the same instant.
21. **Tray icon: transparent vertical slit down the middle.** Follow-up to M18; genuinely ambiguous as
   first stated, so candidate renders were shown before picking "a literal transparent cut through the
   middle" over "a left/right color split". Purely cosmetic, doesn't touch segment logic. Verified the
   same way as M18 (standalone Python reimplementation + separate Rust build/clippy check).
22. **Edit existing rules in the console.** The rule list only ever offered enable/disable/delete/fire
   — no way to change a trigger/action/targets short of rebuilding from scratch, even though the
   backend already supported update-by-id. Added `start_editing_rule` + an `editing_rule_id` field.
   **One called-out limitation**: `Action::Notify`'s `pattern_ms` is already-flattened by the time
   it's saved, so editing can't recover the original base-pattern/repeat-count split (ambiguous by
   construction) — loads as the literal flattened pattern with repeat reset to 1, an exact round trip
   for the *result*, not a recovery of what was originally typed. Verified by build/clippy + a careful
   field-by-field review against the new-rule form (no input-simulation tool available for a live
   click-through); the underlying update-by-id mechanism itself was already proven live earlier.

## Verification

- M0/M1 each have their own standalone, concrete pass/fail test — see above, don't skip either.
- From M2 onward, verification is "run the app against real lamps on the Manjaro dev machine" — no
  automated BLE hardware-in-the-loop test exists here.
- `line_codec.rs` and `rules::engine`'s edge-detection get real unit tests (pure logic, highest-value
  place for bugs to hide quietly).
- Windows/macOS code paths compile (CI cross-compiles) but are unverified until run on real hardware.

## Explicitly out of scope for v1

The notification trigger's Windows/macOS equivalents — no portable "a notification was shown" API
exists on either OS (see M14/M20's "Why `OnNotification` stays Linux-only" note above). Everything
else originally scoped past v1 (the visualizer, the notification trigger on Linux) was in fact built
within v1 — see M12 and M9/M20 respectively.
