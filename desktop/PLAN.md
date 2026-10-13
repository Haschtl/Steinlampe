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
3. **M2 — Wire core::ble into the daemon, make the console a thin IPC client.** Resolved (decided
   with the user): BLE/rules/mic detection all live in `app` (the daemon), not `console` - see
   "Decisions already made" above. Build order: (a) `core::ble::{lamp_actor, manager}` - the
   tokio-task-per-lamp actor and `LampManager`, reusing `handshake.rs`/`trust.rs`/`line_codec.rs`
   from M1 (the probe only proved the handshake logic standalone, not this actor/channel wiring);
   (b) `core::ipc` - the shared request/event protocol plus server (daemon) and client (console)
   halves, loopback TCP as decided above; (c) wire the daemon's `main.rs` to start a tokio runtime,
   `LampManager`, and the IPC server alongside the existing tray-icon loop; (d) the console connects
   over IPC on startup and renders a lamp list with on/off only, reusing the real probe logic's
   proven connect/handshake behavior end to end, just via IPC instead of in-process. Because BLE now
   survives console close/reopen by construction (it lives in the daemon), there's no "reconnect
   from scratch" concern to design around.
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
