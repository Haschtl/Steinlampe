//! The on-demand console UI: a perfectly ordinary eframe app, spawned by the
//! tray daemon (`quarzlampe-tray`) when the user wants it and left to exit
//! normally when its window is closed - no hide-to-tray trick, no invisible
//! anchor viewport. The daemon is what stays running, owning BLE/rules/mic
//! detection; this process is a thin IPC client (`core::ipc`) that only
//! shows lamp state and forwards manual actions.
//!
//! Layout: a left-side nav (Connections/Rules) over one scrolling content
//! area, matching the two-section design sketched and approved as a Claude
//! Artifact (see PLAN.md's "Console UI direction" note) - a card per lamp
//! with its own inline quick-actions, instead of M3's old single-select-
//! then-apply action bar.

use std::collections::HashSet;
use std::sync::mpsc as std_mpsc;
use std::time::Duration;

use eframe::egui;
use quarzlampe_core::ipc::{IpcClient, IpcResponse, LampSummary, RequestKind, ScannedLamp};
use quarzlampe_core::rules::{Action, Rule, Trigger};
use tokio::sync::mpsc as tokio_mpsc;
use uuid::Uuid;

const ACCENT: egui::Color32 = egui::Color32::from_rgb(0xF2, 0xA3, 0x3C);

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
enum Section {
    Connections,
    Rules,
}

#[derive(Clone, Copy, PartialEq)]
enum TriggerKind {
    MicActive,
    OnBoot,
    OnBeforeSleep,
    BeforeShutdown,
    OnNotification,
    OnAppOpen,
    OnKeyPress,
}

impl TriggerKind {
    const ALL: [TriggerKind; 7] = [
        Self::MicActive,
        Self::OnBoot,
        Self::OnBeforeSleep,
        Self::BeforeShutdown,
        Self::OnNotification,
        Self::OnAppOpen,
        Self::OnKeyPress,
    ];

    fn label(&self) -> &'static str {
        match self {
            Self::MicActive => "Mic becomes active",
            Self::OnBoot => "Daemon starts (on boot)",
            Self::OnBeforeSleep => "System is about to sleep (Linux only)",
            Self::BeforeShutdown => "System is about to shut down (Linux only)",
            Self::OnNotification => "A notification appears (Linux, best-effort)",
            Self::OnAppOpen => "An app starts",
            Self::OnKeyPress => "A hotkey is pressed (not on Wayland)",
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
    NotifyPattern,
    StartWake,
    SendMorse,
}

impl ActionKind {
    const ALL: [ActionKind; 8] = [
        Self::On,
        Self::Off,
        Self::Brightness,
        Self::Mode,
        Self::Profile,
        Self::NotifyPattern,
        Self::StartWake,
        Self::SendMorse,
    ];

    fn label(&self) -> &'static str {
        match self {
            Self::On => "On",
            Self::Off => "Off",
            Self::Brightness => "Set brightness",
            Self::Mode => "Set mode",
            Self::Profile => "Load profile",
            Self::NotifyPattern => "Send notify pattern",
            Self::StartWake => "Start wake-up",
            Self::SendMorse => "Send morse code",
        }
    }
}

/// `global-hotkey`'s combo-string format is `<lowercase modifiers>+<Code>`,
/// e.g. `"control+alt+KeyL"` - this is every `Code` worth offering in a
/// dropdown (letters, digits, function keys); media/punctuation keys aren't
/// worth the extra list length for a lamp shortcut.
fn key_code_choices() -> Vec<String> {
    let mut v: Vec<String> = ('A'..='Z').map(|c| format!("Key{c}")).collect();
    v.extend((0..=9).map(|n| format!("Digit{n}")));
    v.extend((1..=12).map(|n| format!("F{n}")));
    v
}

/// Mirrors the firmware's own validation for `notify`'s positional
/// duration tokens (`> 0`, otherwise skipped) - see `src/command.cpp`.
/// `None` (not an empty `Some(vec![])`) when nothing valid was typed, so
/// `action_to_command` falls back to the firmware's own default pattern
/// instead of sending a bare `notify` with an explicit-but-empty list.
fn parse_ms_list(s: &str) -> Option<Vec<u32>> {
    let values: Vec<u32> = s.split_whitespace().filter_map(|t| t.parse::<u32>().ok()).filter(|&n| n > 0).collect();
    if values.is_empty() { None } else { Some(values) }
}

struct ConsoleApp {
    to_core: tokio_mpsc::UnboundedSender<UiToCore>,
    from_core: std_mpsc::Receiver<CoreToUi>,
    lamps: Vec<LampSummary>,
    scan_results: Vec<ScannedLamp>,
    rules: Vec<Rule>,
    status: String,
    section: Section,
    // New-rule form state.
    show_new_rule: bool,
    new_rule_name: String,
    new_rule_targets: HashSet<String>,
    new_rule_trigger_kind: TriggerKind,
    new_rule_app_name: String,
    new_rule_key_ctrl: bool,
    new_rule_key_alt: bool,
    new_rule_key_shift: bool,
    new_rule_key_super: bool,
    new_rule_key_code: String,
    new_rule_action_kind: ActionKind,
    new_rule_brightness: u8,
    new_rule_mode: u32,
    new_rule_profile: u8,
    new_rule_notify_pattern: String,
    new_rule_notify_fade: u32,
    new_rule_notify_repeat: u8,
    new_rule_wake_seconds: u32,
    new_rule_wake_soft: bool,
    new_rule_wake_mode_enabled: bool,
    new_rule_wake_mode: u32,
    new_rule_wake_bri_enabled: bool,
    new_rule_wake_bri: u8,
    new_rule_morse_text: String,
}

impl ConsoleApp {
    fn new_rule_trigger(&self) -> Trigger {
        match self.new_rule_trigger_kind {
            TriggerKind::MicActive => Trigger::MicActive,
            TriggerKind::OnBoot => Trigger::OnBoot,
            TriggerKind::OnBeforeSleep => Trigger::OnBeforeSleep,
            TriggerKind::BeforeShutdown => Trigger::BeforeShutdown,
            TriggerKind::OnNotification => Trigger::OnNotification,
            TriggerKind::OnAppOpen => Trigger::OnAppOpen { app_name: self.new_rule_app_name.trim().to_string() },
            TriggerKind::OnKeyPress => Trigger::OnKeyPress { key: self.new_rule_hotkey_string() },
        }
    }

    fn new_rule_hotkey_string(&self) -> String {
        let mut parts = Vec::new();
        if self.new_rule_key_ctrl {
            parts.push("control");
        }
        if self.new_rule_key_alt {
            parts.push("alt");
        }
        if self.new_rule_key_shift {
            parts.push("shift");
        }
        if self.new_rule_key_super {
            parts.push("super");
        }
        parts.push(self.new_rule_key_code.as_str());
        parts.join("+")
    }

    fn new_rule_action(&self) -> Action {
        match self.new_rule_action_kind {
            ActionKind::On => Action::On,
            ActionKind::Off => Action::Off,
            ActionKind::Brightness => Action::SetBrightness { pct: self.new_rule_brightness },
            ActionKind::Mode => Action::SetMode { pattern: self.new_rule_mode },
            ActionKind::Profile => Action::LoadProfile { slot: self.new_rule_profile },
            ActionKind::NotifyPattern => {
                let base = parse_ms_list(&self.new_rule_notify_pattern);
                let repeat = self.new_rule_notify_repeat.max(1) as usize;
                let pattern_ms = base.map(|v| {
                    let len = v.len().max(1);
                    v.into_iter().cycle().take(len * repeat).collect()
                });
                Action::Notify {
                    pattern_ms,
                    fade_ms: if self.new_rule_notify_fade > 0 { Some(self.new_rule_notify_fade) } else { None },
                }
            }
            ActionKind::StartWake => Action::StartWake {
                seconds: self.new_rule_wake_seconds.max(1),
                soft: self.new_rule_wake_soft,
                mode: if self.new_rule_wake_mode_enabled { Some(self.new_rule_wake_mode) } else { None },
                brightness_pct: if self.new_rule_wake_bri_enabled { Some(self.new_rule_wake_bri) } else { None },
            },
            ActionKind::SendMorse => Action::SendMorse { text: self.new_rule_morse_text.trim().to_string() },
        }
    }

    fn new_rule_valid(&self) -> bool {
        let base_ok = !self.new_rule_name.trim().is_empty() && !self.new_rule_targets.is_empty();
        let trigger_ok = match self.new_rule_trigger_kind {
            TriggerKind::OnAppOpen => !self.new_rule_app_name.trim().is_empty(),
            _ => true,
        };
        let action_ok = match self.new_rule_action_kind {
            ActionKind::SendMorse => !self.new_rule_morse_text.trim().is_empty(),
            _ => true,
        };
        base_ok && trigger_ok && action_ok
    }

    fn lamp_display_name(&self, lamp_key: &str) -> String {
        self.lamps
            .iter()
            .find(|l| l.lamp_key == lamp_key)
            .map(|l| l.name.clone())
            .unwrap_or_else(|| lamp_key.to_string())
    }

    fn connections_ui(&self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.heading("Lamps");
            let connected = self.lamps.iter().filter(|l| l.connection == "connected").count();
            ui.label(format!("{connected} connected"));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("Scan for lamps").clicked() {
                    let _ = self.to_core.send(UiToCore::Scan);
                }
            });
        });
        ui.add_space(10.0);

        ui.horizontal_wrapped(|ui| {
            for lamp in &self.lamps {
                lamp_card(ui, lamp, &self.to_core);
            }
        });

        if !self.scan_results.is_empty() {
            ui.add_space(18.0);
            ui.label(egui::RichText::new("DISCOVERED NEARBY").small().color(ui.visuals().weak_text_color()));
            ui.label(
                egui::RichText::new("Pair within ~60s of boot, or it already has a saved token")
                    .small()
                    .color(ui.visuals().weak_text_color()),
            );
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
        }
    }

    fn rules_ui(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.heading("Rules");
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if !self.show_new_rule && ui.button("+ New rule").clicked() {
                    self.show_new_rule = true;
                    self.new_rule_name.clear();
                    self.new_rule_targets.clear();
                }
            });
        });
        ui.add_space(10.0);

        if self.show_new_rule {
            egui::Frame::NONE
                .fill(ui.visuals().faint_bg_color)
                .corner_radius(10.0)
                .inner_margin(16.0)
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label("Name:");
                        ui.text_edit_singleline(&mut self.new_rule_name);
                    });

                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.label("When:");
                        egui::ComboBox::from_id_salt("new_rule_trigger_kind")
                            .selected_text(self.new_rule_trigger_kind.label())
                            .show_ui(ui, |ui| {
                                for kind in TriggerKind::ALL {
                                    ui.selectable_value(&mut self.new_rule_trigger_kind, kind, kind.label());
                                }
                            });
                    });
                    match self.new_rule_trigger_kind {
                        TriggerKind::OnAppOpen => {
                            ui.horizontal(|ui| {
                                ui.label("App name (as the OS reports it, e.g. \"firefox\"):");
                                ui.text_edit_singleline(&mut self.new_rule_app_name);
                            });
                        }
                        TriggerKind::OnKeyPress => {
                            ui.horizontal(|ui| {
                                ui.checkbox(&mut self.new_rule_key_ctrl, "Ctrl");
                                ui.checkbox(&mut self.new_rule_key_alt, "Alt");
                                ui.checkbox(&mut self.new_rule_key_shift, "Shift");
                                ui.checkbox(&mut self.new_rule_key_super, "Super/Cmd");
                                egui::ComboBox::from_id_salt("new_rule_key_code")
                                    .selected_text(&self.new_rule_key_code)
                                    .show_ui(ui, |ui| {
                                        for code in key_code_choices() {
                                            let selected = code == self.new_rule_key_code;
                                            if ui.selectable_label(selected, &code).clicked() {
                                                self.new_rule_key_code = code;
                                            }
                                        }
                                    });
                            });
                        }
                        _ => {}
                    }

                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.label("Then:");
                        egui::ComboBox::from_id_salt("new_rule_action_kind")
                            .selected_text(self.new_rule_action_kind.label())
                            .show_ui(ui, |ui| {
                                for kind in ActionKind::ALL {
                                    ui.selectable_value(&mut self.new_rule_action_kind, kind, kind.label());
                                }
                            });
                    });
                    match self.new_rule_action_kind {
                        ActionKind::Brightness => {
                            ui.add(egui::Slider::new(&mut self.new_rule_brightness, 0..=100).text("brightness %"));
                        }
                        ActionKind::Mode => {
                            ui.add(egui::DragValue::new(&mut self.new_rule_mode).range(1..=64).prefix("mode "));
                        }
                        ActionKind::Profile => {
                            ui.add(egui::DragValue::new(&mut self.new_rule_profile).range(1..=6).prefix("profile slot "));
                        }
                        ActionKind::NotifyPattern => {
                            ui.horizontal(|ui| {
                                ui.label("Pattern (ms, space-separated on/off):");
                                ui.text_edit_singleline(&mut self.new_rule_notify_pattern)
                                    .on_hover_text("Leave empty for the firmware's default (120 60 120 200)");
                            });
                            ui.horizontal(|ui| {
                                ui.add(egui::DragValue::new(&mut self.new_rule_notify_fade).range(0..=2000).suffix(" ms fade"));
                                ui.add(egui::DragValue::new(&mut self.new_rule_notify_repeat).range(1..=10).prefix("repeat "));
                            });
                        }
                        ActionKind::StartWake => {
                            ui.horizontal(|ui| {
                                ui.add(egui::DragValue::new(&mut self.new_rule_wake_seconds).range(1..=3600).suffix(" s"));
                                ui.checkbox(&mut self.new_rule_wake_soft, "soft");
                            });
                            ui.horizontal(|ui| {
                                ui.checkbox(&mut self.new_rule_wake_mode_enabled, "mode");
                                ui.add_enabled(
                                    self.new_rule_wake_mode_enabled,
                                    egui::DragValue::new(&mut self.new_rule_wake_mode).range(1..=64),
                                );
                                ui.checkbox(&mut self.new_rule_wake_bri_enabled, "brightness %");
                                ui.add_enabled(
                                    self.new_rule_wake_bri_enabled,
                                    egui::DragValue::new(&mut self.new_rule_wake_bri).range(0..=100),
                                );
                            });
                        }
                        ActionKind::SendMorse => {
                            ui.horizontal(|ui| {
                                ui.label("Text:");
                                ui.text_edit_singleline(&mut self.new_rule_morse_text);
                            });
                        }
                        ActionKind::On | ActionKind::Off => {}
                    }

                    ui.add_space(4.0);
                    ui.label("Targets:");
                    ui.horizontal_wrapped(|ui| {
                        for lamp in &self.lamps {
                            let mut checked = self.new_rule_targets.contains(&lamp.lamp_key);
                            if ui.selectable_label(checked, &lamp.name).clicked() {
                                checked = !checked;
                                if checked {
                                    self.new_rule_targets.insert(lamp.lamp_key.clone());
                                } else {
                                    self.new_rule_targets.remove(&lamp.lamp_key);
                                }
                            }
                        }
                    });
                    ui.add_space(8.0);
                    ui.horizontal(|ui| {
                        if ui.button("Cancel").clicked() {
                            self.show_new_rule = false;
                        }
                        if ui.add_enabled(self.new_rule_valid(), egui::Button::new("Save rule")).clicked() {
                            let rule = Rule {
                                id: Uuid::new_v4(),
                                name: std::mem::take(&mut self.new_rule_name),
                                enabled: true,
                                trigger: self.new_rule_trigger(),
                                targets: self.new_rule_targets.iter().cloned().collect(),
                                action: self.new_rule_action(),
                            };
                            self.new_rule_targets.clear();
                            self.show_new_rule = false;
                            let _ = self.to_core.send(UiToCore::SaveRule(rule));
                        }
                    });
                });
            ui.add_space(14.0);
        }

        for rule in &self.rules {
            egui::Frame::NONE
                .fill(ui.visuals().extreme_bg_color)
                .corner_radius(10.0)
                .inner_margin(12.0)
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        let mut enabled = rule.enabled;
                        if ui.checkbox(&mut enabled, "").changed() {
                            let mut updated = rule.clone();
                            updated.enabled = enabled;
                            let _ = self.to_core.send(UiToCore::SaveRule(updated));
                        }
                        ui.strong(&rule.name);
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if ui.small_button("Delete").clicked() {
                                let _ = self.to_core.send(UiToCore::DeleteRule(rule.id));
                            }
                            if ui.small_button("Fire now").clicked() {
                                let _ = self.to_core.send(UiToCore::FireRule(rule.id));
                            }
                        });
                    });
                    let targets: Vec<String> = rule.targets.iter().map(|k| self.lamp_display_name(k)).collect();
                    ui.label(
                        egui::RichText::new(format!(
                            "{} \u{2192} {} \u{b7} {}",
                            describe_trigger(&rule.trigger),
                            describe_action(&rule.action),
                            targets.join(", ")
                        ))
                        .small()
                        .color(ui.visuals().weak_text_color()),
                    );
                });
            ui.add_space(8.0);
        }
    }
}

fn describe_trigger(trigger: &Trigger) -> String {
    match trigger {
        Trigger::MicActive => "mic becomes active".to_string(),
        Trigger::OnBoot => "daemon starts".to_string(),
        Trigger::OnBeforeSleep => "system is about to sleep".to_string(),
        Trigger::BeforeShutdown => "system is about to shut down".to_string(),
        Trigger::OnNotification => "a notification appears".to_string(),
        Trigger::OnAppOpen { app_name } => format!("\"{app_name}\" starts"),
        Trigger::OnKeyPress { key } => format!("hotkey {key} is pressed"),
    }
}

fn describe_action(action: &Action) -> String {
    match action {
        Action::On => "turn on".to_string(),
        Action::Off => "turn off".to_string(),
        Action::SetBrightness { pct } => format!("set brightness to {pct}%"),
        Action::SetMode { pattern } => format!("set mode to {pattern}"),
        Action::LoadProfile { slot } => format!("load profile {slot}"),
        Action::Notify { .. } => "send notify pattern".to_string(),
        Action::StartWake { seconds, soft, mode, brightness_pct } => {
            let mut parts = vec![format!("start a {seconds}s wake-up")];
            if *soft {
                parts.push("(soft)".to_string());
            }
            if let Some(m) = mode {
                parts.push(format!("mode {m}"));
            }
            if let Some(b) = brightness_pct {
                parts.push(format!("to {b}%"));
            }
            parts.join(" ")
        }
        Action::SendMorse { text } => format!("send morse \"{text}\""),
    }
}

fn status_visual(connection: &str) -> (egui::Color32, &'static str) {
    match connection {
        "connected" => (egui::Color32::from_rgb(0x4A, 0xDE, 0x80), "Connected"),
        "connecting" => (ACCENT, "Connecting\u{2026}"),
        "reconnecting" => (ACCENT, "Reconnecting\u{2026}"),
        "awaiting_trust_token" => (ACCENT, "Awaiting trust token\u{2026}"),
        _ => (egui::Color32::from_rgb(0x8B, 0x92, 0xA0), "Disconnected"),
    }
}

/// A toggle switch in the style of the approved console sketch - egui has
/// no built-in one. Standard egui-book recipe: allocate a pill-shaped rect,
/// animate a knob across it, flip `*on` on click.
fn toggle_ui(ui: &mut egui::Ui, on: &mut bool) -> egui::Response {
    let desired_size = ui.spacing().interact_size.y * egui::vec2(1.8, 1.0);
    let (rect, mut response) = ui.allocate_exact_size(desired_size, egui::Sense::click());
    if response.clicked() {
        *on = !*on;
        response.mark_changed();
    }
    if ui.is_rect_visible(rect) {
        let how_on = ui.ctx().animate_bool_responsive(response.id, *on);
        let visuals = ui.style().interact_selectable(&response, *on);
        let rect = rect.expand(visuals.expansion);
        let radius = 0.5 * rect.height();
        ui.painter().rect(rect, radius, visuals.bg_fill, visuals.bg_stroke, egui::StrokeKind::Inside);
        let circle_x = egui::lerp((rect.left() + radius)..=(rect.right() - radius), how_on);
        let center = egui::pos2(circle_x, rect.center().y);
        ui.painter().circle(center, 0.75 * radius, visuals.fg_stroke.color, visuals.fg_stroke);
    }
    response
}

/// One lamp's card: name + status, inline power/brightness/mode/profile
/// quick-actions, all disabled unless the lamp is actually connected.
/// A free function (not a `ConsoleApp` method) since it only needs the
/// lamp's own data and a way to send commands, not the rest of the app
/// state - keeps the borrow simple when called from `connections_ui(&self)`.
fn lamp_card(ui: &mut egui::Ui, lamp: &LampSummary, to_core: &tokio_mpsc::UnboundedSender<UiToCore>) {
    let connected = lamp.connection == "connected";
    let send = |line: String| {
        let _ = to_core.send(UiToCore::SendCommand { lamp_key: lamp.lamp_key.clone(), line });
    };

    ui.scope(|ui| {
        ui.set_width(248.0);
        egui::Frame::NONE.fill(ui.visuals().extreme_bg_color).corner_radius(12.0).inner_margin(16.0).show(ui, |ui| {
            ui.set_width(216.0);
            ui.spacing_mut().item_spacing.y = 8.0;
            ui.vertical(|ui| {
                ui.horizontal(|ui| {
                    ui.strong(&lamp.name);
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        let (color, label) = status_visual(&lamp.connection);
                        ui.label(label);
                        ui.colored_label(color, "\u{25cf}");
                    });
                });

                if let Some(err) = &lamp.last_error {
                    ui.colored_label(egui::Color32::from_rgb(0xF2, 0x55, 0x5A), err);
                }

                ui.separator();

                ui.horizontal(|ui| {
                    ui.label("Power");
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.add_enabled_ui(connected, |ui| {
                            let mut on = lamp.lamp_on.unwrap_or(false);
                            if toggle_ui(ui, &mut on).changed() {
                                send(if on { "on".to_string() } else { "off".to_string() });
                            }
                        });
                    });
                });

                ui.horizontal(|ui| {
                    ui.label("Brightness");
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.label(format!("{:.0}%", lamp.brightness_pct.unwrap_or(0.0)));
                    });
                });
                ui.add_enabled_ui(connected, |ui| {
                    let mut value = lamp.brightness_pct.unwrap_or(0.0);
                    let resp = ui.add(egui::Slider::new(&mut value, 0.0..=100.0).show_value(false));
                    // Only send on release/blur, not every frame of the drag -
                    // one BLE write per adjustment, not a flood of them.
                    if resp.drag_stopped() || resp.lost_focus() {
                        send(format!("bri {}", value as u8));
                    }
                });

                ui.horizontal(|ui| {
                    ui.label("Mode");
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.add_enabled_ui(connected, |ui| {
                            let pattern = lamp.pattern.unwrap_or(1);
                            if ui.small_button("+").clicked() {
                                send(format!("mode {}", (pattern + 1).min(64)));
                            }
                            ui.label(pattern.to_string());
                            if ui.small_button("\u{2212}").clicked() {
                                send(format!("mode {}", pattern.saturating_sub(1).max(1)));
                            }
                        });
                    });
                });

                ui.label("Profile");
                ui.horizontal(|ui| {
                    ui.add_enabled_ui(connected, |ui| {
                        for slot in 1u8..=6 {
                            if ui.small_button(slot.to_string()).clicked() {
                                send(format!("profile load {slot}"));
                            }
                        }
                    });
                });
            });
        });
    });
}

impl eframe::App for ConsoleApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        while let Ok(msg) = self.from_core.try_recv() {
            match msg {
                CoreToUi::Lamps(lamps) => self.lamps = lamps,
                CoreToUi::ScanResults(results) => self.scan_results = results,
                CoreToUi::Rules(rules) => self.rules = rules,
                CoreToUi::Status(s) => self.status = s,
            }
        }

        let nav_frame = egui::Frame::NONE.fill(ui.visuals().extreme_bg_color).inner_margin(14.0);
        egui::Panel::left("nav").default_size(180.0).resizable(false).frame(nav_frame).show(ui, |ui| {
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                let (rect, _) = ui.allocate_exact_size(egui::vec2(18.0, 18.0), egui::Sense::hover());
                ui.painter().rect_filled(rect, 6.0, ACCENT);
                ui.add_space(2.0);
                ui.strong("Quarzlampe");
            });
            ui.add_space(18.0);
            if ui.selectable_label(self.section == Section::Connections, "  Connections").clicked() {
                self.section = Section::Connections;
            }
            ui.add_space(2.0);
            if ui.selectable_label(self.section == Section::Rules, "  Rules").clicked() {
                self.section = Section::Rules;
            }
            ui.with_layout(egui::Layout::bottom_up(egui::Align::LEFT), |ui| {
                ui.add_space(10.0);
                ui.label(egui::RichText::new(&self.status).small().color(ui.visuals().weak_text_color()));
            });
        });

        egui::CentralPanel::default().show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| match self.section {
                Section::Connections => self.connections_ui(ui),
                Section::Rules => self.rules_ui(ui),
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

/// Dark theme with the brand's warm accent color, plus a bit more breathing
/// room than egui's defaults (spacing/padding), matching the approved
/// console sketch - see PLAN.md's "Console UI direction" note. Not a claim
/// that this looks like a native OS widget set; see that note for why.
fn apply_visuals(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.selection.bg_fill = ACCENT;
    visuals.selection.stroke.color = egui::Color32::from_rgb(0x23, 0x14, 0x05);
    visuals.hyperlink_color = ACCENT;
    ctx.set_visuals(visuals);

    ctx.all_styles_mut(|style| {
        style.spacing.item_spacing = egui::vec2(10.0, 10.0);
        style.spacing.button_padding = egui::vec2(10.0, 6.0);
        style.spacing.interact_size.y = 24.0;
    });
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
            .with_inner_size([860.0, 620.0])
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
        Box::new(move |cc| {
            apply_visuals(&cc.egui_ctx);
            Ok(Box::new(ConsoleApp {
                to_core,
                from_core,
                lamps: Vec::new(),
                scan_results: Vec::new(),
                rules: Vec::new(),
                status: "connecting to daemon...".to_string(),
                section: Section::Connections,
                show_new_rule: false,
                new_rule_name: String::new(),
                new_rule_targets: HashSet::new(),
                new_rule_trigger_kind: TriggerKind::MicActive,
                new_rule_app_name: String::new(),
                new_rule_key_ctrl: true,
                new_rule_key_alt: true,
                new_rule_key_shift: false,
                new_rule_key_super: false,
                new_rule_key_code: "KeyL".to_string(),
                new_rule_action_kind: ActionKind::On,
                new_rule_brightness: 50,
                new_rule_mode: 1,
                new_rule_profile: 1,
                new_rule_notify_pattern: String::new(),
                new_rule_notify_fade: 0,
                new_rule_notify_repeat: 1,
                new_rule_wake_seconds: 180,
                new_rule_wake_soft: false,
                new_rule_wake_mode_enabled: false,
                new_rule_wake_mode: 1,
                new_rule_wake_bri_enabled: false,
                new_rule_wake_bri: 80,
                new_rule_morse_text: String::new(),
            }))
        }),
    )
}
