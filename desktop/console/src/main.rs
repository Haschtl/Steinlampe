//! The on-demand console UI: a perfectly ordinary eframe app, spawned by the
//! tray daemon (`quarzlampe-tray`) when the user wants it and left to exit
//! normally when its window is closed - no hide-to-tray trick, no invisible
//! anchor viewport. The daemon is what stays running, owning BLE/rules/mic
//! detection; this process is a thin IPC client (`core::ipc`) that only
//! shows lamp state and forwards manual actions.

use std::collections::HashSet;
use std::sync::mpsc as std_mpsc;
use std::time::Duration;

use eframe::egui;
use quarzlampe_core::ipc::{IpcClient, IpcResponse, LampSummary, RequestKind, ScannedLamp};
use quarzlampe_core::rules::{action_to_command, Action};
use tokio::sync::mpsc as tokio_mpsc;

enum UiToCore {
    Scan,
    Connect { lamp_key: String, name: String },
    SendCommand { lamp_key: String, line: String },
}

enum CoreToUi {
    Lamps(Vec<LampSummary>),
    ScanResults(Vec<ScannedLamp>),
    Status(String),
}

/// Runs on a background tokio runtime (its own OS thread - eframe owns the
/// main thread synchronously). Connects to the daemon (retrying until it's
/// up), then polls `ListLamps` periodically and forwards UI-initiated
/// requests, relaying everything to the UI thread over a plain
/// `std::sync::mpsc` channel it can drain non-blockingly each frame.
async fn run_core(mut from_ui: tokio_mpsc::UnboundedReceiver<UiToCore>, to_ui: std_mpsc::Sender<CoreToUi>) {
    let mut client = loop {
        match IpcClient::connect().await {
            Ok(c) => break c,
            Err(e) => {
                let _ = to_ui.send(CoreToUi::Status(format!("waiting for daemon: {e}")));
                tokio::time::sleep(Duration::from_secs(1)).await;
            }
        }
    };
    let _ = to_ui.send(CoreToUi::Status("connected to daemon".to_string()));

    let mut poll = tokio::time::interval(Duration::from_millis(1500));
    loop {
        tokio::select! {
            _ = poll.tick() => {
                match client.request(RequestKind::ListLamps).await {
                    Ok(IpcResponse::Lamps { lamps }) => { let _ = to_ui.send(CoreToUi::Lamps(lamps)); }
                    Ok(IpcResponse::Error { message }) => { let _ = to_ui.send(CoreToUi::Status(message)); }
                    Ok(_) => {}
                    Err(e) => { let _ = to_ui.send(CoreToUi::Status(e.to_string())); }
                }
            }
            msg = from_ui.recv() => {
                let Some(msg) = msg else { break };
                let kind = match msg {
                    UiToCore::Scan => RequestKind::Scan { seconds: 4 },
                    UiToCore::Connect { lamp_key, name } => RequestKind::Connect { lamp_key, name },
                    UiToCore::SendCommand { lamp_key, line } => RequestKind::SendCommand { lamp_key, line },
                };
                match client.request(kind).await {
                    Ok(IpcResponse::ScanResults { lamps }) => { let _ = to_ui.send(CoreToUi::ScanResults(lamps)); }
                    Ok(IpcResponse::Error { message }) => { let _ = to_ui.send(CoreToUi::Status(message)); }
                    Ok(_) => {}
                    Err(e) => { let _ = to_ui.send(CoreToUi::Status(e.to_string())); }
                }
            }
        }
    }
}

struct ConsoleApp {
    to_core: tokio_mpsc::UnboundedSender<UiToCore>,
    from_core: std_mpsc::Receiver<CoreToUi>,
    lamps: Vec<LampSummary>,
    scan_results: Vec<ScannedLamp>,
    status: String,
    selected: HashSet<String>,
    brightness_pct: u8,
    mode_pattern: u32,
    profile_slot: u8,
}

impl ConsoleApp {
    /// Sends one `Action`, translated through the same
    /// `rules::commands::action_to_command` the rule engine will reuse in
    /// M4, to every currently-selected lamp.
    fn dispatch_to_selected(&self, action: &Action) {
        let line = action_to_command(action);
        for lamp_key in &self.selected {
            let _ = self.to_core.send(UiToCore::SendCommand { lamp_key: lamp_key.clone(), line: line.clone() });
        }
    }
}

impl eframe::App for ConsoleApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        while let Ok(msg) = self.from_core.try_recv() {
            match msg {
                CoreToUi::Lamps(lamps) => {
                    // Drop selections for lamps that disappeared (e.g. shut down).
                    let known: HashSet<&String> = lamps.iter().map(|l| &l.lamp_key).collect();
                    self.selected.retain(|k| known.contains(k));
                    self.lamps = lamps;
                }
                CoreToUi::ScanResults(results) => self.scan_results = results,
                CoreToUi::Status(s) => self.status = s,
            }
        }

        egui::Frame::central_panel(&egui::Style::default()).show(ui, |ui| {
            ui.heading("Quarzlampe console");
            ui.label(&self.status);
            ui.separator();

            if ui.button("Scan for lamps").clicked() {
                let _ = self.to_core.send(UiToCore::Scan);
            }

            ui.separator();
            ui.horizontal(|ui| {
                ui.label("Connected lamps:");
                if ui.small_button("Select all").clicked() {
                    self.selected = self.lamps.iter().map(|l| l.lamp_key.clone()).collect();
                }
                if ui.small_button("Select none").clicked() {
                    self.selected.clear();
                }
            });
            for lamp in &self.lamps {
                let mut checked = self.selected.contains(&lamp.lamp_key);
                ui.horizontal(|ui| {
                    if ui.checkbox(&mut checked, "").changed() {
                        if checked {
                            self.selected.insert(lamp.lamp_key.clone());
                        } else {
                            self.selected.remove(&lamp.lamp_key);
                        }
                    }
                    ui.label(format!(
                        "{} [{}] on={:?} bri={:?} pattern={:?}",
                        lamp.name, lamp.connection, lamp.lamp_on, lamp.brightness_pct, lamp.pattern
                    ));
                    if let Some(err) = &lamp.last_error {
                        ui.colored_label(egui::Color32::from_rgb(220, 120, 60), err);
                    }
                });
            }

            ui.separator();
            ui.label(format!("Actions for {} selected lamp(s):", self.selected.len()));
            ui.add_enabled_ui(!self.selected.is_empty(), |ui| {
                ui.horizontal(|ui| {
                    if ui.button("On").clicked() {
                        self.dispatch_to_selected(&Action::On);
                    }
                    if ui.button("Off").clicked() {
                        self.dispatch_to_selected(&Action::Off);
                    }
                });
                ui.horizontal(|ui| {
                    ui.add(egui::Slider::new(&mut self.brightness_pct, 0..=100).text("brightness %"));
                    if ui.button("Apply").clicked() {
                        self.dispatch_to_selected(&Action::SetBrightness { pct: self.brightness_pct });
                    }
                });
                ui.horizontal(|ui| {
                    ui.add(egui::DragValue::new(&mut self.mode_pattern).range(1..=64).prefix("mode "));
                    if ui.button("Apply").clicked() {
                        self.dispatch_to_selected(&Action::SetMode { pattern: self.mode_pattern });
                    }
                });
                ui.horizontal(|ui| {
                    ui.add(egui::DragValue::new(&mut self.profile_slot).range(1..=6).prefix("profile slot "));
                    if ui.button("Load").clicked() {
                        self.dispatch_to_selected(&Action::LoadProfile { slot: self.profile_slot });
                    }
                });
            });

            ui.separator();
            ui.label("Scan results (tap Connect - only works within ~60s of the lamp booting unless it already has a cached token):");
            for found in &self.scan_results {
                ui.horizontal(|ui| {
                    ui.label(&found.name);
                    if ui.button("Connect").clicked() {
                        let _ = self.to_core.send(UiToCore::Connect {
                            lamp_key: found.lamp_key.clone(),
                            name: found.name.clone(),
                        });
                    }
                });
            }
        });

        // Keep redrawing even with no input, so updates arriving on
        // from_core get picked up promptly (same lesson as the M0 spike).
        ui.ctx().request_repaint_after(Duration::from_millis(300));
    }
}

fn main() -> eframe::Result<()> {
    let (to_core, from_ui) = tokio_mpsc::unbounded_channel::<UiToCore>();
    let (to_ui, from_core) = std_mpsc::channel::<CoreToUi>();

    std::thread::spawn(move || {
        let rt = tokio::runtime::Runtime::new().expect("failed to start tokio runtime");
        rt.block_on(run_core(from_ui, to_ui));
    });

    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([520.0, 480.0])
            .with_title("Quarzlampe"),
        ..Default::default()
    };
    eframe::run_native(
        "Quarzlampe",
        native_options,
        Box::new(move |_cc| {
            Ok(Box::new(ConsoleApp {
                to_core,
                from_core,
                lamps: Vec::new(),
                scan_results: Vec::new(),
                status: "connecting to daemon...".to_string(),
                selected: HashSet::new(),
                brightness_pct: 100,
                mode_pattern: 1,
                profile_slot: 1,
            }))
        }),
    )
}
