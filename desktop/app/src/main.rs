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
use std::time::Duration;

use muda::{Menu, MenuEvent, MenuItem};
use tray_icon::{TrayIconBuilder, TrayIconEvent};

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

    let console_path = console_binary_path()?;
    let mut console_child: Option<Child> = None;

    println!("[tray] daemon running. Console binary: {}", console_path.display());
    loop {
        if let Ok(event) = TrayIconEvent::receiver().try_recv() {
            if let TrayIconEvent::Click { .. } = event {
                spawn_or_note(&mut console_child, &console_path);
            }
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
