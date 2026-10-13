//! M0 spike: prove eframe + a system tray icon can coexist, and that closing
//! the "console" window doesn't kill the process (tray icon / background
//! work should survive). See the M0 milestone in the project plan.
//!
//! Finding from the first attempt: `ViewportCommand::Visible(false)` on the
//! ROOT viewport does not reliably hide the window under GNOME/Mutter on
//! Wayland (close_requested kept re-firing every frame, window stayed fully
//! visible). Revised approach: keep the root viewport as a permanent,
//! invisible 1x1 anchor that's never shown and never closed, and put the
//! actual console UI in a *deferred child viewport* that we simply stop
//! re-registering when the user closes it, and start re-registering again
//! when they ask to reopen it (tray click / menu). Closing a deferred
//! viewport doesn't touch the root, so the process (and tray icon) survives
//! regardless of what Wayland does with "hide."

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use eframe::egui;
use egui::ViewportId;
use muda::{Menu, MenuEvent, MenuItem};
use tray_icon::{TrayIcon, TrayIconBuilder, TrayIconEvent};

fn console_viewport_id() -> ViewportId {
    ViewportId::from_hash_of("quarzlampe-console")
}

struct SpikeApp {
    _tray_icon: TrayIcon,
    show_item: MenuItem,
    quit_item: MenuItem,
    console_open: bool,
    quit_requested: bool,
    counter: u64,
    // The deferred viewport's ui callback is a plain `Fn`, so it can't hold
    // `&mut self` - it flips this flag instead, which `logic()` reads/resets
    // next frame.
    console_close_requested: Arc<AtomicBool>,
}

impl SpikeApp {
    fn new() -> Self {
        let menu = Menu::new();
        let show_item = MenuItem::new("Show console", true, None);
        let quit_item = MenuItem::new("Quit", true, None);
        menu.append(&show_item).unwrap();
        menu.append(&quit_item).unwrap();

        let icon = load_icon();
        let tray_icon = TrayIconBuilder::new()
            .with_menu(Box::new(menu))
            .with_tooltip("Quarzlampe (M0 spike)")
            .with_icon(icon)
            .build()
            .expect("failed to build tray icon");

        Self {
            _tray_icon: tray_icon,
            show_item,
            quit_item,
            console_open: true,
            quit_requested: false,
            counter: 0,
            console_close_requested: Arc::new(AtomicBool::new(false)),
        }
    }
}

fn load_icon() -> tray_icon::Icon {
    let size = 16u32;
    let mut rgba = Vec::with_capacity((size * size * 4) as usize);
    for _ in 0..(size * size) {
        rgba.extend_from_slice(&[0x5b, 0xe6, 0xff, 0xff]);
    }
    tray_icon::Icon::from_rgba(rgba, size, size).expect("failed to build placeholder icon")
}

impl eframe::App for SpikeApp {
    // Runs every frame regardless of any viewport's visibility - this is
    // where tray/menu draining lives so it keeps working even while the
    // console viewport is closed.
    fn logic(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        while let Ok(event) = TrayIconEvent::receiver().try_recv() {
            if let TrayIconEvent::Click { .. } = event {
                eprintln!("[spike] tray click -> console_open=true");
                self.console_open = true;
            }
        }
        while let Ok(event) = MenuEvent::receiver().try_recv() {
            if event.id == self.show_item.id() {
                eprintln!("[spike] menu Show -> console_open=true");
                self.console_open = true;
            } else if event.id == self.quit_item.id() {
                eprintln!("[spike] menu Quit -> closing root");
                self.quit_requested = true;
                ctx.send_viewport_cmd_to(ViewportId::ROOT, egui::ViewportCommand::Close);
            }
        }

        if self.console_close_requested.swap(false, Ordering::SeqCst) {
            eprintln!("[spike] console close acknowledged -> console_open=false");
            self.console_open = false;
        }

        if self.console_open {
            let close_flag = Arc::clone(&self.console_close_requested);
            ctx.show_viewport_deferred(
                console_viewport_id(),
                egui::ViewportBuilder::default()
                    .with_inner_size([420.0, 240.0])
                    .with_title("Quarzlampe tray spike (M0)"),
                move |ui, _class| {
                    // Closing THIS viewport must not touch the root/process.
                    if ui.ctx().input(|i| i.viewport().close_requested()) {
                        eprintln!("[spike] console viewport close_requested");
                        ui.ctx().send_viewport_cmd(egui::ViewportCommand::CancelClose);
                        close_flag.store(true, Ordering::SeqCst);
                    }
                    ui.heading("Quarzlampe tray spike (M0) - deferred viewport");
                    ui.label("Closing this window should NOT kill the tray icon / process.");
                },
            );
        }

        ctx.request_repaint_after(Duration::from_millis(200));
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        // Root viewport is a permanent, invisible 1x1 anchor - nothing to draw.
        let _ = ui;
        self.counter += 1;
    }

    fn save(&mut self, _storage: &mut dyn eframe::Storage) {}
}

fn main() -> eframe::Result<()> {
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1.0, 1.0])
            .with_visible(false)
            .with_decorations(false)
            .with_taskbar(false),
        ..Default::default()
    };
    eframe::run_native(
        "quarzlampe-tray-spike",
        native_options,
        Box::new(|_cc| Ok(Box::new(SpikeApp::new()))),
    )
}
