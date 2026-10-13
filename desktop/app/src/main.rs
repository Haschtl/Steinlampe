//! Tray daemon: the always-running background process. No window, no
//! GUI toolkit (eframe/egui/winit) at all - just a tray icon + menu, which
//! is all `tray-icon`'s `ksni` backend needs (it runs its own D-Bus worker
//! thread with no event loop required from us).
//!
//! Two-process architecture, chosen after the M0 spike (single-process,
//! eframe-owned invisible-root-viewport trick) turned out to leave a
//! permanent blank second entry in the GNOME/Wayland taskbar - an eframe
//! root viewport always creates a real OS window even when told to be
//! invisible, and there's no portable way to suppress that from the app
//! side (Wayland has no "skip taskbar" hint). Splitting the always-running
//! part (this daemon, genuinely window-less) from the on-demand UI (the
//! `quarzlampe-console` binary, a perfectly ordinary eframe app with no
//! hide tricks - closing it just exits that process normally) removes the
//! problem at the root instead of patching around it.
//!
//! The tray menu itself also shows every connected lamp with its live
//! on/off/brightness state and quick actions (toggle, brightness presets) -
//! rebuilt from `DaemonState` on a timer, since muda has no way to patch a
//! native menu in place item by item.

use std::collections::{HashMap, HashSet};
use std::process::{Child, Command};
use std::sync::Arc;
use std::time::{Duration, Instant};

use global_hotkey::hotkey::HotKey;
use global_hotkey::{GlobalHotKeyEvent, GlobalHotKeyManager, HotKeyState};
use muda::{CheckMenuItem, Menu, MenuEvent, MenuItem, PredefinedMenuItem, Submenu};
use quarzlampe_core::ble::{FileTokenStore, LampCommand, LampManager};
use quarzlampe_core::i18n::{t, Lang};
use quarzlampe_core::ipc::{DaemonState, IpcServer};
use quarzlampe_core::mic::{self, MicActivity};
use quarzlampe_core::rules::{action_to_command, Trigger};
use quarzlampe_core::state::ConnectionState;
use sysinfo::{ProcessesToUpdate, System};
use tokio::sync::Mutex;
use tray_icon::{TrayIconBuilder, TrayIconEvent};

mod autostart;
mod install;

/// Where lamp pairing tokens are cached between runs. Separate from the
/// IPC discovery file (`ipc::discovery_file_path`) - this one needs to
/// survive across daemon restarts, that one is republished fresh every
/// time the daemon starts.
fn token_store_dir() -> anyhow::Result<std::path::PathBuf> {
    let dirs = directories::ProjectDirs::from("dev", "quarzlampe", "quarzlampe")
        .ok_or_else(|| anyhow::anyhow!("could not determine a config directory on this platform"))?;
    Ok(dirs.data_dir().join("lamp-tokens"))
}

/// Starts the async core (BLE manager + IPC server) on its own tokio
/// runtime, on a background OS thread - the main thread stays free to run
/// the tray's synchronous event-polling loop below. Hands the constructed
/// `DaemonState` back to the main thread over a plain channel once it's
/// ready, so the tray loop can read live lamp state and dispatch commands
/// without a redundant IPC round-trip to itself.
fn spawn_core_runtime() -> std::sync::mpsc::Receiver<Arc<Mutex<DaemonState>>> {
    let (state_tx, state_rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let rt = match tokio::runtime::Runtime::new() {
            Ok(rt) => rt,
            Err(e) => {
                eprintln!("[daemon] failed to start tokio runtime: {e}");
                return;
            }
        };
        rt.block_on(async {
            let tokens = match token_store_dir().and_then(|dir| FileTokenStore::new(dir).map_err(Into::into)) {
                Ok(t) => Arc::new(t),
                Err(e) => {
                    eprintln!("[daemon] failed to open token store: {e}");
                    return;
                }
            };
            let manager = match LampManager::new(tokens).await {
                Ok(m) => m,
                Err(e) => {
                    eprintln!("[daemon] failed to initialize BLE manager: {e}");
                    return;
                }
            };
            let rules = quarzlampe_core::config::load_rules().unwrap_or_else(|e| {
                eprintln!("[daemon] failed to load rules.toml, starting with none: {e}");
                Vec::new()
            });
            println!("[daemon] loaded {} rule(s)", rules.len());
            let state = Arc::new(Mutex::new(DaemonState { manager, rules, visualizer: None, visualizer_error: None }));
            let _ = state_tx.send(Arc::clone(&state));

            #[cfg(target_os = "linux")]
            {
                spawn_power_event_watch(Arc::clone(&state)).await;
                spawn_notification_watch(Arc::clone(&state)).await;
            }
            fire_rules_where_async(&state, |t| *t == Trigger::OnBoot).await;

            if let Err(e) = IpcServer::bind_and_serve(state).await {
                eprintln!("[daemon] IPC server error: {e}");
            }
        });
    });
    state_rx
}

/// Finds the console binary, assumed to be a sibling of this daemon's own
/// executable (true for `cargo build`/`cargo run` - all workspace binaries
/// land in the same `target/<profile>/` directory; a packaged release build
/// should ship them the same way, side by side).
fn console_binary_path() -> std::io::Result<std::path::PathBuf> {
    let exe = std::env::current_exe()?;
    let dir = exe.parent().expect("executable always has a parent dir");
    let name = if cfg!(windows) { "quarzlampe-console.exe" } else { "quarzlampe-console" };
    Ok(dir.join(name))
}

/// Draws a filled dot (transparent outside the circle) in the given color -
/// the tray icon doubles as a little status LED, see `status_color` below.
fn render_status_icon(color: [u8; 3]) -> tray_icon::Icon {
    let size = 32u32;
    let mut rgba = vec![0u8; (size * size * 4) as usize];
    let center = size as f32 / 2.0 - 0.5;
    let radius = size as f32 / 2.0 - 1.5;
    for y in 0..size {
        for x in 0..size {
            let dx = x as f32 - center;
            let dy = y as f32 - center;
            if (dx * dx + dy * dy).sqrt() <= radius {
                let i = ((y * size + x) * 4) as usize;
                rgba[i] = color[0];
                rgba[i + 1] = color[1];
                rgba[i + 2] = color[2];
                rgba[i + 3] = 0xff;
            }
        }
    }
    tray_icon::Icon::from_rgba(rgba, size, size).expect("failed to build tray icon")
}

/// Grey when nothing is connected, a dimmed version of the brand's lamp-icon
/// cyan (`frontend/public/icon-lamp.svg`'s `#5be6ff` stroke) when lamps are
/// connected but all off, full brightness the moment any one of them is lit,
/// so the tray icon itself says "is anything on right now" at a glance
/// without opening the menu.
fn status_color(lamps: &[LampSnapshot]) -> [u8; 3] {
    let any_on = lamps.iter().any(|l| l.connection == ConnectionState::Connected && l.lamp_on == Some(true));
    let any_connected = lamps.iter().any(|l| l.connection == ConnectionState::Connected);
    if any_on {
        [0x5b, 0xe6, 0xff]
    } else if any_connected {
        [0x2a, 0x6b, 0x78]
    } else {
        [0x5b, 0x61, 0x70]
    }
}

fn tooltip_text(lang: Lang, lamps: &[LampSnapshot]) -> String {
    let connected = lamps.iter().filter(|l| l.connection == ConnectionState::Connected).count();
    let on = lamps.iter().filter(|l| l.connection == ConnectionState::Connected && l.lamp_on == Some(true)).count();
    if connected == 0 {
        format!("Quarzlampe \u{2014} {}", t(lang, "tray.tooltipNoLamps"))
    } else {
        format!(
            "Quarzlampe \u{2014} {connected} {}, {on} {}",
            t(lang, "tray.tooltipConnectedWord"),
            t(lang, "tray.tooltipOnWord")
        )
    }
}

fn spawn_or_note(console_child: &mut Option<Child>, console_path: &std::path::Path) {
    match console_child {
        Some(child) => match child.try_wait() {
            Ok(None) => println!("[tray] console already running (pid {})", child.id()),
            _ => *console_child = Command::new(console_path).spawn().ok(),
        },
        None => *console_child = Command::new(console_path).spawn().ok(),
    }
}

/// Assigns a stable small integer to each lamp key the first time it's
/// seen, and keeps it for the daemon's lifetime. Menu item ids are built
/// from this index rather than the raw lamp key, because `btleplug` lamp
/// keys are an opaque `Debug`-formatted platform id (colons, braces, even
/// an embedded null byte on this Linux/BlueZ backend) - not safe or sane
/// to round-trip through a menu id string. Staying stable across the
/// periodic menu rebuilds (rather than being re-assigned by iteration
/// order each time) is what makes a click on a freshly-rebuilt menu still
/// resolve correctly even if it raced a rebuild.
struct LampIndex {
    key_to_idx: HashMap<String, u32>,
    idx_to_key: HashMap<u32, String>,
    next: u32,
}

impl LampIndex {
    fn new() -> Self {
        Self { key_to_idx: HashMap::new(), idx_to_key: HashMap::new(), next: 0 }
    }

    fn idx_for(&mut self, lamp_key: &str) -> u32 {
        if let Some(&i) = self.key_to_idx.get(lamp_key) {
            return i;
        }
        let i = self.next;
        self.next += 1;
        self.key_to_idx.insert(lamp_key.to_string(), i);
        self.idx_to_key.insert(i, lamp_key.to_string());
        i
    }

    fn key_for(&self, idx: u32) -> Option<&str> {
        self.idx_to_key.get(&idx).map(String::as_str)
    }
}

struct LampSnapshot {
    key: String,
    name: String,
    connection: ConnectionState,
    lamp_on: Option<bool>,
    brightness_pct: Option<f32>,
}

fn snapshot_lamps(state: &Arc<Mutex<DaemonState>>) -> Vec<LampSnapshot> {
    let guard = state.blocking_lock();
    guard
        .manager
        .lamps()
        .map(|(key, handle)| {
            let s = handle.state_rx.borrow();
            LampSnapshot {
                key: key.clone(),
                name: handle.name.clone(),
                connection: s.connection.clone(),
                lamp_on: s.lamp_on,
                brightness_pct: s.brightness_pct,
            }
        })
        .collect()
}

fn status_text(lang: Lang, connection: &ConnectionState, on: Option<bool>, brightness: Option<f32>) -> String {
    match connection {
        ConnectionState::Connected => {
            let power = match on {
                Some(true) => t(lang, "status.on"),
                Some(false) => t(lang, "status.off"),
                None => t(lang, "status.unknown"),
            };
            match brightness {
                Some(b) => format!("{power} \u{b7} {b:.0}%"),
                None => power.to_string(),
            }
        }
        ConnectionState::Connecting => t(lang, "status.connecting").to_string(),
        ConnectionState::AwaitingTrustToken => t(lang, "status.awaitingTrust").to_string(),
        ConnectionState::Reconnecting => t(lang, "status.reconnecting").to_string(),
        ConnectionState::Disconnected => t(lang, "status.disconnected").to_string(),
    }
}

/// Rebuilds the whole tray menu from scratch - muda (like every native menu
/// API) has no way to patch one item's label in place, so showing live lamp
/// state means replacing the entire `Menu` on a timer (see the main loop).
/// `show_item`/`quit_item` are the two items that never change, kept alive
/// across rebuilds and simply re-appended each time.
fn build_menu(
    lang: Lang,
    lamps: &[LampSnapshot],
    index: &mut LampIndex,
    show_item: &MenuItem,
    autostart_item: &CheckMenuItem,
    quit_item: &MenuItem,
) -> Menu {
    autostart_item.set_checked(autostart::is_enabled());
    let menu = Menu::new();

    if lamps.is_empty() {
        let _ = menu.append(&MenuItem::new(t(lang, "tray.noLamps"), false, None));
    } else {
        for lamp in lamps {
            let idx = index.idx_for(&lamp.key);
            let connected = lamp.connection == ConnectionState::Connected;
            let status = status_text(lang, &lamp.connection, lamp.lamp_on, lamp.brightness_pct);
            let submenu = Submenu::new(format!("{} \u{2014} {}", lamp.name, status), true);

            let toggle_label = if lamp.lamp_on == Some(true) { t(lang, "tray.turnOff") } else { t(lang, "tray.turnOn") };
            let _ = submenu.append(&MenuItem::with_id(format!("lamp-{idx}-toggle"), toggle_label, connected, None));
            let _ = submenu.append(&PredefinedMenuItem::separator());
            for pct in [100u8, 75, 50, 25] {
                let _ = submenu.append(&MenuItem::with_id(
                    format!("lamp-{idx}-bri-{pct}"),
                    format!("{}: {pct}%", t(lang, "tray.brightness")),
                    connected,
                    None,
                ));
            }

            let _ = menu.append(&submenu);
        }
    }

    let _ = menu.append(&PredefinedMenuItem::separator());
    let _ = menu.append(show_item);
    let _ = menu.append(autostart_item);
    let _ = menu.append(quit_item);
    menu
}

/// Parses a `lamp-<idx>-toggle` / `lamp-<idx>-bri-<pct>` menu item id back
/// into a lamp key + action and dispatches it. Reads the lamp's *current*
/// on/off state at click time (not whatever was true when the menu was last
/// rebuilt, which can be up to ~1.5s stale) so a toggle click is never sent
/// in the wrong direction because of that staleness.
fn handle_lamp_menu_event(id: &str, state: &Arc<Mutex<DaemonState>>, index: &LampIndex) {
    let Some(rest) = id.strip_prefix("lamp-") else { return };
    let Some((idx_str, action)) = rest.split_once('-') else { return };
    let Ok(idx) = idx_str.parse::<u32>() else { return };
    let Some(lamp_key) = index.key_for(idx) else { return };

    let guard = state.blocking_lock();
    if action == "toggle" {
        let currently_on = guard
            .manager
            .lamps()
            .find(|(k, _)| k.as_str() == lamp_key)
            .and_then(|(_, h)| h.state_rx.borrow().lamp_on);
        let line = if currently_on == Some(true) { "off" } else { "on" };
        guard.manager.send(lamp_key, LampCommand::SendLine(line.to_string()));
    } else if let Some(pct) = action.strip_prefix("bri-") {
        guard.manager.send(lamp_key, LampCommand::SendLine(format!("bri {pct}")));
    }
}

/// Polls the platform's mic-activity provider on its own dedicated thread -
/// the provider isn't `Send` (see `core::mic`'s doc comment on why), so it
/// has to be constructed and used entirely on the thread that owns it,
/// never built elsewhere and moved in. On each observed `Inactive` ->
/// `Active` edge, fires every enabled `MicActive` rule exactly once (see
/// PLAN.md's "Evaluation loop" note on why that's a *rising edge*, not a
/// level check - a still-active mic must not keep re-firing every ~300ms).
fn spawn_mic_evaluation_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        let mut provider = mic::default_provider();
        let mut last = MicActivity::Inactive;
        let mut was_erroring = false;
        loop {
            match provider.poll() {
                Ok(activity) => {
                    if was_erroring {
                        println!("[mic] detection recovered");
                        was_erroring = false;
                    }
                    if last == MicActivity::Inactive && activity == MicActivity::Active {
                        fire_rules_where(&state, |t| *t == Trigger::MicActive);
                    } else if last == MicActivity::Active && activity == MicActivity::Inactive {
                        fire_rules_where(&state, |t| *t == Trigger::MicInactive);
                    }
                    last = activity;
                }
                Err(e) => {
                    // Never synthesize an edge from a failed poll - `Err`
                    // must never be treated as "mic is off" (or on).
                    if !was_erroring {
                        eprintln!("[mic] detection error: {e}");
                        was_erroring = true;
                    }
                }
            }
            std::thread::sleep(Duration::from_millis(300));
        }
    });
}

/// Mirrors `spawn_mic_evaluation_loop` exactly - see `core::camera`'s doc
/// comment for the per-OS detection story (Linux/Windows real, macOS
/// deliberately unimplemented).
fn spawn_camera_evaluation_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        let mut provider = quarzlampe_core::camera::default_provider();
        let mut last = quarzlampe_core::camera::CameraActivity::Inactive;
        let mut was_erroring = false;
        loop {
            match provider.poll() {
                Ok(activity) => {
                    if was_erroring {
                        println!("[camera] detection recovered");
                        was_erroring = false;
                    }
                    use quarzlampe_core::camera::CameraActivity;
                    if last == CameraActivity::Inactive && activity == CameraActivity::Active {
                        fire_rules_where(&state, |t| *t == Trigger::CameraActive);
                    } else if last == CameraActivity::Active && activity == CameraActivity::Inactive {
                        fire_rules_where(&state, |t| *t == Trigger::CameraInactive);
                    }
                    last = activity;
                }
                Err(e) => {
                    if !was_erroring {
                        eprintln!("[camera] detection error: {e}");
                        was_erroring = true;
                    }
                }
            }
            std::thread::sleep(Duration::from_millis(300));
        }
    });
}

/// Always running (like the mic/camera loops), but only ever acts while
/// `DaemonState.visualizer` is `Some` - checked fresh every tick rather
/// than started/stopped as its own thread, so there's no start/stop
/// thread-lifecycle to manage, just a config to read. ~10Hz (every
/// 100ms), matching the "simple: loudness -> brightness" scope agreed on,
/// see `core::audio`'s doc comment. Skips sending a `bri` command when
/// the computed value hasn't changed since the last tick, so silence (or
/// a sustained level) doesn't flood the lamp with redundant BLE writes.
fn spawn_visualizer_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        let mut provider = quarzlampe_core::audio::default_provider();
        let mut last_sent: HashMap<String, u8> = HashMap::new();
        let mut was_erroring = false;
        loop {
            let config = state.blocking_lock().visualizer.clone();
            match config {
                Some(cfg) => match provider.poll() {
                    Ok(Some(rms)) => {
                        if was_erroring {
                            println!("[visualizer] detection recovered");
                            was_erroring = false;
                        }
                        let scaled = (rms * cfg.gain).clamp(0.0, 1.0);
                        let span = f32::from(cfg.max_brightness.saturating_sub(cfg.min_brightness));
                        let bri = (f32::from(cfg.min_brightness) + scaled * span).round() as u8;
                        let mut guard = state.blocking_lock();
                        guard.visualizer_error = None;
                        for lamp_key in &cfg.targets {
                            if last_sent.get(lamp_key) != Some(&bri) {
                                guard.manager.send(lamp_key, LampCommand::SendLine(format!("bri {bri}")));
                                last_sent.insert(lamp_key.clone(), bri);
                            }
                        }
                    }
                    Ok(None) => {}
                    Err(e) => {
                        if !was_erroring {
                            eprintln!("[visualizer] detection error: {e}");
                            was_erroring = true;
                        }
                        state.blocking_lock().visualizer_error = Some(e);
                    }
                },
                None => last_sent.clear(),
            }
            std::thread::sleep(Duration::from_millis(100));
        }
    });
}

/// Shared by every trigger source that lives on a plain OS thread (mic
/// polling, the app-open watcher, the tray loop's key-press handling) -
/// `blocking_lock()` is safe here specifically because none of those run
/// inside a tokio task. `spawn_power_event_watch`/`spawn_notification_watch`
/// (Linux-only, zbus-driven) are genuine async tasks instead and use
/// `fire_rules_where_async` below rather than this one.
fn fire_rules_where(state: &Arc<Mutex<DaemonState>>, matches: impl Fn(&Trigger) -> bool) {
    let guard = state.blocking_lock();
    let matched: Vec<_> = guard.rules.iter().filter(|r| r.enabled && matches(&r.trigger)).collect();
    if matched.is_empty() {
        return;
    }
    for rule in matched {
        println!("[rules] firing \"{}\"", rule.name);
        let line = action_to_command(&rule.action);
        for lamp_key in &rule.targets {
            guard.manager.send(lamp_key, LampCommand::SendLine(line.clone()));
        }
    }
}

async fn fire_rules_where_async(state: &Arc<Mutex<DaemonState>>, matches: impl Fn(&Trigger) -> bool) {
    let guard = state.lock().await;
    let matched: Vec<_> = guard.rules.iter().filter(|r| r.enabled && matches(&r.trigger)).collect();
    if matched.is_empty() {
        return;
    }
    for rule in matched {
        println!("[rules] firing \"{}\"", rule.name);
        let line = action_to_command(&rule.action);
        for lamp_key in &rule.targets {
            guard.manager.send(lamp_key, LampCommand::SendLine(line.clone()));
        }
    }
}

/// Polls every running process's name once a second (cross-platform via
/// `sysinfo`, no OS-specific code needed here unlike the other triggers)
/// and fires `OnAppOpen` rules the moment a name appears that wasn't there
/// on the previous poll - "the app just started", not "the app is
/// running" (so it fires once per launch, not continuously).
fn spawn_app_open_watch_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        let mut sys = System::new();
        let mut seen: HashSet<String> = HashSet::new();
        loop {
            sys.refresh_processes(ProcessesToUpdate::All, true);
            let current: HashSet<String> =
                sys.processes().values().map(|p| p.name().to_string_lossy().to_lowercase()).collect();
            for name in current.difference(&seen) {
                let name = name.clone();
                fire_rules_where(&state, |t| {
                    matches!(t, Trigger::OnAppOpen { app_name } if app_name.to_lowercase() == name)
                });
            }
            seen = current;
            std::thread::sleep(Duration::from_secs(1));
        }
    });
}

/// Linux-only: `logind`'s `PrepareForSleep`/`PrepareForShutdown` D-Bus
/// signals on the system bus - the standard, portable (across desktop
/// environments) way to learn about an imminent suspend or shutdown on a
/// systemd-based Linux system. Each signal fires twice per event (once
/// with `start=true` just before, once with `start=false` after resume/on
/// cancellation) - only `true` is "about to happen".
#[cfg(target_os = "linux")]
#[zbus::proxy(
    default_service = "org.freedesktop.login1",
    default_path = "/org/freedesktop/login1",
    interface = "org.freedesktop.login1.Manager"
)]
trait Login1Manager {
    #[zbus(signal)]
    fn prepare_for_sleep(&self, start: bool) -> zbus::Result<()>;
    #[zbus(signal)]
    fn prepare_for_shutdown(&self, start: bool) -> zbus::Result<()>;
}

#[cfg(target_os = "linux")]
async fn spawn_power_event_watch(state: Arc<Mutex<DaemonState>>) {
    tokio::spawn(async move {
        use futures_util::StreamExt;

        let connection = match zbus::Connection::system().await {
            Ok(c) => c,
            Err(e) => {
                eprintln!("[power] could not connect to the system bus, sleep/shutdown triggers won't fire: {e}");
                return;
            }
        };
        let proxy = match Login1ManagerProxy::new(&connection).await {
            Ok(p) => p,
            Err(e) => {
                eprintln!("[power] logind not available, sleep/shutdown triggers won't fire: {e}");
                return;
            }
        };
        let (Ok(mut sleep_stream), Ok(mut shutdown_stream)) =
            (proxy.receive_prepare_for_sleep().await, proxy.receive_prepare_for_shutdown().await)
        else {
            eprintln!("[power] could not subscribe to logind signals, sleep/shutdown triggers won't fire");
            return;
        };
        loop {
            tokio::select! {
                Some(msg) = sleep_stream.next() => {
                    if let Ok(args) = msg.args() && args.start {
                        fire_rules_where_async(&state, |t| *t == Trigger::OnBeforeSleep).await;
                    }
                }
                Some(msg) = shutdown_stream.next() => {
                    if let Ok(args) = msg.args() && args.start {
                        fire_rules_where_async(&state, |t| *t == Trigger::BeforeShutdown).await;
                    }
                }
            }
        }
    });
}

/// Linux-only, best-effort: there is no portable API for "a notification
/// just appeared" short of watching the session bus for other processes'
/// calls to the notification daemon's `Notify` method - which requires
/// switching this connection into D-Bus "monitor" mode
/// (`org.freedesktop.DBus.Monitoring.BecomeMonitor`), the modern
/// replacement for the old `eavesdrop=true` match-rule flag. Bus policy on
/// some systems may refuse this; if so, this trigger silently never fires
/// rather than taking the daemon down - see PLAN.md for the "this was
/// explicitly flagged as fragile" context.
#[cfg(target_os = "linux")]
async fn spawn_notification_watch(state: Arc<Mutex<DaemonState>>) {
    tokio::spawn(async move {
        use futures_util::StreamExt;

        let connection = match zbus::Connection::session().await {
            Ok(c) => c,
            Err(e) => {
                eprintln!("[notify-trigger] could not connect to the session bus: {e}");
                return;
            }
        };
        let match_rule = "interface='org.freedesktop.Notifications',member='Notify',eavesdrop='true'".to_string();
        let become_monitor = connection
            .call_method(
                Some("org.freedesktop.DBus"),
                "/org/freedesktop/DBus",
                Some("org.freedesktop.DBus.Monitoring"),
                "BecomeMonitor",
                &(vec![match_rule], 0u32),
            )
            .await;
        if let Err(e) = become_monitor {
            eprintln!(
                "[notify-trigger] bus policy refused monitor mode, OnNotification rules will never fire: {e}"
            );
            return;
        }
        println!("[notify-trigger] watching the session bus for notifications");
        let mut stream = zbus::MessageStream::from(connection);
        while let Some(Ok(msg)) = stream.next().await {
            let header = msg.header();
            let is_notify = header.interface().map(|i| i.as_str()) == Some("org.freedesktop.Notifications")
                && header.member().map(|m| m.as_str()) == Some("Notify");
            if is_notify {
                fire_rules_where_async(&state, |t| *t == Trigger::OnNotification).await;
            }
        }
    });
}

/// `global-hotkey`'s Linux (X11) backend spins its own event-pumping
/// thread (confirmed by reading its source), so just constructing it here
/// and polling `GlobalHotKeyEvent::receiver()` like `MenuEvent`/
/// `TrayIconEvent` is enough - no event loop of our own needed, consistent
/// with the rest of this window-less daemon. Windows/macOS are a real
/// exception: their backends need a native message/run loop pumped on the
/// registering thread (Win32 `GetMessage`/`DispatchMessage`, Cocoa's
/// `NSApplication` run loop respectively - see the crate's own README),
/// which this daemon's plain `loop { ...; sleep(100ms) }` is not. Building
/// one just for this would mean reworking the whole M0 window-less-daemon
/// architecture - not done; `OnKeyPress` rules are expected to silently
/// never fire on Windows/macOS until that happens. `None` if the manager
/// itself fails to initialize (e.g. no X11 available at all, pure Wayland
/// with no XWayland) - `OnKeyPress` rules then simply never fire, same
/// graceful-degradation shape as every other trigger's platform gap.
fn init_hotkey_manager() -> Option<GlobalHotKeyManager> {
    match GlobalHotKeyManager::new() {
        Ok(m) => Some(m),
        Err(e) => {
            eprintln!("[hotkey] global hotkeys unavailable on this system, OnKeyPress rules won't fire: {e}");
            None
        }
    }
}

/// Keeps the OS's registered global hotkeys in sync with the current
/// `OnKeyPress` rules, re-diffed on the same cadence as the tray menu
/// refresh. Returns the new key-string -> id map (replacing the caller's
/// old one) so hotkey *events* (which only carry an id) can be mapped back
/// to a key string and from there to the rule(s) using it.
fn sync_registered_hotkeys(
    manager: &GlobalHotKeyManager,
    rules: &[quarzlampe_core::rules::Rule],
    previous: &HashMap<String, HotKey>,
) -> HashMap<String, HotKey> {
    let desired: HashMap<String, HotKey> = rules
        .iter()
        .filter(|r| r.enabled)
        .filter_map(|r| match &r.trigger {
            Trigger::OnKeyPress { key } => key.parse::<HotKey>().ok().map(|hk| (key.clone(), hk)),
            _ => None,
        })
        .collect();

    let stale: Vec<HotKey> =
        previous.iter().filter(|(k, _)| !desired.contains_key(*k)).map(|(_, hk)| *hk).collect();
    let new: Vec<HotKey> = desired.iter().filter(|(k, _)| !previous.contains_key(*k)).map(|(_, hk)| *hk).collect();
    if !stale.is_empty() {
        let _ = manager.unregister_all(&stale);
    }
    if !new.is_empty() && let Err(e) = manager.register_all(&new) {
        eprintln!("[hotkey] failed to register one or more hotkeys: {e}");
    }
    desired
}

fn main() -> anyhow::Result<()> {
    if let Some(action) = std::env::args().nth(1)
        && (action == "--install" || action == "--uninstall")
    {
        return install::run(&action);
    }

    let lang = Lang::detect();

    let show_item = MenuItem::with_id("show-console", t(lang, "tray.showConsole"), true, None);
    let autostart_item =
        CheckMenuItem::with_id("toggle-autostart", t(lang, "tray.startOnBoot"), true, autostart::is_enabled(), None);
    let quit_item = MenuItem::with_id("quit", t(lang, "tray.quit"), true, None);

    let starting_menu = Menu::new();
    let _ = starting_menu.append(&MenuItem::new(t(lang, "tray.starting"), false, None));
    let _ = starting_menu.append(&PredefinedMenuItem::separator());
    let _ = starting_menu.append(&show_item);
    let _ = starting_menu.append(&autostart_item);
    let _ = starting_menu.append(&quit_item);

    // Kept alive for the whole process lifetime - dropping this removes the
    // tray icon.
    let tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(starting_menu))
        .with_tooltip(format!("Quarzlampe \u{2014} {}", t(lang, "tray.starting")))
        .with_icon(render_status_icon(status_color(&[])))
        .build()?;

    let state_rx = spawn_core_runtime();
    let mut daemon_state: Option<Arc<Mutex<DaemonState>>> = None;
    let mut lamp_index = LampIndex::new();
    let mut last_refresh: Option<Instant> = None;

    let hotkey_manager = init_hotkey_manager();
    let mut registered_hotkeys: HashMap<String, HotKey> = HashMap::new();

    let console_path = console_binary_path()?;
    let mut console_child: Option<Child> = None;

    println!("[tray] daemon running. Console binary: {}", console_path.display());
    loop {
        if daemon_state.is_none() && let Ok(s) = state_rx.try_recv() {
            spawn_mic_evaluation_loop(Arc::clone(&s));
            spawn_camera_evaluation_loop(Arc::clone(&s));
            spawn_app_open_watch_loop(Arc::clone(&s));
            spawn_visualizer_loop(Arc::clone(&s));
            daemon_state = Some(s);
        }

        if let Some(state) = &daemon_state {
            let needs_refresh = last_refresh.map(|t| t.elapsed() >= Duration::from_millis(1500)).unwrap_or(true);
            if needs_refresh {
                let lamps = snapshot_lamps(state);
                let menu = build_menu(lang, &lamps, &mut lamp_index, &show_item, &autostart_item, &quit_item);
                tray_icon.set_menu(Some(Box::new(menu)));
                let _ = tray_icon.set_icon(Some(render_status_icon(status_color(&lamps))));
                let _ = tray_icon.set_tooltip(Some(tooltip_text(lang, &lamps)));
                if let Some(manager) = &hotkey_manager {
                    let rules = state.blocking_lock().rules.clone();
                    registered_hotkeys = sync_registered_hotkeys(manager, &rules, &registered_hotkeys);
                }
                last_refresh = Some(Instant::now());
            }
        }

        if let Ok(TrayIconEvent::Click { .. }) = TrayIconEvent::receiver().try_recv() {
            spawn_or_note(&mut console_child, &console_path);
        }
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == "show-console" {
                spawn_or_note(&mut console_child, &console_path);
            } else if event.id == "toggle-autostart" {
                let enabled = autostart::is_enabled();
                if let Err(e) = autostart::set_enabled(!enabled) {
                    eprintln!("[tray] failed to change start-on-boot setting: {e}");
                }
            } else if event.id == "quit" {
                if let Some(mut child) = console_child.take() {
                    let _ = child.kill();
                }
                break;
            } else if let Some(state) = &daemon_state {
                handle_lamp_menu_event(&event.id.0, state, &lamp_index);
            }
        }
        if let Ok(event) = GlobalHotKeyEvent::receiver().try_recv()
            && event.state() == HotKeyState::Pressed
            && let Some(state) = &daemon_state
            && let Some(key) = registered_hotkeys.iter().find(|(_, hk)| hk.id() == event.id()).map(|(k, _)| k.clone())
        {
            fire_rules_where(state, |t| matches!(t, Trigger::OnKeyPress { key: k } if *k == key));
        }
        std::thread::sleep(Duration::from_millis(100));
    }

    Ok(())
}
