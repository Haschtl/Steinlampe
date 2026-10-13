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

use std::process::{Child, Command};
use std::sync::Arc;
use std::time::Duration;

use muda::{Menu, MenuEvent, MenuItem};
use quarzlampe_core::ble::{FileTokenStore, LampManager};
use quarzlampe_core::ipc::IpcServer;
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
/// the tray's synchronous event-polling loop below.
fn spawn_core_runtime() {
    std::thread::spawn(|| {
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
                Ok(m) => Arc::new(Mutex::new(m)),
                Err(e) => {
                    eprintln!("[daemon] failed to initialize BLE manager: {e}");
                    return;
                }
            };
            if let Err(e) = IpcServer::bind_and_serve(manager).await {
                eprintln!("[daemon] IPC server error: {e}");
            }
        });
    });
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

fn load_icon() -> tray_icon::Icon {
    let size = 16u32;
    let mut rgba = Vec::with_capacity((size * size * 4) as usize);
    for _ in 0..(size * size) {
        rgba.extend_from_slice(&[0x5b, 0xe6, 0xff, 0xff]);
    }
    tray_icon::Icon::from_rgba(rgba, size, size).expect("failed to build placeholder icon")
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

fn main() -> anyhow::Result<()> {
    let menu = Menu::new();
    let show_item = MenuItem::new("Show console", true, None);
    let quit_item = MenuItem::new("Quit", true, None);
    menu.append(&show_item)?;
    menu.append(&quit_item)?;

    // Kept alive for the whole process lifetime - dropping this removes the
    // tray icon.
    let _tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Quarzlampe")
        .with_icon(load_icon())
        .build()?;

    spawn_core_runtime();

    let console_path = console_binary_path()?;
    let mut console_child: Option<Child> = None;

    println!("[tray] daemon running. Console binary: {}", console_path.display());
    loop {
        if let Ok(TrayIconEvent::Click { .. }) = TrayIconEvent::receiver().try_recv() {
            spawn_or_note(&mut console_child, &console_path);
        }
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == show_item.id() {
                spawn_or_note(&mut console_child, &console_path);
            } else if event.id == quit_item.id() {
                if let Some(mut child) = console_child.take() {
                    let _ = child.kill();
                }
                break;
            }
        }
        std::thread::sleep(Duration::from_millis(100));
    }

    Ok(())
}
