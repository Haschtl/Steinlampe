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
use std::path::PathBuf;
use std::process::{Child, Command};
use std::sync::Arc;
use std::time::{Duration, Instant};

use chrono::Timelike;
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
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpListener;
use tokio::sync::Mutex;
use tray_icon::{TrayIcon, TrayIconBuilder, TrayIconEvent};
use uuid::Uuid;

mod platform;

#[cfg(any(windows, target_os = "macos"))]
use platform::SystemEvent;

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
            let state = Arc::new(Mutex::new(DaemonState {
                manager,
                rules,
                visualizer: None,
                visualizer_error: None,
                update_available: None,
                auth_token: Uuid::new_v4().to_string(),
                automations_paused: false,
            }));
            let _ = state_tx.send(Arc::clone(&state));

            #[cfg(target_os = "linux")]
            {
                spawn_power_event_watch(Arc::clone(&state)).await;
                spawn_notification_watch(Arc::clone(&state)).await;
                spawn_screen_lock_watch(Arc::clone(&state)).await;
            }
            fire_rules_where_async(&state, |t| *t == Trigger::OnBoot).await;
            spawn_auto_reconnect_loop(Arc::clone(&state));
            tokio::spawn(run_webhook_server(Arc::clone(&state)));

            if let Err(e) = IpcServer::bind_and_serve(state).await {
                eprintln!("[daemon] IPC server error: {e}");
            }
        });
    });
    state_rx
}

/// Parses a webhook request's `GET` line (`GET /webhook/<uuid>?token=<tok>
/// HTTP/1.1`) into the trigger id + the supplied token - no routing
/// framework, since this is a single hand-rolled endpoint shape, the same
/// "simple over generic" choice this daemon already made for the IPC
/// protocol (plain newline-JSON over TCP, not a full RPC framework).
fn parse_webhook_request_line(line: &str) -> Option<(Uuid, String)> {
    let mut parts = line.split_whitespace();
    if parts.next()? != "GET" {
        return None;
    }
    let target = parts.next()?;
    let (path, query) = target.split_once('?').unwrap_or((target, ""));
    let id = Uuid::parse_str(path.strip_prefix("/webhook/")?).ok()?;
    let token = query.split('&').find_map(|kv| kv.strip_prefix("token=")).unwrap_or("").to_string();
    Some((id, token))
}

/// A minimal, `GET`-only HTTP responder - just enough for a Stream Deck
/// "HTTP request" action, Home Assistant's `rest_command`, or `curl` to
/// trigger a rule, not a real HTTP server (no headers are parsed beyond the
/// request line, no keep-alive, every response closes the connection).
/// `127.0.0.1`-only (`run_webhook_server`'s listener), so the only real
/// defense this needs is against another local process/user guessing a
/// trigger's id - hence still checking `?token=` against the same
/// `auth_token` the IPC server already requires.
async fn handle_webhook_connection(stream: tokio::net::TcpStream, state: Arc<Mutex<DaemonState>>) -> anyhow::Result<()> {
    let (reader, mut writer) = stream.into_split();
    let mut lines = BufReader::new(reader).lines();
    let Some(line) = lines.next_line().await? else { return Ok(()) };

    let (status, body) = match parse_webhook_request_line(&line) {
        None => ("400 Bad Request", "bad request".to_string()),
        Some((id, token)) => {
            let auth_token = state.lock().await.auth_token.clone();
            if token != auth_token {
                ("403 Forbidden", "bad token".to_string())
            } else {
                fire_rules_where_async(&state, |t| matches!(t, Trigger::Webhook { id: trigger_id } if *trigger_id == id))
                    .await;
                ("200 OK", "ok".to_string())
            }
        }
    };
    let response =
        format!("HTTP/1.1 {status}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len());
    writer.write_all(response.as_bytes()).await?;
    Ok(())
}

/// Binds the fixed `quarzlampe_core::ipc::WEBHOOK_PORT` on loopback and
/// serves `Trigger::Webhook` requests forever - mirrors
/// `IpcServer::bind_and_serve`'s accept loop shape exactly, just for a
/// different wire protocol (HTTP instead of newline-JSON).
async fn run_webhook_server(state: Arc<Mutex<DaemonState>>) {
    let listener = match TcpListener::bind(("127.0.0.1", quarzlampe_core::ipc::WEBHOOK_PORT)).await {
        Ok(l) => l,
        Err(e) => {
            eprintln!("[webhook] could not bind port {}: {e}", quarzlampe_core::ipc::WEBHOOK_PORT);
            return;
        }
    };
    loop {
        match listener.accept().await {
            Ok((stream, _)) => {
                let state = Arc::clone(&state);
                tokio::spawn(async move {
                    if let Err(e) = handle_webhook_connection(stream, state).await {
                        eprintln!("[webhook] connection error: {e}");
                    }
                });
            }
            Err(e) => eprintln!("[webhook] accept error: {e}"),
        }
    }
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

/// A flat-top-and-bottom hexagon ("a diamond with the top and bottom
/// points cut off") instead of the plain dot the icon used to be - more
/// distinctive at a glance among a row of round/square tray icons, and
/// its shape naturally divides into horizontal bands. `segment_colors`
/// has one entry per lamp (already capped to 4 and stably ordered by
/// `ordered_lamp_colors` below) - 0 or 1 entries render as a single solid
/// fill, 2-4 split the hexagon into that many equal-height horizontal
/// bands, matching the "status LED" idea the old dot already had, just
/// per lamp instead of aggregated into one color. A thin transparent
/// vertical slit always runs down the middle, independent of the band
/// count/colors, so the icon reads as visibly "split" even with just
/// one lamp (a solid fill).
fn render_status_icon(segment_colors: &[[u8; 3]]) -> tray_icon::Icon {
    const SIZE: u32 = 48;
    const EMPTY: [u8; 3] = [0x5b, 0x61, 0x70]; // same muted grey the old "nothing connected" dot used

    let mut rgba = vec![0u8; (SIZE * SIZE * 4) as usize];
    let s = SIZE as f32;
    let cx = s / 2.0;
    let cy = s / 2.0;
    let r = s * 0.46;

    // Clockwise from top-left, in screen coordinates (y grows downward):
    // top edge, right point, bottom edge, left point - the ASCII sketch
    // this was drawn from (" __ " / "/  \" / "\__/") read literally.
    let verts: [(f32, f32); 6] = [
        (cx - r * 0.5, cy - r),
        (cx + r * 0.5, cy - r),
        (cx + r, cy),
        (cx + r * 0.5, cy + r),
        (cx - r * 0.5, cy + r),
        (cx - r, cy),
    ];

    let segments = segment_colors.len().clamp(1, 4);
    let top = cy - r;
    let band_height = (2.0 * r) / segments as f32;
    // A thin fully-transparent vertical slit straight down the middle -
    // purely cosmetic (the band coloring/count logic above is completely
    // unaffected by it), making the icon read as visibly "split" even
    // when there's only one color filling it.
    let slit_half_width = s * 0.025;

    for y in 0..SIZE {
        for x in 0..SIZE {
            let px = x as f32 + 0.5;
            let py = y as f32 + 0.5;
            if !point_in_convex_hexagon(px, py, &verts) {
                continue;
            }
            if (px - cx).abs() <= slit_half_width {
                continue; // leave this pixel transparent
            }
            let color = if segment_colors.is_empty() {
                EMPTY
            } else {
                let band = (((py - top) / band_height) as usize).min(segments - 1);
                segment_colors[band]
            };
            let i = ((y * SIZE + x) * 4) as usize;
            rgba[i] = color[0];
            rgba[i + 1] = color[1];
            rgba[i + 2] = color[2];
            rgba[i + 3] = 0xff;
        }
    }
    tray_icon::Icon::from_rgba(rgba, SIZE, SIZE).expect("failed to build tray icon")
}

/// Cross-product sign test against every edge in turn - works for any
/// convex polygon regardless of winding direction, since it only checks
/// that the point is on the *same* side of every edge, not which side.
fn point_in_convex_hexagon(px: f32, py: f32, verts: &[(f32, f32); 6]) -> bool {
    let mut sign = 0i32;
    for i in 0..verts.len() {
        let (x1, y1) = verts[i];
        let (x2, y2) = verts[(i + 1) % verts.len()];
        let cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
        let this_sign = if cross > 0.0 {
            1
        } else if cross < 0.0 {
            -1
        } else {
            0
        };
        if this_sign != 0 {
            if sign == 0 {
                sign = this_sign;
            } else if this_sign != sign {
                return false;
            }
        }
    }
    true
}

/// Grey when disconnected, a dimmed version of the brand's lamp-icon cyan
/// (`frontend/public/icon-lamp.svg`'s `#5be6ff` stroke) when connected but
/// off, full brightness when on - the same three-tier palette the icon
/// always used, just per lamp now instead of aggregated into one color
/// for the whole tray icon.
fn lamp_status_color(lamp: &LampSnapshot) -> [u8; 3] {
    if lamp.connection == ConnectionState::Connected && lamp.lamp_on == Some(true) {
        [0x5b, 0xe6, 0xff]
    } else if lamp.connection == ConnectionState::Connected {
        [0x2a, 0x6b, 0x78]
    } else {
        [0x5b, 0x61, 0x70]
    }
}

/// Up to 4 lamps' colors for the tray icon, in the same stable order the
/// tray submenu already uses (`LampIndex`) rather than whatever order the
/// underlying `HashMap` happens to iterate in - otherwise the hexagon's
/// segments could swap positions between menu rebuilds even when nothing
/// about the lamps themselves changed.
fn ordered_lamp_colors(lamps: &[LampSnapshot], index: &mut LampIndex) -> Vec<[u8; 3]> {
    let mut sorted: Vec<&LampSnapshot> = lamps.iter().collect();
    sorted.sort_by_key(|l| index.idx_for(&l.key));
    sorted.into_iter().take(4).map(lamp_status_color).collect()
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
        ConnectionState::ReconnectFailed => t(lang, "status.reconnectFailed").to_string(),
        ConnectionState::Disconnected => t(lang, "status.disconnected").to_string(),
    }
}

/// Rebuilds the whole tray menu from scratch - muda (like every native menu
/// API) has no way to patch one item's label in place, so showing live lamp
/// state means replacing the entire `Menu` on a timer (see the main loop).
/// `show_item`/`quit_item` are the two items that never change, kept alive
/// across rebuilds and simply re-appended each time.
#[allow(clippy::too_many_arguments)]
fn build_menu(
    lang: Lang,
    lamps: &[LampSnapshot],
    index: &mut LampIndex,
    show_item: &MenuItem,
    autostart_item: &CheckMenuItem,
    pause_item: &CheckMenuItem,
    update_available: bool,
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
            if lamp.connection == ConnectionState::ReconnectFailed {
                let _ = submenu.append(&PredefinedMenuItem::separator());
                let _ = submenu.append(&MenuItem::with_id(format!("lamp-{idx}-reconnect"), t(lang, "tray.reconnect"), true, None));
            }

            let _ = menu.append(&submenu);
        }
    }

    let _ = menu.append(&PredefinedMenuItem::separator());
    let _ = menu.append(show_item);
    let _ = menu.append(autostart_item);
    let _ = menu.append(pause_item);
    if update_available {
        let _ = menu.append(&MenuItem::with_id("open-update", t(lang, "tray.updateAvailable"), true, None));
    }
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
    } else if action == "reconnect" {
        guard.manager.send(lamp_key, LampCommand::Reconnect);
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

/// Checks once ~10s after startup (gives networking a moment to come up)
/// and then every six hours - see `core::update`'s doc comment for why
/// this is a notify-only check, never a silent self-replace. Six hours
/// against GitHub's unauthenticated REST rate limit (60 requests/hour per
/// IP) isn't close to a concern; a `"dev"`-tagged local build (see
/// `core/build.rs`) short-circuits inside `check_for_update` itself, so
/// this loop still runs but never actually calls out during ordinary
/// development.
fn spawn_update_check_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        std::thread::sleep(Duration::from_secs(10));
        loop {
            match quarzlampe_core::update::check_for_update() {
                Ok(Some(info)) => {
                    println!("[update] newer build available: {}", info.html_url);
                    state.blocking_lock().update_available = Some(info);
                }
                Ok(None) => {}
                Err(e) => eprintln!("[update] check failed: {e}"),
            }
            std::thread::sleep(Duration::from_secs(6 * 60 * 60));
        }
    });
}

const AUTO_RECONNECT_INTERVAL: Duration = Duration::from_secs(120);
const AUTO_RECONNECT_SCAN_SECONDS: u64 = 4;

/// Lamp keys referenced by an *enabled* rule's targets, or by the live
/// (not persisted - see `DaemonState::visualizer`'s doc comment) music
/// visualizer config's targets - the lamps something would actually act
/// on if they were connected, as opposed to one the user merely paired
/// once and never automated.
async fn automation_relevant_lamp_keys(state: &Arc<Mutex<DaemonState>>) -> HashSet<String> {
    let guard = state.lock().await;
    let mut keys: HashSet<String> =
        guard.rules.iter().filter(|r| r.enabled).flat_map(|r| r.targets.iter().cloned()).collect();
    if let Some(cfg) = &guard.visualizer {
        keys.extend(cfg.targets.iter().cloned());
    }
    keys
}

/// Of the given keys, the ones worth scanning for right now: never seen
/// this session at all, or seen but given up on (see item 16's
/// `ConnectionState::ReconnectFailed`). A lamp that's `Connected`,
/// `Connecting`, or still within its own backoff cycle (`Reconnecting`)
/// is deliberately left alone here - re-triggering `connect_by_key` for
/// it would tear down and restart its actor, resetting a retry cycle
/// that's already in progress for no benefit.
async fn lamps_needing_auto_reconnect(state: &Arc<Mutex<DaemonState>>, relevant: &HashSet<String>) -> HashSet<String> {
    let guard = state.lock().await;
    relevant
        .iter()
        .filter(|key| match guard.manager.lamps().find(|(k, _)| k.as_str() == key.as_str()) {
            None => true,
            Some((_, handle)) => handle.state_rx.borrow().connection == ConnectionState::ReconnectFailed,
        })
        .cloned()
        .collect()
}

/// Periodically re-scans for, and auto-connects, any lamp that something
/// (an enabled rule, or the live visualizer) actually depends on being
/// connected - so an automation keeps working across the lamp going out
/// of range and coming back, or being powered on well after the daemon
/// already booted, without anyone having to open the console and click
/// Connect/Reconnect by hand. Deliberately separate from `lamp_actor`'s
/// own give-up-after-5-failures mechanism (item 16): that's a tight,
/// per-lamp retry loop meant to stop hammering one unreachable device;
/// this is a coarse, infrequent "has anything important come back into
/// range" sweep, and the two are designed to not interfere with each
/// other (see `lamps_needing_auto_reconnect`'s doc comment). Only
/// scans at all when there's at least one lamp actually worth finding -
/// no rules/visualizer in use means no background radio activity from
/// this loop whatsoever.
fn spawn_auto_reconnect_loop(state: Arc<Mutex<DaemonState>>) {
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_secs(15)).await;
        loop {
            let relevant = automation_relevant_lamp_keys(&state).await;
            if !relevant.is_empty() {
                let needing_attention = lamps_needing_auto_reconnect(&state, &relevant).await;
                if !needing_attention.is_empty() {
                    let mut guard = state.lock().await;
                    match guard.manager.scan(Duration::from_secs(AUTO_RECONNECT_SCAN_SECONDS)).await {
                        Ok(found) => {
                            for lamp in found {
                                if needing_attention.contains(&lamp.lamp_key) {
                                    println!("[auto-reconnect] \"{}\" is back in range, connecting", lamp.name);
                                    let _ = guard.manager.connect_by_key(&lamp.lamp_key, lamp.name);
                                }
                            }
                        }
                        Err(e) => eprintln!("[auto-reconnect] scan failed: {e}"),
                    }
                }
            }
            tokio::time::sleep(AUTO_RECONNECT_INTERVAL).await;
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
    if guard.automations_paused {
        return;
    }
    let matched: Vec<_> =
        guard.rules.iter().filter(|r| r.enabled && r.triggers.iter().any(&matches)).collect();
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
    if guard.automations_paused {
        return;
    }
    let matched: Vec<_> =
        guard.rules.iter().filter(|r| r.enabled && r.triggers.iter().any(&matches)).collect();
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

/// Fires `AtTime` rules at most once per calendar minute - `chrono::Local`
/// rather than anything timezone-naive, so a laptop's lamp rules keep
/// matching the wall clock it's actually displaying across travel/DST,
/// not whatever timezone the daemon happened to start in. Dedupes on a
/// monotonically increasing "minute id" (`timestamp() / 60`) instead of
/// comparing `(hour, minute)` to the previous tick - comparing the pair
/// alone would refire every day once the clock comes back around to the
/// same wall-clock minute, which is exactly the once-a-day behavior
/// wanted, but it would *also* refire if this loop's own tick happened to
/// land on the same tuple twice in a row across a backwards clock jump (a
/// NTP correction, a laptop resuming with a stale RTC) - the minute id is
/// immune to that since it only ever increases.
fn spawn_time_trigger_loop(state: Arc<Mutex<DaemonState>>) {
    std::thread::spawn(move || {
        let mut last_minute_id: Option<i64> = None;
        loop {
            let now = chrono::Local::now();
            let minute_id = now.timestamp().div_euclid(60);
            if last_minute_id != Some(minute_id) {
                last_minute_id = Some(minute_id);
                let (hour, minute) = (now.hour() as u8, now.minute() as u8);
                fire_rules_where(&state, |t| matches!(t, Trigger::AtTime { hour: h, minute: m } if *h == hour && *m == minute));
            }
            std::thread::sleep(Duration::from_secs(5));
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

/// The `freedesktop.org` screen-saver spec's `ActiveChanged` signal, the
/// most broadly-implemented portable way to learn "the session just got
/// locked/unlocked" on Linux - GNOME and KDE both expose
/// `org.freedesktop.ScreenSaver` (GNOME additionally under
/// `org.gnome.ScreenSaver`, not needed here since a zbus signal proxy
/// matches on path+interface, not sender name). Other desktop environments
/// may simply never emit it - same graceful-degradation shape as every
/// other best-effort Linux trigger in this file (`OnNotification`,
/// `OnBeforeSleep`): the trigger just never fires there, nothing breaks.
#[cfg(target_os = "linux")]
#[zbus::proxy(
    default_service = "org.freedesktop.ScreenSaver",
    default_path = "/org/freedesktop/ScreenSaver",
    interface = "org.freedesktop.ScreenSaver"
)]
trait ScreenSaver {
    #[zbus(signal)]
    fn active_changed(&self, active: bool) -> zbus::Result<()>;
}

#[cfg(target_os = "linux")]
async fn spawn_screen_lock_watch(state: Arc<Mutex<DaemonState>>) {
    tokio::spawn(async move {
        use futures_util::StreamExt;

        let connection = match zbus::Connection::session().await {
            Ok(c) => c,
            Err(e) => {
                eprintln!("[screen-lock] could not connect to the session bus, lock/unlock triggers won't fire: {e}");
                return;
            }
        };
        let proxy = match ScreenSaverProxy::new(&connection).await {
            Ok(p) => p,
            Err(e) => {
                eprintln!("[screen-lock] org.freedesktop.ScreenSaver unavailable, lock/unlock triggers won't fire: {e}");
                return;
            }
        };
        let Ok(mut stream) = proxy.receive_active_changed().await else {
            eprintln!("[screen-lock] could not subscribe to ActiveChanged, lock/unlock triggers won't fire");
            return;
        };
        while let Some(msg) = stream.next().await {
            if let Ok(args) = msg.args() {
                if args.active {
                    fire_rules_where_async(&state, |t| *t == Trigger::ScreenLocked).await;
                } else {
                    fire_rules_where_async(&state, |t| *t == Trigger::ScreenUnlocked).await;
                }
            }
        }
    });
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
///
/// Observed live (with `notify-send` + a counting rule) firing *twice*
/// per logical notification on this dev machine's desktop environment -
/// two genuinely distinct `Notify` method calls, different senders and
/// serials both times, not the same message delivered twice. Something
/// in this session's notification stack (likely a forwarder/mirror of
/// some kind) is itself issuing a second call, not a bug in this
/// watcher or in `BecomeMonitor`. Deliberately not deduplicated here -
/// doing that by serial/sender would risk silently dropping a real
/// second notification from a different app that happens to arrive in
/// the same instant. Document this as a known caveat (a rule using
/// `Action::Notify` may blink twice in quick succession on some
/// systems) rather than papering over it.
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
            if !is_notify {
                continue;
            }
            // The Notify call's full signature is `susssasa{sv}i` (app_name,
            // replaces_id, app_icon, summary, body, actions, hints,
            // expire_timeout) - only `app_name` matters here, but zbus's
            // typed `deserialize` checks the target type's signature
            // against the message's actual one, so every field has to be
            // declared even though the rest are discarded.
            type NotifyArgs =
                (String, u32, String, String, String, Vec<String>, HashMap<String, zbus::zvariant::OwnedValue>, i32);
            let app_name = match msg.body().deserialize::<NotifyArgs>() {
                Ok((app_name, ..)) => app_name,
                Err(e) => {
                    eprintln!("[notify-trigger] could not read a Notify call's app name, skipping: {e}");
                    continue;
                }
            };
            let app_name_lower = app_name.to_lowercase();
            fire_rules_where_async(&state, |t| match t {
                Trigger::OnNotification { app_name_filter: None } => true,
                Trigger::OnNotification { app_name_filter: Some(filter) } => {
                    app_name_lower.contains(&filter.to_lowercase())
                }
                _ => false,
            })
            .await;
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
        .flat_map(|r| r.triggers.iter())
        .filter_map(|t| match t {
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

/// A fixed loopback port, bound for the whole process lifetime, used purely
/// as a cross-platform "only one of these binds successfully" mutex - a
/// second `quarzlampe-tray` launched while one is already running fails to
/// bind this and exits instead of creating a second tray icon, second BLE
/// manager, and a second (port-colliding) discovery file. Deliberately
/// separate from the IPC server's own listener (`ipc::discovery_file_path`),
/// which always binds to an OS-assigned port (`127.0.0.1:0`) - this one has
/// to be fixed precisely because its only job is "can a second instance
/// bind the exact same address".
const SINGLE_INSTANCE_PORT: u16 = 47_621;

/// `Some` (holding the bound listener - keep it alive for the process's
/// whole lifetime, dropping it releases the lock) if this is the only
/// running instance, `None` if another one already holds the port.
fn acquire_single_instance_lock() -> Option<std::net::TcpListener> {
    std::net::TcpListener::bind(("127.0.0.1", SINGLE_INSTANCE_PORT)).ok()
}

/// Linux-only, best-effort (same shape as `spawn_notification_watch`): the
/// daemon has no window of its own to show a dialog from, so a desktop
/// notification is the only way to tell a user who just relaunched it that
/// nothing new happened. Silently does nothing if the session bus or the
/// notification daemon isn't reachable - the `eprintln!` right before this
/// is called is the only guaranteed-visible trace on Windows/macOS or a
/// headless Linux session.
#[cfg(target_os = "linux")]
fn notify_already_running(lang: Lang) {
    let Ok(rt) = tokio::runtime::Runtime::new() else { return };
    rt.block_on(async {
        let Ok(connection) = zbus::Connection::session().await else { return };
        let hints: HashMap<String, zbus::zvariant::Value> = HashMap::new();
        let _ = connection
            .call_method(
                Some("org.freedesktop.Notifications"),
                "/org/freedesktop/Notifications",
                Some("org.freedesktop.Notifications"),
                "Notify",
                &(
                    "Quarzlampe",
                    0u32,
                    "",
                    "Quarzlampe",
                    t(lang, "singleInstance.alreadyRunning"),
                    Vec::<String>::new(),
                    hints,
                    5000i32,
                ),
            )
            .await;
    });
}

#[cfg(not(target_os = "linux"))]
fn notify_already_running(_lang: Lang) {}

/// Everything one `tick()` touches, bundled so the same loop body can run
/// from either a plain `loop { ctx.tick(); sleep(...); }` (Linux/Windows) or
/// an `NSTimer` callback (macOS - see `platform::macos`'s doc comment for
/// why that platform can't use the plain-loop shape at all).
struct MainLoopCtx {
    lang: Lang,
    show_item: MenuItem,
    autostart_item: CheckMenuItem,
    pause_item: CheckMenuItem,
    quit_item: MenuItem,
    tray_icon: TrayIcon,
    state_rx: std::sync::mpsc::Receiver<Arc<Mutex<DaemonState>>>,
    daemon_state: Option<Arc<Mutex<DaemonState>>>,
    lamp_index: LampIndex,
    last_refresh: Option<Instant>,
    hotkey_manager: Option<GlobalHotKeyManager>,
    registered_hotkeys: HashMap<String, HotKey>,
    console_path: PathBuf,
    console_child: Option<Child>,
}

impl MainLoopCtx {
    fn new(lang: Lang) -> anyhow::Result<Self> {
        let show_item = MenuItem::with_id("show-console", t(lang, "tray.showConsole"), true, None);
        let autostart_item = CheckMenuItem::with_id(
            "toggle-autostart",
            t(lang, "tray.startOnBoot"),
            true,
            autostart::is_enabled(),
            None,
        );
        let pause_item =
            CheckMenuItem::with_id("toggle-automations-paused", t(lang, "tray.pauseAutomations"), true, false, None);
        let quit_item = MenuItem::with_id("quit", t(lang, "tray.quit"), true, None);

        let starting_menu = Menu::new();
        let _ = starting_menu.append(&MenuItem::new(t(lang, "tray.starting"), false, None));
        let _ = starting_menu.append(&PredefinedMenuItem::separator());
        let _ = starting_menu.append(&show_item);
        let _ = starting_menu.append(&autostart_item);
        let _ = starting_menu.append(&pause_item);
        let _ = starting_menu.append(&quit_item);

        // Kept alive for the whole process lifetime - dropping this removes
        // the tray icon.
        let tray_icon = TrayIconBuilder::new()
            .with_menu(Box::new(starting_menu))
            .with_tooltip(format!("Quarzlampe \u{2014} {}", t(lang, "tray.starting")))
            .with_icon(render_status_icon(&[]))
            .build()?;

        let state_rx = spawn_core_runtime();
        let hotkey_manager = init_hotkey_manager();
        let console_path = console_binary_path()?;

        println!("[tray] daemon running. Console binary: {}", console_path.display());

        Ok(Self {
            lang,
            show_item,
            autostart_item,
            pause_item,
            quit_item,
            tray_icon,
            state_rx,
            daemon_state: None,
            lamp_index: LampIndex::new(),
            last_refresh: None,
            hotkey_manager,
            registered_hotkeys: HashMap::new(),
            console_path,
            console_child: None,
        })
    }

    /// One iteration of the main loop. Returns `false` once "quit" was
    /// selected - the caller (the plain loop on Linux/Windows, the
    /// `NSTimer` callback on macOS) is responsible for actually stopping at
    /// that point, this just reports it.
    fn tick(&mut self) -> bool {
        if self.daemon_state.is_none()
            && let Ok(s) = self.state_rx.try_recv()
        {
            spawn_mic_evaluation_loop(Arc::clone(&s));
            spawn_camera_evaluation_loop(Arc::clone(&s));
            spawn_app_open_watch_loop(Arc::clone(&s));
            spawn_visualizer_loop(Arc::clone(&s));
            spawn_update_check_loop(Arc::clone(&s));
            spawn_time_trigger_loop(Arc::clone(&s));
            self.daemon_state = Some(s);
        }

        if let Some(state) = &self.daemon_state {
            let needs_refresh =
                self.last_refresh.map(|t| t.elapsed() >= Duration::from_millis(1500)).unwrap_or(true);
            if needs_refresh {
                let lamps = snapshot_lamps(state);
                let update_available = state.blocking_lock().update_available.is_some();
                self.pause_item.set_checked(state.blocking_lock().automations_paused);
                let menu = build_menu(
                    self.lang,
                    &lamps,
                    &mut self.lamp_index,
                    &self.show_item,
                    &self.autostart_item,
                    &self.pause_item,
                    update_available,
                    &self.quit_item,
                );
                self.tray_icon.set_menu(Some(Box::new(menu)));
                let icon_colors = ordered_lamp_colors(&lamps, &mut self.lamp_index);
                let _ = self.tray_icon.set_icon(Some(render_status_icon(&icon_colors)));
                let _ = self.tray_icon.set_tooltip(Some(tooltip_text(self.lang, &lamps)));
                if let Some(manager) = &self.hotkey_manager {
                    let rules = state.blocking_lock().rules.clone();
                    self.registered_hotkeys = sync_registered_hotkeys(manager, &rules, &self.registered_hotkeys);
                }
                self.last_refresh = Some(Instant::now());
            }
        }

        // Drains everything currently queued on each channel, not just one
        // event - a plain `if let` here left events to trickle out one per
        // ~100ms tick whenever they queued up faster than that (observed on
        // macOS: its native menu tracking runs its own nested event loop
        // while a menu is open, backing up clicks/menu selections that our
        // timer-driven tick can otherwise only drain one at a time), which
        // read as random extra latency and things firing well after the
        // interaction that caused them.
        while let Ok(TrayIconEvent::Click { .. }) = TrayIconEvent::receiver().try_recv() {
            spawn_or_note(&mut self.console_child, &self.console_path);
        }
        while let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == "show-console" {
                spawn_or_note(&mut self.console_child, &self.console_path);
            } else if event.id == "toggle-autostart" {
                let enabled = autostart::is_enabled();
                if let Err(e) = autostart::set_enabled(!enabled) {
                    eprintln!("[tray] failed to change start-on-boot setting: {e}");
                }
            } else if event.id == "toggle-automations-paused" {
                if let Some(state) = &self.daemon_state {
                    let mut guard = state.blocking_lock();
                    guard.automations_paused = !guard.automations_paused;
                    println!(
                        "[tray] automations {}",
                        if guard.automations_paused { "paused" } else { "resumed" }
                    );
                }
            } else if event.id == "open-update" {
                if let Some(state) = &self.daemon_state
                    && let Some(info) = state.blocking_lock().update_available.clone()
                    && let Err(e) = open::that(&info.html_url)
                {
                    eprintln!("[update] failed to open {}: {e}", info.html_url);
                }
            } else if event.id == "quit" {
                if let Some(mut child) = self.console_child.take() {
                    let _ = child.kill();
                }
                return false;
            } else if let Some(state) = &self.daemon_state {
                handle_lamp_menu_event(&event.id.0, state, &self.lamp_index);
            }
        }
        while let Ok(event) = GlobalHotKeyEvent::receiver().try_recv() {
            if event.state() == HotKeyState::Pressed
                && let Some(state) = &self.daemon_state
                && let Some(key) =
                    self.registered_hotkeys.iter().find(|(_, hk)| hk.id() == event.id()).map(|(k, _)| k.clone())
            {
                fire_rules_where(state, |t| matches!(t, Trigger::OnKeyPress { key: k } if *k == key));
            }
        }

        true
    }

    /// The Windows/macOS equivalents of the `Trigger` variants `main.rs`
    /// already fires natively on Linux via zbus (`spawn_power_event_watch`,
    /// `spawn_screen_lock_watch`) - see `platform`'s doc comment for why
    /// those two platforms need their own native plumbing to deliver these
    /// at all.
    #[cfg(any(windows, target_os = "macos"))]
    fn handle_system_event(&mut self, event: SystemEvent) {
        let Some(state) = &self.daemon_state else { return };
        match event {
            SystemEvent::BeforeSleep => fire_rules_where(state, |t| *t == Trigger::OnBeforeSleep),
            SystemEvent::BeforeShutdown => fire_rules_where(state, |t| *t == Trigger::BeforeShutdown),
            SystemEvent::ScreenLocked => fire_rules_where(state, |t| *t == Trigger::ScreenLocked),
            SystemEvent::ScreenUnlocked => fire_rules_where(state, |t| *t == Trigger::ScreenUnlocked),
        }
    }
}

fn main() -> anyhow::Result<()> {
    if let Some(action) = std::env::args().nth(1)
        && (action == "--install" || action == "--uninstall")
    {
        return install::run(&action);
    }

    let lang = Lang::detect();

    let Some(_single_instance_lock) = acquire_single_instance_lock() else {
        eprintln!("[tray] {}", t(lang, "singleInstance.alreadyRunning"));
        notify_already_running(lang);
        return Ok(());
    };

    // Must happen before anything below touches AppKit (the tray icon) or
    // CoreBluetooth (`MainLoopCtx::new()` starts both) - see
    // `platform::macos::init_app`'s doc comment for why getting this
    // ordering wrong meant the Bluetooth permission prompt silently never
    // appeared at all.
    #[cfg(target_os = "macos")]
    platform::macos::init_app();

    let ctx = MainLoopCtx::new(lang)?;

    // macOS needs `NSApplication` actually `-run`ning for the tray icon and
    // hotkeys to receive anything at all (see `platform::macos`'s doc
    // comment) - that call owns the thread until quit, so it replaces the
    // loop below entirely rather than being driven by it.
    #[cfg(target_os = "macos")]
    {
        platform::macos::run(ctx);
        return Ok(());
    }

    #[cfg(not(target_os = "macos"))]
    {
        let mut ctx = ctx;
        // Same reasoning as macOS, but Windows' message pump is non-blocking
        // (`PeekMessageW`), so it slots into the existing sleep-based loop
        // instead of needing to own it.
        #[cfg(windows)]
        let event_rx = platform::windows::init();
        loop {
            #[cfg(windows)]
            {
                platform::windows::pump();
                while let Ok(event) = event_rx.try_recv() {
                    ctx.handle_system_event(event);
                }
            }
            if !ctx.tick() {
                break;
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        Ok(())
    }
}
