//! The on-demand console UI: a perfectly ordinary eframe app, spawned by the
//! tray daemon (`quarzlampe-tray`) when the user wants it and left to exit
//! normally when its window is closed - no hide-to-tray trick, no invisible
//! anchor viewport. The daemon is what stays running, owning BLE/rules/mic
//! detection; this process is a thin IPC client (`core::ipc`) that only
//! shows lamp state and forwards manual actions.

use std::sync::mpsc as std_mpsc;
use std::time::Duration;

use eframe::egui;
use quarzlampe_core::ipc::{IpcClient, IpcResponse, LampSummary, RequestKind, ScannedLamp};
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
}

impl eframe::App for ConsoleApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        while let Ok(msg) = self.from_core.try_recv() {
            match msg {
                CoreToUi::Lamps(lamps) => self.lamps = lamps,
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
            ui.label("Connected lamps:");
            for lamp in &self.lamps {
                ui.horizontal(|ui| {
                    ui.label(format!(
                        "{} [{}] on={:?} bri={:?}",
                        lamp.name, lamp.connection, lamp.lamp_on, lamp.brightness_pct
                    ));
                    if ui.button("On").clicked() {
                        let _ = self.to_core.send(UiToCore::SendCommand {
                            lamp_key: lamp.lamp_key.clone(),
                            line: "on".to_string(),
                        });
                    }
                    if ui.button("Off").clicked() {
                        let _ = self.to_core.send(UiToCore::SendCommand {
                            lamp_key: lamp.lamp_key.clone(),
                            line: "off".to_string(),
                        });
                    }
                });
            }

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
            .with_inner_size([480.0, 360.0])
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
            }))
        }),
    )
}
