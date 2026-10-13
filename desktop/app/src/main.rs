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

use std::collections::HashMap;
use std::process::{Child, Command};
use std::sync::Arc;
use std::time::{Duration, Instant};

use muda::{Menu, MenuEvent, MenuItem, PredefinedMenuItem, Submenu};
use quarzlampe_core::ble::{FileTokenStore, LampCommand, LampManager};
use quarzlampe_core::ipc::{DaemonState, IpcServer};
use quarzlampe_core::state::ConnectionState;
use tokio::sync::Mutex;
use tray_icon::{TrayIconBuilder, TrayIconEvent};

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
            let state = Arc::new(Mutex::new(DaemonState { manager, rules }));
            let _ = state_tx.send(Arc::clone(&state));
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

fn tooltip_text(lamps: &[LampSnapshot]) -> String {
    let connected = lamps.iter().filter(|l| l.connection == ConnectionState::Connected).count();
    let on = lamps.iter().filter(|l| l.connection == ConnectionState::Connected && l.lamp_on == Some(true)).count();
    if connected == 0 {
        "Quarzlampe \u{2014} no lamps connected".to_string()
    } else {
        format!("Quarzlampe \u{2014} {connected} connected, {on} on")
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

fn status_text(connection: &ConnectionState, on: Option<bool>, brightness: Option<f32>) -> String {
    match connection {
        ConnectionState::Connected => {
            let power = match on {
                Some(true) => "On",
                Some(false) => "Off",
                None => "?",
            };
            match brightness {
                Some(b) => format!("{power} \u{b7} {b:.0}%"),
                None => power.to_string(),
            }
        }
        ConnectionState::Connecting => "Connecting\u{2026}".to_string(),
        ConnectionState::AwaitingTrustToken => "Awaiting trust token\u{2026}".to_string(),
        ConnectionState::Reconnecting => "Reconnecting\u{2026}".to_string(),
        ConnectionState::Disconnected => "Disconnected".to_string(),
    }
}

/// Rebuilds the whole tray menu from scratch - muda (like every native menu
/// API) has no way to patch one item's label in place, so showing live lamp
/// state means replacing the entire `Menu` on a timer (see the main loop).
/// `show_item`/`quit_item` are the two items that never change, kept alive
/// across rebuilds and simply re-appended each time.
fn build_menu(lamps: &[LampSnapshot], index: &mut LampIndex, show_item: &MenuItem, quit_item: &MenuItem) -> Menu {
    let menu = Menu::new();

    if lamps.is_empty() {
        let _ = menu.append(&MenuItem::new("No lamps connected", false, None));
    } else {
        for lamp in lamps {
            let idx = index.idx_for(&lamp.key);
            let connected = lamp.connection == ConnectionState::Connected;
            let status = status_text(&lamp.connection, lamp.lamp_on, lamp.brightness_pct);
            let submenu = Submenu::new(format!("{} \u{2014} {}", lamp.name, status), true);

            let toggle_label = if lamp.lamp_on == Some(true) { "Turn off" } else { "Turn on" };
            let _ = submenu.append(&MenuItem::with_id(format!("lamp-{idx}-toggle"), toggle_label, connected, None));
            let _ = submenu.append(&PredefinedMenuItem::separator());
            for pct in [100u8, 75, 50, 25] {
                let _ = submenu.append(&MenuItem::with_id(
                    format!("lamp-{idx}-bri-{pct}"),
                    format!("Brightness: {pct}%"),
                    connected,
                    None,
                ));
            }

            let _ = menu.append(&submenu);
        }
    }

    let _ = menu.append(&PredefinedMenuItem::separator());
    let _ = menu.append(show_item);
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

fn main() -> anyhow::Result<()> {
    let show_item = MenuItem::with_id("show-console", "Show console", true, None);
    let quit_item = MenuItem::with_id("quit", "Quit", true, None);

    let starting_menu = Menu::new();
    let _ = starting_menu.append(&MenuItem::new("Starting\u{2026}", false, None));
    let _ = starting_menu.append(&PredefinedMenuItem::separator());
    let _ = starting_menu.append(&show_item);
    let _ = starting_menu.append(&quit_item);

    // Kept alive for the whole process lifetime - dropping this removes the
    // tray icon.
    let tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(starting_menu))
        .with_tooltip("Quarzlampe \u{2014} starting\u{2026}")
        .with_icon(render_status_icon(status_color(&[])))
        .build()?;

    let state_rx = spawn_core_runtime();
    let mut daemon_state: Option<Arc<Mutex<DaemonState>>> = None;
    let mut lamp_index = LampIndex::new();
    let mut last_refresh: Option<Instant> = None;

    let console_path = console_binary_path()?;
    let mut console_child: Option<Child> = None;

    println!("[tray] daemon running. Console binary: {}", console_path.display());
    loop {
        if daemon_state.is_none() && let Ok(s) = state_rx.try_recv() {
            daemon_state = Some(s);
        }

        if let Some(state) = &daemon_state {
            let needs_refresh = last_refresh.map(|t| t.elapsed() >= Duration::from_millis(1500)).unwrap_or(true);
            if needs_refresh {
                let lamps = snapshot_lamps(state);
                let menu = build_menu(&lamps, &mut lamp_index, &show_item, &quit_item);
                tray_icon.set_menu(Some(Box::new(menu)));
                let _ = tray_icon.set_icon(Some(render_status_icon(status_color(&lamps))));
                let _ = tray_icon.set_tooltip(Some(tooltip_text(&lamps)));
                last_refresh = Some(Instant::now());
            }
        }

        if let Ok(TrayIconEvent::Click { .. }) = TrayIconEvent::receiver().try_recv() {
            spawn_or_note(&mut console_child, &console_path);
        }
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == "show-console" {
                spawn_or_note(&mut console_child, &console_path);
            } else if event.id == "quit" {
                if let Some(mut child) = console_child.take() {
                    let _ = child.kill();
                }
                break;
            } else if let Some(state) = &daemon_state {
                handle_lamp_menu_event(&event.id.0, state, &lamp_index);
            }
        }
        std::thread::sleep(Duration::from_millis(100));
    }

    Ok(())
}
