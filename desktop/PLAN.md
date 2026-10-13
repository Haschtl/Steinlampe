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
- **BLE connections, the rule engine, and mic-active detection all live in the daemon
  (`desktop/app`), not the console.** Decided explicitly once the two-process split (see M0 below)
  raised the question: the whole point of the mic-trigger use case is background automation that
  works whether or not anyone has the console window open, so that logic can't live in a process
  that only exists while the console is open. The console becomes a thin client that talks to the
  daemon over local IPC (plain loopback TCP - see "Daemon/console IPC" below) to show live lamp
  state and send manual commands/rule edits; it has no BLE/tokio logic of its own.
- **Token storage: plain file, not the OS keyring.** The token only buys reconnect convenience, not
  a real security boundary (same reasoning the web frontend already applies to its `localStorage`
  token) - it's revocable by re-pairing, and a keyring dependency would add cross-platform
  complexity (not every Linux session has an unlocked keyring daemon running) for little real
  benefit. Already implemented as `core::ble::trust::FileTokenStore` - no change needed there.
- **Console UI direction: egui stays, redesign landed. ✅ DONE** (implemented in `console/src/main.rs`,
  verified live by hand) - two
  sections reachable from a left sidebar nav (Connections, Rules), not the current single scrolling
  panel. Connections: a card per lamp (name, colored status dot + text label, inline error banner,
  quick-actions baked into the card itself - power toggle, brightness slider, mode stepper, profile
  chips, all greyed out when not connected) plus a lighter "discovered nearby" row with Connect
  buttons below the card grid. Rules: compact rows (enable switch, one-line trigger→action→targets
  summary, fire/delete icon buttons) with an inline "+ New rule" form instead of a separate dialog.
  Visual direction (superseded, see below): dark surface, `system-ui` font stack so each OS still
  renders its own native typeface. Explicitly **not** a claim that egui renders native OS widgets - it doesn't, and the
  user accepted that tradeoff (see the exploratory discussion this decision came out of) in exchange
  for one Rust codebase; a true per-OS native rewrite of just the console stays a possible, deferred
  option *because* the console is already nothing but a thin IPC client - swapping it later
  wouldn't touch `core` or the daemon at all. Sketched and approved as a Claude Artifact
  (static mockup, not real egui code) before implementation:
  <https://claude.ai/artifact/5d6WTK125K6DgQhvNmi3bp>. In egui terms: `egui::Panel::left` for the
  nav (using `ui.selectable_label` for the two entries, not a custom button), one card per lamp via
  `ui.horizontal_wrapped` + a fixed-width `ui.scope` + `egui::Frame` (egui has no CSS grid, this
  wraps close enough), a hand-rolled `toggle_ui` pill switch (egui has no built-in one - standard
  egui-book recipe) for power, and the brightness slider commits on `drag_stopped()`/`lost_focus()`
  rather than every frame of the drag, so dragging doesn't flood the lamp with BLE writes. One real
  simplification from the mockup: profile slot buttons don't highlight a "currently active" slot,
  because the firmware's lightweight `STATE|` push (the only thing polled live, by design - see the
  "Live state" protocol note) doesn't carry which profile is active; showing one as selected would
  be fabricating data the app doesn't have. The old M3 multi-select-then-apply action bar is gone
  entirely, superseded by these per-card controls.
- **Native-look follow-up, decided explicitly after the user pushed back twice on the amber-accent
  egui redesign still not reading as native on any of the three OSes.** Laid out the real options
  plainly - stay on egui and accept the ceiling (free), rewrite the console per-OS in a real native
  toolkit (GTK4/libadwaita, SwiftUI, WinUI3 - true native look, but three codebases in three
  languages, weeks of work each, the daemon stays untouched either way since it's already IPC-
  decoupled), or move the whole desktop app to Tauri/webview (one codebase, can get visually closer
  via OS-aware CSS and Tauri's native vibrancy/acrylic window effects, but still not real native
  widgets - a better illusion, not a different category of thing, and it reopens the earlier "pure
  Rust" decision). **Decision: stay on egui for now, pro-OS native rewrite is the explicit future
  direction once there's time budget for it** - not reconsidering Tauri. In the meantime, three
  concrete asks landed instead of more color/spacing tweaking: the sidebar header now shows the
  real lamp icon (`egui::TextureHandle` loaded once from the same `assets/icon.png` the window icon
  already uses, via `ColorImage::from_rgba_unmultiplied` + `ctx.load_texture`), the whole palette
  was replaced with the web frontend's *actual* theme tokens read straight from
  `frontend/tailwind.config.cjs`/`index.css` (`bg #0b0f1a`, `panel #11182a`, `border #1f2b44`,
  `text #eef2ff`, `muted #9fb2cc`, `accent #5be6ff` - cyan, not the amber this file described
  above) rather than an invented "quartz glow" palette, and the sidebar footer got an
  `ui.hyperlink_to` to the real deployed web app (confirmed live and serving the actual app, not a
  404, before hardcoding the URL: `https://haschtl.github.io/Steinlampe/`) - all configuration
  (profiles, patterns, hardware settings, MIDI, OTA) lives there, not in the console, and the
  console doesn't try to duplicate it. Status-indicator amber (connecting/reconnecting) was kept as
  its own `WARNING` constant, deliberately not reusing the new brand-cyan `ACCENT`, so a status dot
  never shares meaning with "this is the selected/interactive thing." Verified live by hand: icon
  visible, colors read as the same product as the web app, link opens the real page in a browser.

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

**M0 done, verified on the real dev machine (Manjaro, GNOME, Wayland). The architecture below is the
final, working one — it replaced an earlier single-process attempt that worked but left a cosmetic
blemish; see "Rejected alternative" at the end for why.**

**Two-process architecture** (`desktop/app` = daemon, `desktop/console` = UI):
- `desktop/app` (`quarzlampe-tray`) is a tiny, **window-less** daemon: `tray-icon` (with
  `default-features = false, features = ["ksni"]` — the KSNI backend runs its own D-Bus worker
  thread and needs no event loop at all, so this binary has **no eframe/egui/winit dependency**) +
  `muda` for the menu, polling `TrayIconEvent::receiver()`/`MenuEvent::receiver()` in a plain
  `loop { ...; thread::sleep(100ms) }`. On "Show console" (tray click or menu item), it spawns
  `quarzlampe-console` as a child process if one isn't already running (tracked via
  `Option<std::process::Child>` + `try_wait()`); on "Quit" it kills that child (if any) and exits.
  The console binary is located as a sibling of the daemon's own exe
  (`std::env::current_exe()?.parent()`) — true for `cargo build`/`cargo run` since workspace
  binaries land in the same `target/<profile>/` dir, and a packaged release should ship them the
  same way.
- `desktop/console` (`quarzlampe-console`) is a perfectly ordinary `eframe` app — one real, visible,
  normal window, **no hide tricks at all**. Closing it just ends that process normally. The daemon
  doesn't care; it only tracks whether the child is alive to avoid spawning duplicates.
- Confirmed live: tray icon appears (Manjaro's GNOME ships the AppIndicator/KStatusNotifierItem
  extension by default — **stock/vanilla GNOME without it still won't show one**, that part of the
  original risk is real, just not applicable to this dev machine), clicking it spawns the console,
  closing the console window exits only that process (`ps` showed the console as a reaped/defunct
  child while the daemon PID kept running untouched), and critically: **no second/blank taskbar
  entry at all**, since the daemon never creates a window to begin with.
- Windows/macOS: `tray-icon`'s own docs say its backends piggyback on the native message/run loop
  "for free," but since this daemon has no event loop of its own on any platform (just a sleep-poll
  loop), that claim is moot here anyway — still, actually running this on Windows/macOS is
  unverified, no hardware available.

**Rejected alternative (for the record, don't re-attempt without new information)**: a single
eframe process with a permanent, invisible 1×1 "anchor" root viewport keeping the process alive,
and the real console as a `show_viewport_deferred` child viewport that could be closed/reopened
independently of the root. This **worked** (close → process survives → tray click → reopens, cycle
verified by hand) but had two problems that motivated the switch: (1) the invisible root still
showed as a permanent blank taskbar entry — `ViewportBuilder::with_taskbar(false)` and X11
`with_window_type` hints had no effect, Wayland has no standard "skip taskbar" hint, full stop; (2)
`request_repaint_after` is scoped to the *calling* viewport, not global, so the root's and the
deferred console's repaint schedules ran independently — missing a `request_repaint_after` call
inside the deferred viewport's own callback caused multi-second delays noticing clicks, and even
after fixing that, the root noticing the console's close (two independently-ticking contexts
handing off state through a shared `Arc<AtomicBool>`) stayed noticeably delayed. The two-process
split sidesteps both issues entirely rather than chasing them further.

## Daemon/console IPC

Since BLE/rules/mic detection all live in the daemon now, the console needs a way to see lamp state
and send commands/rule edits without any of that logic itself. Plain **loopback TCP**
(`127.0.0.1`, OS-assigned port), not a platform-specific transport (Unix sockets/named pipes via
`interprocess`, or D-Bus) - loopback TCP works identically via `std`/`tokio` on all three OSes with
no extra crate, which matters given none of this can be tested on Windows/macOS here. The daemon
writes `{port}\n{local_auth_token}` to a small file in the OS runtime/config dir on startup (via
`directories::ProjectDirs`) so the console can find and authenticate to it - the auth token is just
local hygiene (stop some other process on the same machine from connecting), not a real security
boundary. Message framing: newline-delimited JSON (`serde_json`), one `IpcRequest`/`IpcEvent` enum
pair defined once in `core` and shared by both sides - mirrors the "one enum, no restructuring
later" approach already used for `Trigger`/`Action`. This is an implementation detail, not a
product decision, so it wasn't put to the user as a choice - flagged here for visibility rather than
silently decided.

## BLE client design

Per-lamp tokio task ("actor"), one `btleplug::platform::Peripheral` each, living in the **daemon**
process and talking to the rest of the daemon only via channels — never `.await` on the egui thread
(moot for the daemon itself, which has no egui thread, but the same discipline applies to whatever
thread drives the IPC server). Core shapes:

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

**Three-crate workspace** under `desktop/` (revised from the original two-crate plan once M0 settled
on the two-*process* architecture above — `app` and `console` are separate binaries/processes, both
depending on `core` for shared logic):

```
desktop/
  Cargo.toml                  # workspace (members: core, app, console)
  core/
    src/
      ble/{uuids,line_codec,handshake,trust}.rs   # done (M1)
      ble/{lamp_actor,manager}.rs                 # M2: tokio-task-per-lamp, channel-based
      state.rs                  # LampState + STATE| parser
      mic/{mod,linux,windows,macos,unsupported}.rs
      rules/{model,engine,commands}.rs
      config/mod.rs
      ipc/{protocol,server,client}.rs             # M2: shared request/event enums + both ends
    examples/probe.rs           # done (M1): terminal-only exerciser, no GUI
  app/src/                      # the daemon - owns BLE/rules/mic, runs the IPC server
    main.rs                      # done (M0): window-less tray daemon, spawns console on demand
                                  # M2+: also starts the tokio runtime, LampManager, rule engine,
                                  # mic provider, and ipc::server
  console/src/                  # the UI - an ipc::client, no BLE/tokio logic of its own
    main.rs                      # done (M0, placeholder content): ordinary eframe app, no hide tricks
                                  # M2+: connects to the daemon's IPC port, renders lamp list /
                                  # rule editor / pairing flow from what the daemon reports
```

## Milestones (highest-risk unknowns first)

1. **M0 — Tray+egui integration. ✅ DONE**, verified by hand on the real Manjaro/GNOME/Wayland
   session. Landed on the two-process architecture (window-less `app` daemon + ordinary `console`
   binary spawned on demand) after an initial single-process attempt worked but left a cosmetic
   blank taskbar entry - see the "Highest-risk unknown" section above for the full story including
   the rejected alternative. No remaining known issues on this platform. Still outstanding: same
   test on Windows/macOS (no hardware available here) - the daemon's `console_binary_path()`
   sibling-exe lookup and the `.exe` suffix handling are written to be portable but unverified.
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
3. **M2 — Wire core::ble into the daemon, make the console a thin IPC client. ✅ DONE**, verified
   end to end against the real "ON-AIR Lampe" - scan found it, connect succeeded (and the firmware
   actually trusted this machine's BlueZ adapter address, no `PendingToken` rejection this time -
   unlike the M1 probe runs, which saw that path; the lamp's trusted-address list had presumably
   accumulated this address from earlier testing), `send_command("on")` turned the lamp on for real
   and `ListLamps` reported `lamp_on: true, brightness_pct: 100.0, pattern: 1` moments later (via a
   real `STATE|` push through the whole stack), `send_command("off")` turned it back off and the
   state updated again. Built: `core::ble::{lamp_actor, manager}` (tokio-task-per-lamp actor with
   capped-backoff reconnect, `LampManager` owning scan + a scanned-peripherals cache so a
   `Connect{lamp_key}` IPC request - which can only carry a string, not a live `btleplug` handle -
   can look the real peripheral back up), `core::state` (a `STATE|` line parser, unit-tested,
   deliberately *not* a dependency of the handshake - matches the "STATE| only, not the full
   snapshot, for ongoing display" scope decision), and `core::ipc::{protocol, server, client}`
   (newline-JSON over loopback TCP, exactly as decided above). The daemon starts all of this on a
   background OS thread with its own `tokio::Runtime`, alongside its existing synchronous tray-icon
   polling loop on the main thread - confirmed via a dead-end-then-resolved investigation that this
   split does *not* cause any D-Bus-library conflict between `tray-icon`'s `ksni`/zbus and
   `btleplug`'s `dbus`/`dbus-tokio` (several targeted repros ruled that out one by one); the
   apparent "hang" that prompted the investigation turned out to be a red herring - the daemon was
   publishing its discovery file to `/run/user/<uid>/quarzlampe/daemon.conn` (the XDG runtime dir)
   the whole time, and an earlier `find / -xdev` search had silently skipped it because `/run` is a
   separate tmpfs mount. Console UI is intentionally minimal for this milestone (a lamp list with
   On/Off buttons and a Scan button, polling `ListLamps` every 1.5s) - multi-select and
   brightness/mode/profile actions are M3's job.
4. **M3 — Console polish. ✅ DONE.** Added `core::rules::{model, commands}` now (just `Action` +
   `action_to_command`, unit-tested - `Trigger`/`Rule` stay deferred to M4, no point adding them
   unused). Console: checkbox per lamp + Select all/none, an action bar (On/Off, brightness slider,
   mode number, profile-slot number) that dispatches to every selected lamp through
   `action_to_command`, and `lamp_on`/`last_error` now shown per row. Verified live: `bri 40` and
   `mode 2` (the exact strings `action_to_command` produces) both took effect on the real lamp and
   the resulting `STATE|` push reported them back correctly.
   - **Gap found, not yet fixed (note for M6 hardening)**: `LampCommand::Disconnect` doesn't
     actually mean "stay disconnected" - `lamp_actor.rs`'s main loop treats *any* disconnect
     (deliberate or dropped) identically and always retries with backoff, so a lamp manually
     disconnected reconnects on its own a few seconds later if it's still in range and trusted.
     Harmless for now (M3 doesn't expose a disconnect button), but will need a real "stop trying"
     state before the console gets one.
5. **M4 — Rule engine + persisted config, manually fired. ✅ DONE.** Added `core::rules::model`
   (`Trigger` - just `MicActive` so far - and `Rule{id,name,enabled,trigger,targets,action}`) and
   `core::config` (`rules.toml` in the OS config dir via `directories::ProjectDirs`, atomic
   temp-file-then-rename save, loaded once at daemon startup). `ipc::server` now holds a
   `DaemonState{manager, rules}` behind one `Mutex` (a `FireRule` request needs both together) and
   added `ListRules`/`SaveRule`/`DeleteRule`/`FireRule` request handling - `SaveRule` does
   insert-or-update-by-id and persists the whole list on every change, `FireRule` looks a rule up by
   id and dispatches `action_to_command(&rule.action)` to every target lamp key, bypassing trigger
   detection entirely (that's M5's job - this only validates the Rule -> Action -> lamp pipeline).
   Console gained a "Rules" section: existing rules listed with an enable checkbox, "Fire now", and
   "Delete", plus a new-rule form (name field, action-kind picker reusing the same
   brightness/mode/profile number inputs as the M3 quick-action bar, and per-lamp target
   checkboxes) gated on name-non-empty + at least one target. Trigger is always `MicActive` for now
   since it's the only variant - no picker needed until M5's `Notification`/etc. variants exist.
   **Verified live end to end** via a raw Python IPC socket client (no lamp-side change needed, so
   no reason to involve the console UI itself for this check) against the real "ON-AIR Lampe":
   saved a rule targeting it, `FireRule` with action `on` turned it on (confirmed via the next
   `ListLamps` poll showing `lamp_on: true`), updating the same rule's action to
   `SetBrightness{pct:35}` and firing again changed brightness to exactly 35% in the next `STATE|`
   push, switching to `Off` and firing turned it back off - each change reflected through the real
   firmware round-trip, not just an in-memory assertion. Also verified `ListRules` reflects saves
   immediately, `DeleteRule` removes it from both the live list and `rules.toml` (confirmed by
   reading the file directly: `rules = []` after delete), and the daemon logs `loaded N rule(s)` on
   a fresh restart. No gaps found worth a hardening note this time - the only thing M4 deliberately
   left out (by design, see the milestone's own scope) is wiring a trigger to actually fire a rule
   on its own; that's M5.
5a. **Tray menu lamp controls (ad hoc, not a numbered milestone). ✅ DONE.** The user asked for the
   OS tray/menu-bar dropdown itself (not just the console window) to list every connected lamp with
   its live on/off/brightness state and quick controls. `app/src/main.rs` now rebuilds the whole
   `muda::Menu` from `DaemonState` every ~1.5s (native menu APIs have no in-place item patching, so
   a full rebuild is the only option) - one `Submenu` per lamp titled `"<name> — <status>"`
   (e.g. "ON-AIR Lampe — On · 72%"), containing a "Turn on"/"Turn off" toggle and four brightness
   presets (100/75/50/25%), everything disabled unless the lamp is `Connected` - same rule the
   console's quick-action controls already follow, kept consistent across both surfaces. The main
   thread (where the tray's synchronous event loop runs) gets the daemon's `Arc<Mutex<DaemonState>>`
   handed to it over a `std::sync::mpsc` channel sent once from the async core thread right after
   construction, then reads it with `tokio::sync::Mutex::blocking_lock()` - safe here specifically
   because the tray loop is plain synchronous code with no tokio runtime of its own, so there's no
   risk of the "blocking inside an async context" panic that method guards against. Menu item ids
   can't embed the raw lamp key directly (btleplug's Linux/BlueZ lamp keys are an opaque `Debug`
   string full of colons, braces and even an embedded null byte) - a small `LampIndex` assigns each
   lamp key a stable small integer the first time it's seen and keeps it for the daemon's lifetime,
   so `lamp-<idx>-toggle`/`lamp-<idx>-bri-<pct>` ids stay valid even if a click races a menu rebuild.
   A toggle click reads the lamp's *current* on/off state at the moment it's handled (not whatever
   was true when that menu was last built, up to ~1.5s earlier) so it's never sent backwards.
   **Verified live** against the real "ON-AIR Lampe" (user confirmed by hand: submenu showed the
   correct name/status, "Turn off" and a brightness preset both worked, menu text updated on the
   next refresh) - no further open issues.
5b. **Tray icon reflects state; console gets a real taskbar icon (ad hoc). ✅ DONE.** Two small
   follow-ups the user asked for right after 5a. The tray icon itself (not just its menu) is now a
   procedurally-drawn dot, recolored on the same ~1.5s refresh as the menu: grey when nothing's
   connected, a dimmed version of the brand lamp icon's cyan (`frontend/public/icon-lamp.svg`'s
   `#5be6ff`) when lamps are connected but all off, full brightness the moment any one is lit - a
   tiny "is anything on" LED readable without opening the menu. Tooltip text got the same treatment
   (`"N connected, M on"`). The console window now also carries a real icon - the same
   `icon-lamp.svg` the web frontend uses as its favicon, rasterized once ahead of time
   (`rsvg-convert`, since nothing already in the dependency tree can rasterize SVG) to
   `console/assets/icon.png` and loaded via the `image` crate into an `egui::IconData` at startup.
   **Found live, not anticipated**: `with_icon`'s pixel buffer alone did nothing in this GNOME/
   Wayland session - Wayland has no `_NET_WM_ICON`-style push from the app; GNOME Shell resolves a
   window's taskbar/dash/alt-tab icon from a `.desktop` file matching the window's `app_id`
   (`xdg_toplevel.set_app_id`, exposed as `ViewportBuilder::with_app_id`), not from anything the app
   draws itself. Fixed by setting `.with_app_id("dev.quarzlampe.console")` and installing a matching
   `dev.quarzlampe.console.desktop` (`Icon=` pointing at the rasterized PNG by absolute path,
   `NoDisplay=true` so it doesn't clutter the app grid/search as a fake "real" app) to
   `~/.local/share/applications/` on this dev machine. That desktop file currently points `Exec=`
   at the debug build's absolute path - correct for this dev machine only; a real packaged install
   will need to generate/ship its own desktop file pointing at wherever it actually installs the
   binary (out of scope now, no packaging/install step exists yet for any platform). Windows/macOS
   use their own, different icon mechanisms entirely (.ico resource embedding /.app bundle
   `Info.plist` icon respectively) - neither implemented, both unverified, no hardware here.
   **Verified live** (user confirmed): tray icon color matched lamp state, console window showed
   the lamp icon in the taskbar after the app-id + desktop-file fix.
6. **M5 — Mic trigger end-to-end on Linux. 🟡 MOSTLY DONE.** Added `core::mic` (`MicActivityProvider`
   trait + `default_provider()`) and `core::mic::linux::PulseMicProvider`: a source output with
   `corked == false` means some process is actively recording, matching PLAN's original note. Talks
   to `libpulse-binding` directly rather than through the obvious wrapper crate (`pulsectl-rs`),
   which turned out to be GPL-3.0-or-later - the wrong license for something about to be built into
   a binary offered for public download (see the CI/download section below); `libpulse-binding`
   itself is MIT/Apache-2.0, and the client library it binds (`libpulse`) is LGPL, so this stays
   clean. One deliberate deviation from the original sketch: the trait is `Result<MicActivity,
   String>` (`MicActivity` just `Active`/`Inactive`), not a 3-variant enum with `Unknown(String)` -
   same meaning, `Err` is the "unsupported/failed, never treat as off" case, just more idiomatic
   Rust. Also **not** `Send` on the trait, unlike the original sketch - the real Linux backend holds
   `Rc`s (as every `libpulse-binding` example does), so the provider has to be *constructed inside*
   its own dedicated polling thread, never built elsewhere and moved in; `app::spawn_mic_evaluation_loop`
   does exactly that, polls every 300ms, and on each observed `Inactive -> Active` edge (never on
   a failed poll) locks `DaemonState` just long enough to dispatch every enabled `MicActive` rule's
   action via the same `action_to_command`/`manager.send` path `FireRule` and the tray menu already
   use. `core::mic::unsupported` stubs out Windows/macOS (`Err` always) - still unwritten, unverified,
   no hardware here, exactly as planned.
   **Verified live in isolation**: a new `core/examples/mic_probe.rs` (same role as `probe.rs` has
   for BLE) printed `inactive` -> `ACTIVE` -> `inactive` in exact sync with starting/stopping a real
   `parecord` capture. **Not yet verified end-to-end against a real lamp** - both lamps in range hit
   unrelated live BLE issues when trying to connect this session (BlueZ had lost track of one
   device entirely; the other hit what looks like a trust-gate rejection, outside its token/boot
   window) - the user decided not to chase hardware access tonight, accepting the isolated mic-poll
   test plus M4's already-proven `Rule -> action_to_command -> manager.send` dispatch path (reused
   unchanged here) as sufficient evidence. Do the real end-to-end pass (speak near a connected lamp
   with an enabled `MicActive` rule, confirm it turns on by itself) next time hardware is at hand.
7. **M6 — Pairing/trust UX + multi-lamp hardening.** Scan/add flow with explicit 60s-window
   messaging, per-lamp reconnect-failure surfacing, config import/export, stress test with as many
   physical lamps as available connected at once.
8. **CI + download offer (ad hoc, not a numbered milestone). ✅ DONE.** `.github/workflows/
   desktop-build.yml`: a matrix build job (`ubuntu-latest`/`windows-latest`/`macos-latest`) that
   just builds the workspace with `cargo build --release --workspace` on each OS's own
   GitHub-hosted runner and uploads the two binaries as a plain workflow artifact - triggers on
   every push/PR touching `desktop/**`, same as the firmware's own `ci.yml`. Linux needs
   `libgtk-3-dev` (muda's default menu backend), `libdbus-1-dev` (btleplug/bluez-async) and
   `libpulse-dev` (M5's new `core::mic::linux`) installed via `apt-get` first; Windows/macOS need
   nothing extra - `muda`/`tray-icon` use `windows-sys`/`objc2` on those, not GTK, gated by target
   in their own `Cargo.toml`, confirmed by reading it rather than guessing. **This only proves the
   code builds on Windows/macOS** - BLE and mic detection have never run on either, no hardware
   there; the workflow's own release notes say so explicitly, and so does the Hilfe-page card (next
   paragraph) - don't let "CI is green" get mistaken for "verified" anywhere these builds are
   mentioned.
   A second job, `publish-release`, only runs on a manual `workflow_dispatch` (never on an ordinary
   push) and republishes a `desktop-latest` GitHub Release with the three platforms' binaries
   attached - kept manual on purpose: this is still an early, Linux-only-verified feature, so
   putting a public "download our app" link in front of end users (frontend's Hilfe page, see
   below) happening automatically on every commit felt like the wrong default; publishing is now a
   deliberate, one-click action instead. Added a "Desktop App (Beta)" card to
   `frontend/src/sections/HelpSection.tsx` (new `i18n.tsx` keys in both `en`/`de`) linking to that
   Release and to the Actions workflow page, with the Linux-tested/Windows-macOS-unverified caveat
   stated directly in the card text - verified by hand in a local dev-server preview of `/help`.
   **Not yet done, deliberately**: nobody has clicked "Run workflow" yet, so `desktop-latest`
   doesn't exist as a real Release until that happens (manually, by the user, after reviewing the
   workflow) - the Hilfe page's download link will 404 until then.
9. **Six more triggers, two more actions, start-on-boot (ad hoc). ✅ DONE, mostly verified live.**
   The user asked for a much richer trigger/action vocabulary than M4/M5's `MicActive`-only start,
   plus native autostart. All landed in one pass since they share no new infrastructure beyond
   what M4/M5 already built (the `Rule`/`Trigger`/`Action` serde shape, the IPC round-trip, the
   `fire_rules_where`/`fire_rules_where_async` dispatch helper generalized out of M5's
   `fire_mic_rules`).
   - **New `Trigger` variants**: `OnBoot` (fires once, right after rules load, from inside
     `spawn_core_runtime` - no new infra needed). `OnAppOpen { app_name }` - a `sysinfo`-based
     poller (`spawn_app_open_watch_loop`, its own thread, 1s cadence) diffing the running-process
     name set each tick; genuinely cross-platform, no OS-specific code, since `sysinfo` already is
     the portability layer. `OnBeforeSleep`/`BeforeShutdown` - Linux-only, `logind`'s
     `PrepareForSleep`/`PrepareForShutdown` D-Bus signals on the system bus via a `zbus` `#[proxy]`
     trait (`Login1Manager`), run as a tokio task (`spawn_power_event_watch`) on the daemon's
     existing runtime - only the `start=true` edge of each signal counts, not the `false` one that
     follows resume/cancellation. `OnNotification` - Linux-only, best-effort exactly as flagged
     before building it (the user explicitly accepted this risk): no portable "a notification
     appeared" API exists, so this switches a dedicated session-bus connection into D-Bus "monitor"
     mode (`org.freedesktop.DBus.Monitoring.BecomeMonitor`, the modern replacement for the
     deprecated `eavesdrop=true` match-rule flag) and watches every message for
     `interface=org.freedesktop.Notifications,member=Notify`. `OnKeyPress { key }` - a
     `global-hotkey` combo string (`"control+alt+KeyL"` format, i.e. lowercase modifiers + a
     `keyboard_types::Code` name); re-diffed against enabled rules on the same 1.5s cadence as the
     tray menu refresh (`sync_registered_hotkeys`), events polled in the main loop exactly like
     `MenuEvent`/`TrayIconEvent` already are.
   - **New `Action` variants**: `StartWake{seconds,soft,mode,brightness_pct}` and
     `SendMorse{text}`, matching the firmware's `wake`/`morse` syntax exactly (read straight from
     `src/command.cpp` by a research subagent, not guessed). `Action::Notify` already existed
     (M3) but had never been exposed as a rule-engine action kind in the console - just wired up
     the existing picker, no model change needed.
   - **`OnKeyPress` has a real, unfixable (by us) gap on two platforms**: `global-hotkey`'s own
     README says Linux is X11-only (confirmed: its Linux backend spins an X11 event-pumping thread
     internally, nothing Wayland-specific exists) - this dev machine runs GNOME/Wayland, so this
     trigger could not be exercised here *at all*, on either the Linux or any other path. Windows
     and macOS have their own, different gap: both need a native OS message/run loop pumped on the
     registering thread (Win32 `GetMessage`/`DispatchMessage`, Cocoa's `NSApplication` run loop),
     which this intentionally window-less daemon (see M0) does not have and was not reworked to
     provide - so `OnKeyPress` rules are expected to compile and save fine everywhere, but to
     silently never fire on Windows, macOS, or Wayland Linux, full stop, until someone either runs
     this on real X11 or the daemon architecture changes. The user explicitly chose to build it
     anyway, accepting zero verification coverage here, after being told this upfront.
   - **Start-on-boot**: a `autostart` module (`app/src/autostart.rs`), one `cfg`-gated
     implementation per OS behind the same `is_enabled()`/`set_enabled(bool)` pair - Linux writes/
     removes an XDG `.desktop` file under `~/.config/autostart/`; Windows writes/removes a
     `HKCU\...\Run` value via the `winreg` crate (API read directly from its downloaded source,
     not from memory, but never actually compiled - no Windows target available here at all, the
     strongest "unverified" label used anywhere in this project so far); macOS writes/removes a
     `~/Library/LaunchAgents/...plist`. Surfaced as a `CheckMenuItem` in the tray menu, re-synced
     from `autostart::is_enabled()` on every menu rebuild rather than trusted to hold its own state.
   - **Verified live** (this session, Linux): an `OnBoot` rule fired exactly once at a fresh daemon
     start; an `OnAppOpen` rule fired the moment a `yes` process was launched; an `OnNotification`
     rule fired when `notify-send` was run (saw it fire *twice* for one `notify-send` call once -
     a real quirk worth knowing about, not just a theoretical one, though not chased further given
     this trigger's already-accepted best-effort status); the `autostart` toggle was clicked on and
     off in the live tray menu and the `.desktop` file was confirmed to appear and disappear on
     disk each time. `OnBeforeSleep`/`BeforeShutdown`'s D-Bus subscription was confirmed to connect
     and register without error, but actually suspending/shutting down the dev machine to fire it
     for real was not attempted this session (disruptive, not worth it given the subscription
     mechanism itself is a well-established, low-risk pattern). `OnKeyPress` has no verification at
     all, for the reasons above - not even "it compiles and the manager initializes" was skipped,
     that part *was* confirmed (no `[hotkey]` error logged at startup on this Wayland machine,
     meaning `GlobalHotKeyManager::new()` itself succeeded, presumably via XWayland - but per the
     gap above, that is not the same as hotkeys actually working).
10. **Windows/macOS mic detection, and a real installer (ad hoc). ✅ DONE on Linux, written but
    unrun elsewhere.** The user explicitly asked for this knowing it can't be verified here -
    accepting that risk up front, not discovered partway through.
    - **`core::mic::windows`**: the same registry location Windows' own Settings > Privacy >
      Microphone dashboard reads (`HKCU\...\CapabilityAccessManager\ConsentStore\microphone`) -
      each app that's ever used the mic gets a subkey (desktop apps nest one level deeper under a
      `NonPackaged` subkey) with a `LastUsedTimeStop` REG_QWORD; `== 0` means using it *right now*.
      Via `winreg`, whose API was read from its real downloaded source (not memory) - same
      confidence level as the autostart Windows code already had, i.e. "should be right, never
      compiled, no Windows target available here at all."
    - **`core::mic::macos`**: CoreAudio's `kAudioDevicePropertyDeviceIsRunningSomewhere` on the
      system default input device, via raw `coreaudio-sys` FFI (`AudioObjectGetPropertyData` +
      `AudioObjectPropertyAddress`). This is the single least-verifiable piece of code in the
      entire project: `coreaudio-sys` generates its bindings via `bindgen` against the real macOS
      SDK *at build time*, on an actual Mac (or GitHub's `macos-latest` runner) - there's no local
      way to even confirm the generated symbol names, let alone compile it, from Linux. The C API
      itself (stable since Mac OS X 10.4) is written from direct knowledge, not guessed, but expect
      this file specifically to need a real fix-up pass once real CI/Mac feedback exists. Sidesteps
      one naming ambiguity on purpose: uses the raw `mElement` value `0` instead of betting on
      whether the installed SDK exposes `kAudioObjectPropertyElementMain` or the older
      `...ElementMaster` (Apple renamed it between SDK versions; both are numerically `0`).
    - **Why `OnBeforeSleep`/`BeforeShutdown`/`OnNotification` stay Linux-only, not a gap closed**:
      genuinely looked at extending each to Windows/macOS and deliberately stopped rather than
      writing speculative FFI with no real chance of working. Windows sleep detection *does* have a
      clean, window-less, documented API (`PowerRegisterSuspendResumeNotification`, a plain
      callback, no message loop needed) and would have been worth adding - not done this pass, left
      as a concrete follow-up, not a dead end. Windows shutdown and macOS sleep/shutdown all
      fundamentally need either a real top-level window (`WM_QUERYENDSESSION`, Windows) or an
      active run loop (`NSWorkspace` notifications, macOS) - the same architectural gap `OnKeyPress`
      already has on both platforms (see above), and reworking this window-less daemon's event loop
      just for these triggers wasn't judged worth it. `OnNotification` has no Windows/macOS
      equivalent at all - no "eavesdrop on other apps' notifications" mechanism exists on either,
      full stop, not just "harder."
    - **A real, simple installer**: `quarzlampe-tray --install` / `--uninstall` - running the
      downloaded binary with a flag, not a separate installer executable or a packaging format per
      platform (.deb/.msi/.dmg). Linux: copies both binaries to `~/.local/bin/`, installs the icon
      to the proper XDG icon-theme location (`~/.local/share/icons/hicolor/256x256/apps/`), writes
      a *visible* `.desktop` launcher to `~/.local/share/applications/` (separate from and
      complementary to `autostart.rs`'s own, deliberately-hidden `NoDisplay=true` one in
      `~/.config/autostart/` - different directories, different purposes, no collision). Windows:
      copies to `%LOCALAPPDATA%\Programs\Quarzlampe\`, creates a Start Menu shortcut by shelling
      out to a one-line PowerShell `WScript.Shell` script rather than writing `IShellLink` COM
      interop - every Windows install already has PowerShell, so this needed no new Win32 FFI
      surface at all. macOS: builds a real (if minimal, no `.icns`) `Quarzlampe.app` bundle under
      `~/Applications/` - just directory/file writes, no FFI, so meaningfully more trustworthy than
      the mic/CoreAudio code despite being equally unrun. None of the three touch the
      `autostart.rs` "Start on boot" toggle - that stays a separate, explicit, already-built opt-in.
      **Verified live end to end on Linux**: ran `--install`, confirmed all three artifacts
      (binaries, icon, `.desktop`) landed correctly and `desktop-file-validate` passed it, ran the
      *installed* binary standalone and confirmed it found its installed sibling console binary
      correctly, then ran `--uninstall` (from the installed binary itself, which deletes its own
      file while still running - works fine on Linux, the inode stays alive until the process
      exits) and confirmed everything was removed. Windows/macOS install paths are unverified, same
      caveat as the mic backends above. Added a short "run with --install" line to the Hilfe page's
      desktop-app card (both `en`/`de`) so this isn't buried only in the binary's own behavior.
11. **i18n: English/German, derived from the OS locale (ad hoc). ✅ DONE, verified live both ways.**
    `core::i18n`: a `Lang` enum (`En`/`De`) detected once at startup via the `sys-locale` crate
    (handles the actual per-OS lookup itself - `$LANG` on Linux, `GetUserDefaultLocaleName` on
    Windows, `NSLocale` on macOS - so no OS-specific code needed here at all), plus a `dict!` macro
    that generates a `t(lang, key) -> &'static str` lookup over a flat table of every *static*
    label (buttons, headings, field names) - same spirit as the web frontend's own `i18n.tsx`, not
    a framework. Shared between the tray daemon and the console since both need some of the same
    strings (the connection/power status words in particular). No in-app language switcher - purely
    locale-derived; a reasonable follow-up, not done here. The handful of strings that interpolate
    live data (the tray's tooltip/status text, the console's rule-description sentences built by
    `describe_trigger`/`describe_action`) aren't in the static table - each takes `lang` and does
    its own `match (lang, ...)` with `format!` directly, since a templating engine would be more
    machinery than four or five sentences are worth.
    **Verified live, both directions**: German (this machine's actual `$LANG=de_DE.UTF-8`) showed
    correctly throughout the tray menu and console on an ordinary launch with no overrides; relaunched
    the console with `LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8` and confirmed it switched fully to
    English. Not yet exercised on Windows/macOS (no hardware here, as always), but `sys-locale`
    itself is a small, widely-used crate handling exactly this lookup - lower risk than anything
    else cross-platform in this project, since the actual translation table is pure Rust with zero
    platform-specific code.
12. **Mic-inactive, camera trigger, music visualizer (ad hoc). ✅ DONE, mostly verified live.**
    Decided explicitly with the user: MQTT stays a *future* firmware-side idea, not built now (see
    the saved memory on why - the firmware has zero WiFi today, so this is its own project, not a
    small add); a daemon-side MQTT bridge was considered and declined outright.
    - **`Trigger::MicInactive`**: the falling edge `MicActive`'s rising edge was always missing.
      `app::spawn_mic_evaluation_loop` now fires it on `Active -> Inactive` the same way it already
      fired `MicActive` on the reverse edge - no new infrastructure, just the other half of a
      comparison that was already there. Verified live via `parecord`.
    - **`Trigger::CameraActive`/`CameraInactive`**: new `core::camera`, same shape as `core::mic`.
      Linux: scans every process's `/proc/<pid>/fd/*` for one pointing at a `/dev/video*` node (the
      same technique `fuser /dev/video0` uses) - no broker daemon exists for cameras the way
      PulseAudio brokers mics, so this is a plain `/proc` walk, not reused introspection code.
      Windows: the exact same `ConsentStore` registry mechanism as `core::mic::windows`, just under
      `\webcam`. **macOS deliberately has no implementation** - unlike CoreAudio's property
      selectors (`core::mic::macos`), which are ubiquitous in tutorials and recalled with real
      confidence, CoreMediaIO's equivalent `FourCharCode` constants are not something that could be
      verified or confidently recalled here; shipping a guessed constant would silently query the
      *wrong* property and report "inactive" forever with no error - a worse failure mode than the
      usual "unverified" caveat, so it was left out rather than guessed. **Verified live on Linux**:
      a new `core/examples/camera_probe.rs` correctly flagged `ACTIVE` while `ffmpeg -f v4l2 -i
      /dev/video0` held the device open; a saved `CameraActive`+`CameraInactive` rule pair fired
      exactly once each, in the right order, against a real (if momentary) camera open.
    - **Music-reactive visualizer**: scoped explicitly, after asking, to the simplest version -
      system-audio *loudness* (RMS) drives lamp brightness linearly, ~10Hz, no beat-detection or
      frequency bands (those were offered as pricier alternatives and declined). New
      `core::audio::{LoudnessProvider, default_provider}`. Linux: a genuine PulseAudio/PipeWire
      *recording* stream (not the introspection-only calls `core::mic::linux` uses) connected to
      `"<default_sink_name>.monitor"` - captures whatever the system is currently playing, never a
      real microphone. Windows: WASAPI loopback via `cpal`, not hand-rolled WASAPI COM FFI - cpal's
      own backend adds `AUDCLNT_STREAMFLAGS_LOOPBACK` automatically when `build_input_stream` is
      called on an *output* device (confirmed by reading cpal's WASAPI source, not guessed), so
      this is built against cpal's ordinary, published cross-platform API. **macOS has no
      implementation**: CoreAudio has no built-in loopback mode at all, unlike WASAPI - the usual
      workarounds (a virtual driver like BlackHole, or `ScreenCaptureKit`'s audio capture on macOS
      13+) are each their own separate project, not a small gap. Daemon side: `DaemonState` gained
      `visualizer: Option<VisualizerConfig>` (targets/gain/brightness range, *not* persisted -
      always `None` on a fresh daemon, a live mode rather than a rule) and `visualizer_error:
      Option<String>`; a new always-running `spawn_visualizer_loop` (same "always on, only acts
      while config is `Some`" shape as the trigger loops) maps RMS through `gain` into a brightness
      value and sends `bri <pct>` only when it actually changed, so silence doesn't flood the lamp
      with redundant BLE writes. Two new IPC calls (`SetVisualizer`/`GetVisualizer`). Console UI: a
      "Music Visualizer" panel under the lamp grid (enable checkbox, gain slider, per-lamp target
      chips, an inline error line if `visualizer_error` is set). **Verified live on Linux**: a new
      `core/examples/audio_probe.rs` tracked a real sine-tone test file's RMS correctly through
      `paplay`; the full daemon pipeline (`SetVisualizer` -> real RMS capture -> brightness
      computation -> `manager.send`) ran error-free for the duration of a 6s test tone against a
      real (but BLE-trust-rejected, so not actually connected) lamp key - the user explicitly
      accepted this as sufficient rather than rebooting a lamp for the final visual confirmation,
      same call as the mic-trigger verification earlier.
13. **Quick Rules (ad hoc). ✅ DONE, verified live via direct IPC.** Requested explicitly for
    "Otto-Normal-User": a way to flip one switch on a lamp card instead of hand-building a pair of
    rules in the editor. **Console-only packaging, no new automation capability and no new IPC
    calls** - `QuickRuleKind` (`core::rules::model`) is just a named bundle of `(Trigger, Action)`
    pairs (`rule_specs()`); the console expands it into ordinary `Rule`s via the *existing*
    `SaveRule`/`DeleteRule` requests, same as the manual rule editor already does. `Rule` gained a
    `quick_rule: Option<QuickRuleKind>` tag (`#[serde(default, skip_serializing_if =
    "Option::is_none")]`, so old `rules.toml` files without it still load) so a toggle can later
    find and remove exactly the rules it created. First (only) kind: `OnAir` - `MicActive -> On` +
    `MicInactive -> Off`, both scoped to one lamp. Console: a separator + labeled toggle + hint text
    at the bottom of each lamp card (`lamp_card` now takes `rules: &[Rule]`); `quick_rule_active()`
    checks whether both tagged rules already exist for that lamp key, toggling on calls
    `build_quick_rule()` (uses `i18n` for rule names, which is why the console builds the `Rule`s
    and not `core`) and sends two `SaveRule`s, toggling off sends `DeleteRule` for each matching id.
    **Verified live against the running daemon via a direct IPC script** (same lamp-less-but-real
    verification pattern as item 12, since the user has consistently declined rebooting a physical
    lamp just to confirm UI wiring): toggling on produces exactly two rules, both tagged
    `quick_rule: on_air`, both targeting only the one lamp key; toggling off removes exactly those
    two and nothing else, confirmed both in the daemon's in-memory `ListRules` response and in
    `rules.toml` on disk. No macOS/Windows gap here - this is pure console-side logic over triggers
    (`MicActive`/`MicInactive`) that already exist on every platform.
14. **macOS camera trigger + a more trustworthy mic backend, auto-update checks, code-signing CI
    scaffolding (ad hoc). ✅ DONE on Linux (where "done" means "runs and is IPC-verified"; the macOS
    pieces are genuinely new code, still unrun on real macOS hardware).**
    - **macOS camera trigger, finally implemented.** The earlier gap (see item 12's old reasoning,
      still visible in git history) was that CoreMediaIO's `FourCharCode` property-selector
      constants weren't something that could be verified or confidently recalled from this Linux
      dev environment, and a wrong guess would have compiled while silently always reporting
      "inactive" - worse than shipping nothing. That blocker is gone: `objc2-core-media-io` is a
      real, published, actively-maintained binding crate (confirmed via its docs.rs pages, not
      memory - exact function signatures for `CMIOObjectGetPropertyData`/
      `CMIOObjectGetPropertyDataSize`, the `CMIOObjectPropertyAddress` struct's fields, and every
      constant used, all checked one by one) whose constants come straight from Apple's real
      headers. New `core::camera::macos` enumerates every CMIO device via
      `kCMIOHardwarePropertyDevices` (CoreMediaIO has no "default input device" the way CoreAudio
      does) and reports `Active` if any one of them has `kCMIODevicePropertyDeviceIsRunningSomewhere`
      set - a real Apple-forum code snippet surfaced during research confirms this is the right
      selector/scope/element combination other tools actually use for this.
    - **macOS mic detection rebuilt on the same foundation.** `core::mic::macos` used raw
      `coreaudio-sys` FFI before - flagged at the time as "the least verifiable code in this
      project" because that crate generates its bindings fresh via `bindgen` against whatever SDK
      the build machine has, with no way to even inspect the generated names without a Mac. Swapped
      for `objc2-core-audio`, the CoreAudio sibling of the crate above, for the same reason: published,
      versioned bindings instead of regenerated-per-build ones. Logic is unchanged (poll
      `kAudioDevicePropertyDeviceIsRunningSomewhere` on the default input device), only the FFI
      layer underneath it is more trustworthy now.
    - **Auto-update checks - notify-only, deliberately not a self-replacing updater.** New
      `core::update::check_for_update`, called from a new always-running `app::spawn_update_check_loop`
      (once ~10s after daemon startup, then every 6 hours). Compares the running build's own
      embedded git commit (`core::update::BUILD_SHA`, baked in by a new `core/build.rs` from the
      `GITHUB_SHA` env var - `"dev"` for any plain local `cargo build`, which short-circuits the
      check entirely so ordinary development never sees a false "update available") against a
      `BUILD_SHA.txt` asset the release workflow now publishes alongside the binaries (deliberately
      *not* against the GitHub release/tag's own commit ref, which `action-gh-release` isn't
      documented to reliably move when republishing to an already-existing tag like
      `desktop-latest`). `DaemonState` gained `update_available: Option<UpdateInfo>`; new
      `GetUpdateStatus` IPC call; tray menu shows an extra "Update available..." item (opens the
      release page via the `open` crate) and the console shows the same as a sidebar hyperlink next
      to "Open web interface". **Deliberately notify-only, not a binary-swapping updater**, for two
      reasons: no code signing exists yet, so silently downloading and replacing a running binary
      with an unsigned one fetched over plain HTTPS would be exactly the kind of supply-chain-
      sensitive step that should wait until signing exists rather than being built around its
      absence; and replacing a *running* executable's file is a genuinely different operation per
      OS (Windows generally won't allow overwriting an open `.exe` without a separate
      swap-on-restart dance) that can't be exercised from this Linux dev environment anyway.
      **Verified live**: a new `core/examples/update_probe.rs` confirmed the dev-build short-circuit
      (`BUILD_SHA = dev`, no network call) and, built once with a fake `GITHUB_SHA` to force a real
      check, made a genuine call to the GitHub API and got back a real 404 (`desktop-latest` doesn't
      exist yet, since nobody has run `publish-release` against this workflow yet - confirmed
      separately by listing this repo's actual releases). The `ReleaseResponse`/`ReleaseAsset`
      deserialization shape was checked against a real existing release (the firmware's `0.3.0` tag)
      with a throwaway script, not just assumed. The full daemon/console wiring (`DaemonState`
      field, `GetUpdateStatus` IPC round-trip, the console's poll loop) was verified live against
      the running daemon: `GetUpdateStatus` correctly returns `available: null` on this dev build.
    - **Code-signing: free paths chosen over paid ones, per explicit decision.** Asked "wtf, no free
      way to sign for macOS/Windows?" - answer, and what got built:
      - **macOS: ad-hoc signing (free), Gatekeeper warning documented instead of paid-away.**
        There's genuinely no free way to remove Gatekeeper's "unidentified developer" warning
        itself - only a paid ($99/year) Apple Developer Program membership (Developer ID signing +
        notarization) does that. `codesign --force --deep --sign -` (the `-` means ad-hoc: no
        certificate, no account, just the Xcode CLI tools already on every `macos-latest` runner)
        runs unconditionally - originally on the two loose binaries directly in the workflow, now
        folded into `packaging/macos/build-dmg.sh` where it signs the whole `.app` bundle instead
        (see item 15). This satisfies Apple Silicon's blanket "must carry some signature to execute
        at all" requirement but does nothing for the download warning - that gets a documented
        bypass instead (right-click -> Open -> "Open Anyway"), added to the release notes and to
        the frontend's Help page (`help.desktopAppSigning`, en/de).
      - **Windows: real free signing via SignPath Foundation - conditional on actually open-sourcing
        the project first.** SignPath Foundation signs Windows binaries for free for qualifying
        open source projects, but requires an OSI-approved license with no proprietary
        components, a fully automated CI build tied to the public repo, and a published code
        signing policy - none of which this repo had (no LICENSE file at all = all-rights-reserved
        by default). Decided explicitly with the user to actually meet that bar: **GPL-3.0-or-later**
        was chosen (over MIT/Apache-2.0, also offered) and the repo got a real
        [LICENSE](../LICENSE) (the canonical FSF text, fetched from GitHub's own
        `choosealicense.com` license-template source rather than reconstructed from memory, since
        getting legal text wrong by paraphrasing would be worse than not adding one), `license =
        "GPL-3.0-or-later"` in every Cargo.toml and the frontend's `package.json`, and
        [desktop/CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md) (SignPath's required public
        policy doc: what gets signed, how - automatically, per commit, via
        `signpath/github-action-submit-signing-request@v3` - and who approves requests). The CI
        workflow's Windows path now uploads the two unsigned `.exe`s, submits a signing request,
        and copies the signed result into `dist/` - gated on job-level `env.SIGNPATH_API_TOKEN !=
        ''` (same step-env-not-visible-to-its-own-`if`-constraint as before) plus three
        `vars.SIGNPATH_*` values. **Still not active**: actually applying to SignPath
        (signpath.io/apply) is an identity-linked step only the repo owner can take, no amount of
        scaffolding activates it by itself. Until that happens the Windows binaries stay
        unsigned, same as today, with a documented SmartScreen bypass ("More info" -> "Run
        anyway") in the same two places as the macOS one.
      - Both paths are still unverified in the deeper sense: this YAML has never actually run (no
        push access from this environment), so even "ad-hoc signing succeeds" and "the SignPath
        action's inputs are exactly right" are written-correctly-by-inspection (the SignPath
        action's documented example was read directly from `docs.signpath.io`, not guessed), not
        CI-run-confirmed.
15. **Real native installers, not loose executable pairs (ad hoc).** Prompted by the user
    pointing out that `publish-release` only ever uploaded six loose files (tray + console per
    platform) - not "fertige Installer", just two executables each time, which the user has to
    find, pair up in one directory, and run `--install` on themselves. Replaced with a real
    packaged installer per platform, built by a small shell script per OS under
    `desktop/packaging/<platform>/`:
    - **Linux: an AppImage (`packaging/linux/build-appimage.sh`), verified live on this dev
      machine - the one platform-specific installer format actually run and confirmed working,
      not just written-by-inspection.** Built an `AppDir` (the two release binaries under
      `usr/bin/`, the real app icon, a `.desktop` file, a plain `AppRun` shell script execing
      `quarzlampe-tray`) and ran the official `appimagetool` (`AppImage/appimagetool`'s
      `continuous` release) against it. **Verified twice**, once with debug binaries while first
      getting the AppDir layout right and once again against the final committed script and real
      release binaries: running the resulting `.AppImage` directly printed the daemon's normal
      three startup log lines, and critically the `[tray] daemon running. Console binary: ...`
      line showed the *mounted* path (`/tmp/.mount_XXXXXX/usr/bin/quarzlampe-console`) resolved
      correctly - confirming `console_binary_path()`'s sibling-exe lookup still works from inside
      an AppImage's runtime-mounted squashfs, which wasn't obvious beforehand.
      - **Real bug found this way, not hypothetical**: `app::autostart`'s Linux `set_enabled` used
        `std::env::current_exe()` to build the "Start on boot" `.desktop` file's `Exec=` line. From
        inside a mounted AppImage that resolves to the temporary mount path, which stops existing
        the moment the process exits - enabling autostart while running from an AppImage would
        have silently written a `.desktop` file pointing nowhere, broken on the very next boot.
        Confirmed the actual mechanism live (dumped env vars from inside a running AppImage) before
        fixing it: AppImage's runtime sets `$APPIMAGE` to the real, permanent path of the
        `.AppImage` file itself specifically for this case - `exe_path()` now prefers that env var
        over `current_exe()` when it's set, with `current_exe()` as the fallback for every
        non-AppImage case (including the AppImage's own `install.rs` `--install` flow, which copies
        real bytes to a new file rather than referencing a path, so it was never actually affected
        by this - confirmed by re-reading that code, not assumed).
    - **macOS: a `.dmg` containing a real `Quarzlampe.app` bundle
      (`packaging/macos/build-dmg.sh`), unverified - no macOS machine here to run it.** Builds the
      bundle (`Contents/MacOS/` with both binaries, `Contents/Info.plist`, a `.icns` built from the
      one source PNG via `sips`+`iconutil` - both built into macOS, no third-party tool), ad-hoc
      signs the whole bundle (moved here from the plain `codesign` step the workflow had before -
      see item 14), then `hdiutil create`s a disk image with the `.app` plus a symlink to
      `/Applications` for the standard drag-to-install convention. No third-party packaging tool
      (no `create-dmg`) - everything used ships with macOS itself.
    - **Windows: a real `.msi` via WiX Toolset v3 (`packaging/windows/main.wxs`), unverified - no
      Windows machine or WiX toolchain here to compile it.** WiX v4/v5/v6's schema is still
      actively changing release to release (confirmed by reading multiple current doc pages that
      each showed a different `Product`/`Package` shape) - too much guessing risk for something
      that can't be compile-checked here at all. Used WiX v3 instead specifically because (a) it's
      already preinstalled on `windows-latest` at `$env:WIX`, no setup step needed, and (b) its
      schema has been stable and unchanging for over a decade, unlike v4+. The `.wxs` itself is
      adapted from `cargo-wix`'s own real v3 template (fetched directly from
      `volks73/cargo-wix`'s repository, not reconstructed from memory) for a two-binary workspace
      instead of that template's single-binary case, plus a Start Menu shortcut using the standard,
      long-stable WiX v3 "Component with a Shortcut + RemoveFolder + a dummy per-user
      `RegistryValue` KeyPath" recipe (a shortcut-only component needs its own `KeyPath` since MSI
      can't use the `Shortcut` element itself as one). Built via `candle.exe`/`light.exe` directly
      (not `cargo wix`'s own CLI, which assumes a single-package context and wasn't confirmed to
      handle a cross-package workspace's two separately-built binaries). Signing now targets this
      `.msi` file itself through the SignPath flow (item 14), not the two loose `.exe`s separately
      as before - signing the thing a user actually downloads and runs is what SmartScreen
      reputation actually cares about.
      - **First real CI run on Windows found a real failure, twice.** `candle.exe` errored with
        `CNDL0103 : The system cannot find the file '.1.0'` - the `-dVersion=0.1.0` define arriving
        mangled, missing its leading `0`. First guess (a PowerShell backtick line-continuation,
        notoriously whitespace-sensitive) turned out wrong - the *identical* error persisted after
        collapsing the command onto one line, which ruled that out cleanly. Checked `cargo-wix`'s
        own source (`create.rs`) directly before guessing again: confirms the single-concatenated
        `-dKey=Value` form (what was already being used) is exactly right for legacy candle.exe,
        with `-d Key=Value` as two separate args only for WiX v4+ - so the switch syntax itself
        wasn't the bug. Fixed by quoting every `-d`/path argument as its own PowerShell string
        (`"-dVersion=0.1.0"` etc.) so nothing is left as a bare, re-tokenizable token - removes
        shell-level argument splitting as a cause outright, regardless of the exact mechanism that
        was splitting it (never fully pinned down). Not yet confirmed fixed - waiting on the next
        CI run.
    - All three installer scripts are called from `.github/workflows/desktop-build.yml`'s `build`
      job; `publish-release` now uploads the packaged installer per platform as the recommended
      download (named prominently first in the release body) alongside the existing loose binaries
      (kept for anyone who prefers `quarzlampe-tray --install` over a packaged installer).

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
