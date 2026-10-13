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
use quarzlampe_core::rules::{action_to_command, Action, Rule, Trigger};
use tokio::sync::mpsc as tokio_mpsc;
use uuid::Uuid;

enum UiToCore {
    Scan,
    Connect { lamp_key: String, name: String },
    SendCommand { lamp_key: String, line: String },
    SaveRule(Rule),
    DeleteRule(Uuid),
    FireRule(Uuid),
}

enum CoreToUi {
    Lamps(Vec<LampSummary>),
    ScanResults(Vec<ScannedLamp>),
    Rules(Vec<Rule>),
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
                // Rules change rarely - polling it at the same cadence as
                // lamp state is wasteful, but simple, and this is a handful
                // of small requests a second at most. Revisit if it matters.
                match client.request(RequestKind::ListRules).await {
                    Ok(IpcResponse::Rules { rules }) => { let _ = to_ui.send(CoreToUi::Rules(rules)); }
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
                    UiToCore::SaveRule(rule) => RequestKind::SaveRule { rule },
                    UiToCore::DeleteRule(id) => RequestKind::DeleteRule { id },
                    UiToCore::FireRule(id) => RequestKind::FireRule { id },
                };
                match client.request(kind).await {
                    Ok(IpcResponse::ScanResults { lamps }) => { let _ = to_ui.send(CoreToUi::ScanResults(lamps)); }
                    Ok(IpcResponse::Rules { rules }) => { let _ = to_ui.send(CoreToUi::Rules(rules)); }
                    Ok(IpcResponse::Error { message }) => { let _ = to_ui.send(CoreToUi::Status(message)); }
                    Ok(_) => {}
                    Err(e) => { let _ = to_ui.send(CoreToUi::Status(e.to_string())); }
                }
            }
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum ActionKind {
    On,
    Off,
    Brightness,
    Mode,
    Profile,
}

impl ActionKind {
    const ALL: [ActionKind; 5] = [Self::On, Self::Off, Self::Brightness, Self::Mode, Self::Profile];

    fn label(&self) -> &'static str {
        match self {
            Self::On => "On",
            Self::Off => "Off",
            Self::Brightness => "Set brightness",
            Self::Mode => "Set mode",
            Self::Profile => "Load profile",
        }
    }
}

struct ConsoleApp {
    to_core: tokio_mpsc::UnboundedSender<UiToCore>,
    from_core: std_mpsc::Receiver<CoreToUi>,
    lamps: Vec<LampSummary>,
    scan_results: Vec<ScannedLamp>,
    rules: Vec<Rule>,
    status: String,
    selected: HashSet<String>,
    brightness_pct: u8,
    mode_pattern: u32,
    profile_slot: u8,
    // New-rule form state.
    new_rule_name: String,
    new_rule_action_kind: ActionKind,
    new_rule_targets: HashSet<String>,
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

    /// Builds the `Action` the new-rule form currently describes, reusing
    /// the same brightness/mode/profile number fields as the manual quick-
    /// action bar above (so there's only one set of numeric inputs, not a
    /// separate copy per section - editing them affects both at once,
    /// which is a minor shared-state tradeoff acceptable at this stage).
    fn new_rule_action(&self) -> Action {
        match self.new_rule_action_kind {
            ActionKind::On => Action::On,
            ActionKind::Off => Action::Off,
            ActionKind::Brightness => Action::SetBrightness { pct: self.brightness_pct },
            ActionKind::Mode => Action::SetMode { pattern: self.mode_pattern },
            ActionKind::Profile => Action::LoadProfile { slot: self.profile_slot },
        }
    }

    fn lamp_display_name(&self, lamp_key: &str) -> String {
        self.lamps
            .iter()
            .find(|l| l.lamp_key == lamp_key)
            .map(|l| l.name.clone())
            .unwrap_or_else(|| lamp_key.to_string())
    }
}

fn describe_action(action: &Action) -> String {
    match action {
        Action::On => "turn on".to_string(),
        Action::Off => "turn off".to_string(),
        Action::SetBrightness { pct } => format!("set brightness to {pct}%"),
        Action::SetMode { pattern } => format!("set mode to {pattern}"),
        Action::LoadProfile { slot } => format!("load profile {slot}"),
        Action::Notify { .. } => "notify".to_string(),
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
                CoreToUi::Rules(rules) => self.rules = rules,
                CoreToUi::Status(s) => self.status = s,
            }
        }

        egui::Frame::central_panel(&egui::Style::default()).show(ui, |ui| {
        egui::ScrollArea::vertical().show(ui, |ui| {
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

            ui.separator();
            ui.heading("Rules");
            for rule in &self.rules {
                ui.group(|ui| {
                    ui.horizontal(|ui| {
                        let mut enabled = rule.enabled;
                        if ui.checkbox(&mut enabled, "").changed() {
                            let mut updated = rule.clone();
                            updated.enabled = enabled;
                            let _ = self.to_core.send(UiToCore::SaveRule(updated));
                        }
                        ui.strong(&rule.name);
                    });
                    let targets: Vec<String> = rule.targets.iter().map(|k| self.lamp_display_name(k)).collect();
                    ui.label(format!(
                        "when mic becomes active -> {} on: {}",
                        describe_action(&rule.action),
                        targets.join(", ")
                    ));
                    ui.horizontal(|ui| {
                        if ui.button("Fire now").clicked() {
                            let _ = self.to_core.send(UiToCore::FireRule(rule.id));
                        }
                        if ui.button("Delete").clicked() {
                            let _ = self.to_core.send(UiToCore::DeleteRule(rule.id));
                        }
                    });
                });
            }

            ui.separator();
            ui.label("Add rule (trigger is always \"mic becomes active\" for now - that's the only one M5 will wire up):");
            ui.horizontal(|ui| {
                ui.label("Name:");
                ui.text_edit_singleline(&mut self.new_rule_name);
            });
            ui.horizontal(|ui| {
                ui.label("Action:");
                egui::ComboBox::from_id_salt("new_rule_action_kind")
                    .selected_text(self.new_rule_action_kind.label())
                    .show_ui(ui, |ui| {
                        for kind in ActionKind::ALL {
                            ui.selectable_value(&mut self.new_rule_action_kind, kind, kind.label());
                        }
                    });
            });
            ui.label("Targets:");
            for lamp in &self.lamps {
                let mut checked = self.new_rule_targets.contains(&lamp.lamp_key);
                if ui.checkbox(&mut checked, &lamp.name).changed() {
                    if checked {
                        self.new_rule_targets.insert(lamp.lamp_key.clone());
                    } else {
                        self.new_rule_targets.remove(&lamp.lamp_key);
                    }
                }
            }
            let can_save = !self.new_rule_name.is_empty() && !self.new_rule_targets.is_empty();
            if ui.add_enabled(can_save, egui::Button::new("Save rule")).clicked() {
                let rule = Rule {
                    id: Uuid::new_v4(),
                    name: std::mem::take(&mut self.new_rule_name),
                    enabled: true,
                    trigger: Trigger::MicActive,
                    targets: self.new_rule_targets.iter().cloned().collect(),
                    action: self.new_rule_action(),
                };
                self.new_rule_targets.clear();
                let _ = self.to_core.send(UiToCore::SaveRule(rule));
            }
        });
        });

        // Keep redrawing even with no input, so updates arriving on
        // from_core get picked up promptly (same lesson as the M0 spike).
        ui.ctx().request_repaint_after(Duration::from_millis(300));
    }
}

/// The same lamp icon the web frontend uses as its favicon
/// (`frontend/public/icon-lamp.svg`, rasterized to PNG ahead of time since
/// nothing in this dependency tree can rasterize SVG) - so the console
/// window gets a real taskbar/dock icon instead of the toolkit default.
fn load_window_icon() -> egui::IconData {
    let bytes = include_bytes!("../assets/icon.png");
    let image = image::load_from_memory(bytes).expect("bundled icon.png is valid").into_rgba8();
    let (width, height) = image.dimensions();
    egui::IconData { rgba: image.into_raw(), width, height }
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
            .with_title("Quarzlampe")
            .with_icon(load_window_icon())
            // On Wayland (GNOME in particular) the taskbar/dash/alt-tab icon
            // comes from a matching `.desktop` file's `Icon=`, not from this
            // pixel buffer - `with_icon` above only covers X11 and other
            // WMs that read `_NET_WM_ICON` directly. The app id has to match
            // the desktop file's name; see `dev.quarzlampe.console.desktop`.
            .with_app_id("dev.quarzlampe.console"),
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
                rules: Vec::new(),
                status: "connecting to daemon...".to_string(),
                selected: HashSet::new(),
                brightness_pct: 100,
                mode_pattern: 1,
                profile_slot: 1,
                new_rule_name: String::new(),
                new_rule_action_kind: ActionKind::On,
                new_rule_targets: HashSet::new(),
            }))
        }),
    )
}
