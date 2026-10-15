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
//!
//! UI text goes through `quarzlampe_core::i18n` (English/German, picked up
//! from the OS locale once at startup - no in-app language switcher).

use std::collections::HashSet;
use std::sync::mpsc as std_mpsc;
use std::time::Duration;

use eframe::egui;
use quarzlampe_core::i18n::{t, Lang};
use quarzlampe_core::ipc::{IpcClient, IpcResponse, LampSummary, RequestKind, ScannedLamp, UpdateInfo, VisualizerConfig};
use quarzlampe_core::rules::{Action, QuickRuleKind, Rule, Trigger};
use tokio::sync::mpsc as tokio_mpsc;
use uuid::Uuid;

/// The web frontend's own theme tokens (`frontend/tailwind.config.cjs`'s
/// dark palette, `frontend/src/index.css` for the derived `--accent-color`),
/// so the console reads as the same product, not a different app that
/// happens to control the same lamps. `ACCENT` is the brand color (nav
/// selection, hyperlinks, the little app-icon tint); `WARNING` is kept
/// separate from it on purpose, since status dots shouldn't share a color
/// with "this is the interactive/selected thing" - "connecting..." stays
/// its own amber rather than inheriting the brand cyan.
const BG: egui::Color32 = egui::Color32::from_rgb(0x0b, 0x0f, 0x1a);
const PANEL: egui::Color32 = egui::Color32::from_rgb(0x11, 0x18, 0x2a);
const BORDER: egui::Color32 = egui::Color32::from_rgb(0x1f, 0x2b, 0x44);
const TEXT: egui::Color32 = egui::Color32::from_rgb(0xee, 0xf2, 0xff);
const MUTED: egui::Color32 = egui::Color32::from_rgb(0x9f, 0xb2, 0xcc);
const ACCENT: egui::Color32 = egui::Color32::from_rgb(0x5b, 0xe6, 0xff);
const WARNING: egui::Color32 = egui::Color32::from_rgb(0xF2, 0xA3, 0x3C);

/// Where the web app (all configuration - profiles, patterns, hardware
/// settings, MIDI, OTA) is actually hosted - confirmed live and serving
/// the real app (not a 404) before hardcoding it here.
const WEB_INTERFACE_URL: &str = "https://haschtl.github.io/Steinlampe/";

enum UiToCore {
    Scan,
    Connect { lamp_key: String, name: String },
    SendCommand { lamp_key: String, line: String },
    Reconnect(String),
    SaveRule(Rule),
    DeleteRule(Uuid),
    FireRule(Uuid),
    SetVisualizer(Option<VisualizerConfig>),
}

enum CoreToUi {
    Lamps(Vec<LampSummary>),
    ScanResults(Vec<ScannedLamp>),
    Rules(Vec<Rule>),
    Visualizer { config: Option<VisualizerConfig>, last_error: Option<String> },
    UpdateStatus(Option<UpdateInfo>),
    Status(String),
    WebhookToken(String),
}

/// Runs on a background tokio runtime (its own OS thread - eframe owns the
/// main thread synchronously). Connects to the daemon (retrying until it's
/// up), then polls `ListLamps` periodically and forwards UI-initiated
/// requests, relaying everything to the UI thread over a plain
/// `std::sync::mpsc` channel it can drain non-blockingly each frame.
async fn run_core(
    mut from_ui: tokio_mpsc::UnboundedReceiver<UiToCore>,
    to_ui: std_mpsc::Sender<CoreToUi>,
    lang: Lang,
    ctx: egui::Context,
) {
    // `request_repaint_after` in `ConsoleApp::ui` only re-polls `from_core` at
    // a periodic ceiling, which meant every update sent from here (lamp
    // state, rules, ...) could sit unseen until that timer next fired. Each
    // send below also pokes `ctx` directly, which wakes the UI thread's
    // event loop immediately - `egui::Context::request_repaint` is designed
    // to be called from any thread. The periodic repaint stays too, as a
    // fallback heartbeat.
    let notify = |msg: CoreToUi| {
        let _ = to_ui.send(msg);
        ctx.request_repaint();
    };

    let mut client = loop {
        match IpcClient::connect().await {
            Ok(c) => break c,
            Err(e) => {
                notify(CoreToUi::Status(format!("{}: {e}", t(lang, "console.waitingForDaemon"))));
                tokio::time::sleep(Duration::from_secs(1)).await;
            }
        }
    };
    notify(CoreToUi::Status(t(lang, "console.connectedToDaemon").to_string()));
    // The webhook auth token never changes while this daemon instance is
    // alive - fetched once here, not on every poll tick like the rest of
    // this function's state (`GetVisualizer`, `GetUpdateStatus`, ...).
    if let Ok(IpcResponse::WebhookInfo { token }) = client.request(RequestKind::GetWebhookInfo).await {
        notify(CoreToUi::WebhookToken(token));
    }

    // Lamp state is what the UI's on/off switch and brightness live-track, so
    // it gets its own fast poll - this is a local IPC round-trip against the
    // daemon's already-current `watch` state (no BLE involved, see
    // `lamp_actor::run_connected`), so polling it quickly is cheap. Rules/
    // visualizer/update-status change rarely and stay on the slower cadence
    // they always had.
    let mut lamp_poll = tokio::time::interval(Duration::from_millis(150));
    let mut aux_poll = tokio::time::interval(Duration::from_millis(1500));
    loop {
        tokio::select! {
            _ = lamp_poll.tick() => {
                match client.request(RequestKind::ListLamps).await {
                    Ok(IpcResponse::Lamps { lamps }) => notify(CoreToUi::Lamps(lamps)),
                    Ok(IpcResponse::Error { message }) => notify(CoreToUi::Status(message)),
                    Ok(_) => {}
                    Err(e) => notify(CoreToUi::Status(e.to_string())),
                }
            }
            _ = aux_poll.tick() => {
                match client.request(RequestKind::ListRules).await {
                    Ok(IpcResponse::Rules { rules }) => notify(CoreToUi::Rules(rules)),
                    Ok(IpcResponse::Error { message }) => notify(CoreToUi::Status(message)),
                    Ok(_) => {}
                    Err(e) => notify(CoreToUi::Status(e.to_string())),
                }
                match client.request(RequestKind::GetVisualizer).await {
                    Ok(IpcResponse::Visualizer { config, last_error }) => notify(CoreToUi::Visualizer { config, last_error }),
                    Ok(IpcResponse::Error { message }) => notify(CoreToUi::Status(message)),
                    Ok(_) => {}
                    Err(e) => notify(CoreToUi::Status(e.to_string())),
                }
                // Reads whatever the daemon's own background check last
                // found - never triggers a check itself (see
                // `core::update`'s doc comment for why this is notify-only).
                match client.request(RequestKind::GetUpdateStatus).await {
                    Ok(IpcResponse::UpdateStatus { available }) => notify(CoreToUi::UpdateStatus(available)),
                    Ok(IpcResponse::Error { message }) => notify(CoreToUi::Status(message)),
                    Ok(_) => {}
                    Err(e) => notify(CoreToUi::Status(e.to_string())),
                }
            }
            msg = from_ui.recv() => {
                let Some(msg) = msg else { break };
                let kind = match msg {
                    UiToCore::Scan => RequestKind::Scan { seconds: 4 },
                    UiToCore::Connect { lamp_key, name } => RequestKind::Connect { lamp_key, name },
                    UiToCore::SendCommand { lamp_key, line } => RequestKind::SendCommand { lamp_key, line },
                    UiToCore::Reconnect(lamp_key) => RequestKind::Reconnect { lamp_key },
                    UiToCore::SaveRule(rule) => RequestKind::SaveRule { rule },
                    UiToCore::DeleteRule(id) => RequestKind::DeleteRule { id },
                    UiToCore::FireRule(id) => RequestKind::FireRule { id },
                    UiToCore::SetVisualizer(config) => RequestKind::SetVisualizer { config },
                };
                match client.request(kind).await {
                    Ok(IpcResponse::ScanResults { lamps }) => notify(CoreToUi::ScanResults(lamps)),
                    Ok(IpcResponse::Rules { rules }) => notify(CoreToUi::Rules(rules)),
                    Ok(IpcResponse::Visualizer { config, last_error }) => notify(CoreToUi::Visualizer { config, last_error }),
                    Ok(IpcResponse::Error { message }) => notify(CoreToUi::Status(message)),
                    Ok(_) => {}
                    Err(e) => notify(CoreToUi::Status(e.to_string())),
                }
            }
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Section {
    Connections,
    Rules,
    Visualizer,
}

#[derive(Clone, Copy, PartialEq)]
enum TriggerKind {
    MicActive,
    MicInactive,
    CameraActive,
    CameraInactive,
    OnBoot,
    OnBeforeSleep,
    BeforeShutdown,
    OnNotification,
    OnAppOpen,
    OnKeyPress,
    AtTime,
    ScreenLocked,
    ScreenUnlocked,
    Webhook,
}

impl TriggerKind {
    const ALL: [TriggerKind; 14] = [
        Self::MicActive,
        Self::MicInactive,
        Self::CameraActive,
        Self::CameraInactive,
        Self::OnBoot,
        Self::OnBeforeSleep,
        Self::BeforeShutdown,
        Self::OnNotification,
        Self::OnAppOpen,
        Self::OnKeyPress,
        Self::AtTime,
        Self::ScreenLocked,
        Self::ScreenUnlocked,
        Self::Webhook,
    ];

    fn label(&self, lang: Lang) -> &'static str {
        let key = match self {
            Self::MicActive => "trigger.micActive",
            Self::MicInactive => "trigger.micInactive",
            Self::CameraActive => "trigger.cameraActive",
            Self::CameraInactive => "trigger.cameraInactive",
            Self::OnBoot => "trigger.onBoot",
            Self::OnBeforeSleep => "trigger.onBeforeSleep",
            Self::BeforeShutdown => "trigger.beforeShutdown",
            Self::OnNotification => "trigger.onNotification",
            Self::OnAppOpen => "trigger.onAppOpen",
            Self::OnKeyPress => "trigger.onKeyPress",
            Self::AtTime => "trigger.atTime",
            Self::ScreenLocked => "trigger.screenLocked",
            Self::ScreenUnlocked => "trigger.screenUnlocked",
            Self::Webhook => "trigger.webhook",
        };
        t(lang, key)
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

    fn label(&self, lang: Lang) -> &'static str {
        let key = match self {
            Self::On => "actionKind.on",
            Self::Off => "actionKind.off",
            Self::Brightness => "actionKind.brightness",
            Self::Mode => "actionKind.mode",
            Self::Profile => "actionKind.profile",
            Self::NotifyPattern => "actionKind.notifyPattern",
            Self::StartWake => "actionKind.startWake",
            Self::SendMorse => "actionKind.sendMorse",
        };
        t(lang, key)
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
    lang: Lang,
    icon_texture: egui::TextureHandle,
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
    /// Triggers already added to the rule being built/edited - `OR`ed
    /// together (see `Rule::triggers`'s doc comment: this app only offers
    /// "any of these", never an AND/time-window combination). The
    /// `new_rule_trigger_kind`+fields below are just the staging area for
    /// building the *next* one to add, not a live view of this list.
    new_rule_triggers: Vec<Trigger>,
    new_rule_trigger_kind: TriggerKind,
    new_rule_app_name: String,
    new_rule_notification_app_filter: String,
    new_rule_time_hour: u8,
    new_rule_time_minute: u8,
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
    /// `Some(id)` while the open form is editing an existing rule instead
    /// of building a new one - Save then updates that id (and preserves
    /// `editing_rule_enabled`, its enabled flag at the time editing
    /// started) rather than creating a fresh rule with a new id.
    editing_rule_id: Option<Uuid>,
    editing_rule_enabled: bool,
    /// Preserved from the rule being edited so Save doesn't silently
    /// detach it from a quick-rule toggle (`lamp_card`'s On-Air/Teams
    /// switches) just because the user happened to use the pencil-edit
    /// form instead of the switch itself.
    editing_rule_quick_rule: Option<QuickRuleKind>,
    // Music visualizer panel state.
    visualizer_enabled: bool,
    visualizer_targets: HashSet<String>,
    visualizer_gain: f32,
    visualizer_unsupported: Option<String>,
    update_available: Option<UpdateInfo>,
    /// The daemon's auth token, fetched once right after connecting
    /// (`GetWebhookInfo`) - combined with `quarzlampe_core::ipc::WEBHOOK_PORT`
    /// and a `Trigger::Webhook`'s own `id` to build the full URL shown for
    /// the user to copy into an external tool. `None` until that first
    /// request completes.
    webhook_token: Option<String>,
}

impl ConsoleApp {
    fn new_rule_trigger(&self) -> Trigger {
        match self.new_rule_trigger_kind {
            TriggerKind::MicActive => Trigger::MicActive,
            TriggerKind::MicInactive => Trigger::MicInactive,
            TriggerKind::CameraActive => Trigger::CameraActive,
            TriggerKind::CameraInactive => Trigger::CameraInactive,
            TriggerKind::OnBoot => Trigger::OnBoot,
            TriggerKind::OnBeforeSleep => Trigger::OnBeforeSleep,
            TriggerKind::BeforeShutdown => Trigger::BeforeShutdown,
            TriggerKind::OnNotification => {
                let filter = self.new_rule_notification_app_filter.trim();
                Trigger::OnNotification {
                    app_name_filter: if filter.is_empty() { None } else { Some(filter.to_string()) },
                }
            }
            TriggerKind::OnAppOpen => Trigger::OnAppOpen { app_name: self.new_rule_app_name.trim().to_string() },
            TriggerKind::OnKeyPress => Trigger::OnKeyPress { key: self.new_rule_hotkey_string() },
            TriggerKind::AtTime => Trigger::AtTime { hour: self.new_rule_time_hour, minute: self.new_rule_time_minute },
            TriggerKind::ScreenLocked => Trigger::ScreenLocked,
            TriggerKind::ScreenUnlocked => Trigger::ScreenUnlocked,
            // A fresh id every time this is built - only ever called once,
            // right when "+ Add trigger" is clicked (see `rules_ui`), never
            // while just re-rendering the picker, so this can't regenerate
            // an id out from under an already-added webhook trigger and
            // break a URL someone already configured elsewhere with it.
            TriggerKind::Webhook => Trigger::Webhook { id: Uuid::new_v4() },
        }
    }

    /// Whether the currently staged trigger-kind + fields have everything
    /// they need to become a `Trigger` - gates the "+ Add trigger" button,
    /// separately from `new_rule_valid()` (which gates Save and only cares
    /// whether *any* triggers have actually been added to the list).
    fn add_trigger_valid(&self) -> bool {
        match self.new_rule_trigger_kind {
            TriggerKind::OnAppOpen => !self.new_rule_app_name.trim().is_empty(),
            _ => true,
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
        let base_ok = !self.new_rule_name.trim().is_empty()
            && !self.new_rule_targets.is_empty()
            && !self.new_rule_triggers.is_empty();
        let action_ok = match self.new_rule_action_kind {
            ActionKind::SendMorse => !self.new_rule_morse_text.trim().is_empty(),
            _ => true,
        };
        base_ok && action_ok
    }

    /// Populates every form field from an existing rule and opens the
    /// form in "editing" mode - the reverse of `new_rule_trigger`/
    /// `new_rule_action`. One exception, called out rather than faked:
    /// `Action::Notify`'s `pattern_ms` is the firmware's already-expanded
    /// list (any `repeat` count was flattened into it when the rule was
    /// first saved - see `new_rule_action`), and there's no way to
    /// recover the original base pattern/repeat count from that alone
    /// (e.g. `[120, 60, 120, 60]` could be base `[120, 60]` repeated
    /// twice, or itself with repeat 1 - genuinely ambiguous). So editing
    /// loads it as the literal stored pattern with repeat reset to 1,
    /// which is exactly what's stored, not a guessed decomposition.
    fn start_editing_rule(&mut self, rule: &Rule) {
        self.editing_rule_id = Some(rule.id);
        self.editing_rule_enabled = rule.enabled;
        self.editing_rule_quick_rule = rule.quick_rule;
        self.new_rule_name = rule.name.clone();
        self.new_rule_targets = rule.targets.iter().cloned().collect();
        // Already-built `Trigger` values, not form fields - unlike the
        // action below, there's nothing to "reverse" here; the add-picker
        // underneath stays at whatever it was last showing, ready to add
        // another entry on top of these.
        self.new_rule_triggers = rule.triggers.clone();

        match &rule.action {
            Action::On => self.new_rule_action_kind = ActionKind::On,
            Action::Off => self.new_rule_action_kind = ActionKind::Off,
            Action::SetBrightness { pct } => {
                self.new_rule_action_kind = ActionKind::Brightness;
                self.new_rule_brightness = *pct;
            }
            Action::SetMode { pattern } => {
                self.new_rule_action_kind = ActionKind::Mode;
                self.new_rule_mode = *pattern;
            }
            Action::LoadProfile { slot } => {
                self.new_rule_action_kind = ActionKind::Profile;
                self.new_rule_profile = *slot;
            }
            Action::Notify { pattern_ms, fade_ms } => {
                self.new_rule_action_kind = ActionKind::NotifyPattern;
                self.new_rule_notify_pattern = pattern_ms
                    .as_ref()
                    .map(|v| v.iter().map(u32::to_string).collect::<Vec<_>>().join(" "))
                    .unwrap_or_default();
                self.new_rule_notify_repeat = 1;
                self.new_rule_notify_fade = fade_ms.unwrap_or(0);
            }
            Action::StartWake { seconds, soft, mode, brightness_pct } => {
                self.new_rule_action_kind = ActionKind::StartWake;
                self.new_rule_wake_seconds = *seconds;
                self.new_rule_wake_soft = *soft;
                self.new_rule_wake_mode_enabled = mode.is_some();
                if let Some(m) = mode {
                    self.new_rule_wake_mode = *m;
                }
                self.new_rule_wake_bri_enabled = brightness_pct.is_some();
                if let Some(b) = brightness_pct {
                    self.new_rule_wake_bri = *b;
                }
            }
            Action::SendMorse { text } => {
                self.new_rule_action_kind = ActionKind::SendMorse;
                self.new_rule_morse_text = text.clone();
            }
        }

        self.show_new_rule = true;
    }

    fn lamp_display_name(&self, lamp_key: &str) -> String {
        self.lamps
            .iter()
            .find(|l| l.lamp_key == lamp_key)
            .map(|l| l.name.clone())
            .unwrap_or_else(|| lamp_key.to_string())
    }

    /// `None` when disabled or no target lamp is selected - sending
    /// `SetVisualizer(None)` is exactly how the daemon-side loop is told
    /// to stop, so "turned off" and "nothing to target" collapse into the
    /// same state on purpose, rather than a separate enabled-with-no-
    /// effect state.
    fn visualizer_config(&self) -> Option<VisualizerConfig> {
        if !self.visualizer_enabled || self.visualizer_targets.is_empty() {
            return None;
        }
        Some(VisualizerConfig {
            targets: self.visualizer_targets.iter().cloned().collect(),
            gain: self.visualizer_gain,
            min_brightness: 5,
            max_brightness: 100,
        })
    }

    fn send_visualizer_update(&self) {
        let _ = self.to_core.send(UiToCore::SetVisualizer(self.visualizer_config()));
    }

    fn connections_ui(&mut self, ui: &mut egui::Ui) {
        let lang = self.lang;
        header_bar(ui, |ui| {
            ui.heading(t(lang, "lamps.heading"));
            let connected = self.lamps.iter().filter(|l| l.connection == "connected").count();
            ui.label(format!("{connected} {}", t(lang, "lamps.connectedSuffix")));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button(t(lang, "lamps.scanButton")).clicked() {
                    let _ = self.to_core.send(UiToCore::Scan);
                }
            });
        });
        ui.add_space(14.0);

        ui.horizontal_wrapped(|ui| {
            for lamp in &self.lamps {
                lamp_card(ui, lamp, &self.to_core, lang, &self.rules);
            }
        });

        if !self.scan_results.is_empty() {
            ui.add_space(18.0);
            discovered_lamps_ui(ui, lang, &self.scan_results, &self.to_core);
        }
    }

    fn visualizer_ui(&mut self, ui: &mut egui::Ui) {
        let lang = self.lang;
        header_bar(ui, |ui| {
            ui.heading(t(lang, "visualizer.heading"));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let mut enabled = self.visualizer_enabled;
                if ui.checkbox(&mut enabled, t(lang, "visualizer.enable")).changed() {
                    self.visualizer_enabled = enabled;
                    self.send_visualizer_update();
                }
            });
        });
        ui.add_space(14.0);
        ui.label(egui::RichText::new(t(lang, "visualizer.hint")).small().color(ui.visuals().weak_text_color()));
        if let Some(err) = &self.visualizer_unsupported {
            ui.colored_label(egui::Color32::from_rgb(0xF2, 0x55, 0x5A), format!("{}: {err}", t(lang, "visualizer.unsupported")));
        }
        ui.add_space(8.0);
        ui.horizontal(|ui| {
            ui.label(t(lang, "visualizer.gain"));
            if ui.add(egui::Slider::new(&mut self.visualizer_gain, 0.5..=20.0)).changed() {
                self.send_visualizer_update();
            }
        });
        ui.label(t(lang, "visualizer.targets"));
        ui.horizontal_wrapped(|ui| {
            for lamp in &self.lamps {
                let checked = self.visualizer_targets.contains(&lamp.lamp_key);
                if ui.selectable_label(checked, &lamp.name).clicked() {
                    if checked {
                        self.visualizer_targets.remove(&lamp.lamp_key);
                    } else {
                        self.visualizer_targets.insert(lamp.lamp_key.clone());
                    }
                    self.send_visualizer_update();
                }
            }
        });
    }

    fn rules_ui(&mut self, ui: &mut egui::Ui) {
        let lang = self.lang;
        header_bar(ui, |ui| {
            ui.heading(t(lang, "rules.heading"));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if !self.show_new_rule && ui.button(t(lang, "rules.newRuleButton")).clicked() {
                    self.show_new_rule = true;
                    self.editing_rule_id = None;
                    self.editing_rule_enabled = true;
                    self.editing_rule_quick_rule = None;
                    self.new_rule_name.clear();
                    self.new_rule_targets.clear();
                    self.new_rule_triggers.clear();
                }
            });
        });
        ui.add_space(14.0);

        if self.show_new_rule {
            egui::Frame::NONE
                .fill(ui.visuals().faint_bg_color)
                .corner_radius(10.0)
                .inner_margin(16.0)
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(t(lang, "rules.name"));
                        ui.text_edit_singleline(&mut self.new_rule_name);
                    });

                    ui.add_space(4.0);
                    ui.label(t(lang, "rules.triggersListHeading"));
                    let mut trigger_to_remove: Option<usize> = None;
                    for (idx, trig) in self.new_rule_triggers.iter().enumerate() {
                        ui.horizontal(|ui| {
                            ui.label(describe_trigger(lang, trig));
                            if ui.small_button(t(lang, "rules.delete")).clicked() {
                                trigger_to_remove = Some(idx);
                            }
                        });
                        if let Trigger::Webhook { id } = trig
                            && let Some(token) = &self.webhook_token {
                                let mut url = webhook_url(*id, token);
                                ui.text_edit_singleline(&mut url);
                            }
                    }
                    if let Some(idx) = trigger_to_remove {
                        self.new_rule_triggers.remove(idx);
                    }

                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.label(t(lang, "rules.when"));
                        egui::ComboBox::from_id_salt("new_rule_trigger_kind")
                            .selected_text(self.new_rule_trigger_kind.label(lang))
                            .show_ui(ui, |ui| {
                                for kind in TriggerKind::ALL {
                                    ui.selectable_value(&mut self.new_rule_trigger_kind, kind, kind.label(lang));
                                }
                            });
                    });
                    match self.new_rule_trigger_kind {
                        TriggerKind::OnAppOpen => {
                            ui.horizontal(|ui| {
                                ui.label(t(lang, "rules.appNameLabel"));
                                ui.text_edit_singleline(&mut self.new_rule_app_name);
                            });
                        }
                        TriggerKind::OnNotification => {
                            ui.horizontal(|ui| {
                                ui.label(t(lang, "rules.notificationAppFilterLabel"));
                                ui.text_edit_singleline(&mut self.new_rule_notification_app_filter);
                            });
                        }
                        TriggerKind::OnKeyPress => {
                            ui.horizontal(|ui| {
                                ui.checkbox(&mut self.new_rule_key_ctrl, t(lang, "rules.ctrl"));
                                ui.checkbox(&mut self.new_rule_key_alt, t(lang, "rules.alt"));
                                ui.checkbox(&mut self.new_rule_key_shift, t(lang, "rules.shift"));
                                ui.checkbox(&mut self.new_rule_key_super, t(lang, "rules.superCmd"));
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
                        TriggerKind::AtTime => {
                            ui.horizontal(|ui| {
                                ui.add(egui::DragValue::new(&mut self.new_rule_time_hour).range(0..=23));
                                ui.label(":");
                                ui.add(egui::DragValue::new(&mut self.new_rule_time_minute).range(0..=59));
                            });
                        }
                        _ => {}
                    }
                    if ui.add_enabled(self.add_trigger_valid(), egui::Button::new(t(lang, "rules.addTrigger"))).clicked()
                    {
                        self.new_rule_triggers.push(self.new_rule_trigger());
                    }

                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.label(t(lang, "rules.then"));
                        egui::ComboBox::from_id_salt("new_rule_action_kind")
                            .selected_text(self.new_rule_action_kind.label(lang))
                            .show_ui(ui, |ui| {
                                for kind in ActionKind::ALL {
                                    ui.selectable_value(&mut self.new_rule_action_kind, kind, kind.label(lang));
                                }
                            });
                    });
                    match self.new_rule_action_kind {
                        ActionKind::Brightness => {
                            ui.add(
                                egui::Slider::new(&mut self.new_rule_brightness, 0..=100)
                                    .text(t(lang, "rules.brightnessPercentLabel")),
                            );
                        }
                        ActionKind::Mode => {
                            ui.add(
                                egui::DragValue::new(&mut self.new_rule_mode)
                                    .range(1..=64)
                                    .prefix(t(lang, "rules.modePrefix")),
                            );
                        }
                        ActionKind::Profile => {
                            ui.add(
                                egui::DragValue::new(&mut self.new_rule_profile)
                                    .range(1..=6)
                                    .prefix(t(lang, "rules.profileSlotPrefix")),
                            );
                        }
                        ActionKind::NotifyPattern => {
                            ui.horizontal(|ui| {
                                ui.label(t(lang, "rules.patternLabel"));
                                ui.text_edit_singleline(&mut self.new_rule_notify_pattern)
                                    .on_hover_text(t(lang, "rules.patternHint"));
                            });
                            ui.horizontal(|ui| {
                                ui.add(
                                    egui::DragValue::new(&mut self.new_rule_notify_fade)
                                        .range(0..=2000)
                                        .suffix(t(lang, "rules.fadeSuffix")),
                                );
                                ui.add(
                                    egui::DragValue::new(&mut self.new_rule_notify_repeat)
                                        .range(1..=10)
                                        .prefix(t(lang, "rules.repeatPrefix")),
                                );
                            });
                        }
                        ActionKind::StartWake => {
                            ui.horizontal(|ui| {
                                ui.add(
                                    egui::DragValue::new(&mut self.new_rule_wake_seconds)
                                        .range(1..=3600)
                                        .suffix(t(lang, "rules.secondsSuffix")),
                                );
                                ui.checkbox(&mut self.new_rule_wake_soft, t(lang, "rules.soft"));
                            });
                            ui.horizontal(|ui| {
                                ui.checkbox(&mut self.new_rule_wake_mode_enabled, t(lang, "rules.modeCheckbox"));
                                ui.add_enabled(
                                    self.new_rule_wake_mode_enabled,
                                    egui::DragValue::new(&mut self.new_rule_wake_mode).range(1..=64),
                                );
                                ui.checkbox(&mut self.new_rule_wake_bri_enabled, t(lang, "rules.brightnessPercentLabel"));
                                ui.add_enabled(
                                    self.new_rule_wake_bri_enabled,
                                    egui::DragValue::new(&mut self.new_rule_wake_bri).range(0..=100),
                                );
                            });
                        }
                        ActionKind::SendMorse => {
                            ui.horizontal(|ui| {
                                ui.label(t(lang, "rules.textLabel"));
                                ui.text_edit_singleline(&mut self.new_rule_morse_text);
                            });
                        }
                        ActionKind::On | ActionKind::Off => {}
                    }

                    ui.add_space(4.0);
                    ui.label(t(lang, "rules.targets"));
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
                        if ui.button(t(lang, "rules.cancel")).clicked() {
                            self.show_new_rule = false;
                            self.editing_rule_id = None;
                            self.editing_rule_quick_rule = None;
                        }
                        if ui.add_enabled(self.new_rule_valid(), egui::Button::new(t(lang, "rules.saveRule"))).clicked() {
                            let rule = Rule {
                                id: self.editing_rule_id.unwrap_or_else(Uuid::new_v4),
                                name: std::mem::take(&mut self.new_rule_name),
                                enabled: self.editing_rule_enabled,
                                triggers: std::mem::take(&mut self.new_rule_triggers),
                                targets: self.new_rule_targets.iter().cloned().collect(),
                                action: self.new_rule_action(),
                                quick_rule: self.editing_rule_quick_rule,
                            };
                            self.new_rule_targets.clear();
                            self.show_new_rule = false;
                            self.editing_rule_id = None;
                            self.editing_rule_quick_rule = None;
                            let _ = self.to_core.send(UiToCore::SaveRule(rule));
                        }
                    });
                });
            ui.add_space(14.0);
        }

        // `start_editing_rule` needs `&mut self`, which can't coexist with
        // `rule: &Rule` borrowed from `&self.rules` below - deferred past
        // the loop, same reason the `.clone()` a few lines down exists for
        // the enabled-checkbox's `SaveRule` (that one only needs `&self`,
        // so it doesn't have to be deferred, just cloned).
        let mut rule_to_edit: Option<Rule> = None;

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
                            if ui.small_button(t(lang, "rules.delete")).clicked() {
                                let _ = self.to_core.send(UiToCore::DeleteRule(rule.id));
                            }
                            if ui.small_button(t(lang, "rules.fireNow")).clicked() {
                                let _ = self.to_core.send(UiToCore::FireRule(rule.id));
                            }
                            if ui.small_button(t(lang, "rules.edit")).clicked() {
                                rule_to_edit = Some(rule.clone());
                            }
                        });
                    });
                    let targets: Vec<String> = rule.targets.iter().map(|k| self.lamp_display_name(k)).collect();
                    let triggers_desc: Vec<String> =
                        rule.triggers.iter().map(|t| describe_trigger(lang, t)).collect();
                    ui.label(
                        egui::RichText::new(format!(
                            "{} \u{2192} {} \u{b7} {}",
                            triggers_desc.join(&format!(" {} ", t(lang, "rules.orSeparator"))),
                            describe_action(lang, &rule.action),
                            targets.join(", ")
                        ))
                        .small()
                        .color(ui.visuals().weak_text_color()),
                    );
                    for trigger in &rule.triggers {
                        if let Trigger::Webhook { id } = trigger
                            && let Some(token) = &self.webhook_token {
                                let mut url = webhook_url(*id, token);
                                ui.add(
                                    egui::TextEdit::singleline(&mut url).desired_width(f32::INFINITY),
                                );
                            }
                    }
                });
            ui.add_space(8.0);
        }

        if let Some(rule) = rule_to_edit {
            self.start_editing_rule(&rule);
        }
    }
}

/// One sidebar nav entry, stretched to (nearly) the full sidebar width -
/// `ui.selectable_label` alone sizes to its text, so "Connections" and
/// "Rules" ended up visibly different widths.
fn nav_button(ui: &mut egui::Ui, selected: bool, text: &str) -> bool {
    let width = ui.available_width();
    ui.add_sized(egui::vec2(width, 30.0), egui::Button::selectable(selected, text)).clicked()
}

/// A section's title + primary action, raised on its own panel-colored bar
/// (full width, rounded, a thin bottom accent line) so it reads as a
/// toolbar distinct from the scrolling content below it, rather than just
/// another line of plain text.
fn header_bar(ui: &mut egui::Ui, add_contents: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::NONE
        .fill(PANEL)
        .stroke(egui::Stroke::new(1.0, BORDER))
        .corner_radius(10.0)
        .inner_margin(egui::Margin::symmetric(16, 12))
        .show(ui, |ui| {
            ui.horizontal(|ui| add_contents(ui));
        });
}

/// The full URL an external tool (Stream Deck, Home Assistant's
/// `rest_command`, a shell script) needs to fire a `Trigger::Webhook` -
/// `quarzlampe_core::ipc::WEBHOOK_PORT` is fixed across daemon restarts
/// (see its own doc comment for why), so only the trigger's own `id` and
/// the daemon's current auth token vary.
fn webhook_url(id: Uuid, token: &str) -> String {
    format!("http://127.0.0.1:{}/webhook/{id}?token={token}", quarzlampe_core::ipc::WEBHOOK_PORT)
}

fn describe_trigger(lang: Lang, trigger: &Trigger) -> String {
    match (lang, trigger) {
        (Lang::En, Trigger::MicActive) => "mic becomes active".to_string(),
        (Lang::De, Trigger::MicActive) => "Mikrofon wird aktiv".to_string(),
        (Lang::En, Trigger::MicInactive) => "mic becomes inactive".to_string(),
        (Lang::De, Trigger::MicInactive) => "Mikrofon wird inaktiv".to_string(),
        (Lang::En, Trigger::CameraActive) => "camera becomes active".to_string(),
        (Lang::De, Trigger::CameraActive) => "Kamera wird aktiv".to_string(),
        (Lang::En, Trigger::CameraInactive) => "camera becomes inactive".to_string(),
        (Lang::De, Trigger::CameraInactive) => "Kamera wird inaktiv".to_string(),
        (Lang::En, Trigger::OnBoot) => "daemon starts".to_string(),
        (Lang::De, Trigger::OnBoot) => "Daemon startet".to_string(),
        (Lang::En, Trigger::OnBeforeSleep) => "system is about to sleep".to_string(),
        (Lang::De, Trigger::OnBeforeSleep) => "System geht gleich in den Ruhezustand".to_string(),
        (Lang::En, Trigger::BeforeShutdown) => "system is about to shut down".to_string(),
        (Lang::De, Trigger::BeforeShutdown) => "System f\u{e4}hrt gleich herunter".to_string(),
        (Lang::En, Trigger::OnNotification { app_name_filter: None }) => "a notification appears".to_string(),
        (Lang::De, Trigger::OnNotification { app_name_filter: None }) => "eine Benachrichtigung erscheint".to_string(),
        (Lang::En, Trigger::OnNotification { app_name_filter: Some(f) }) => format!("a notification from \"{f}\" appears"),
        (Lang::De, Trigger::OnNotification { app_name_filter: Some(f) }) => format!("eine Benachrichtigung von \"{f}\" erscheint"),
        (Lang::En, Trigger::OnAppOpen { app_name }) => format!("\"{app_name}\" starts"),
        (Lang::De, Trigger::OnAppOpen { app_name }) => format!("\"{app_name}\" startet"),
        (Lang::En, Trigger::OnKeyPress { key }) => format!("hotkey {key} is pressed"),
        (Lang::De, Trigger::OnKeyPress { key }) => format!("Tastenkombination {key} wird gedr\u{fc}ckt"),
        (Lang::En, Trigger::AtTime { hour, minute }) => format!("daily at {hour:02}:{minute:02}"),
        (Lang::De, Trigger::AtTime { hour, minute }) => format!("t\u{e4}glich um {hour:02}:{minute:02}"),
        (Lang::En, Trigger::ScreenLocked) => "the screen locks".to_string(),
        (Lang::De, Trigger::ScreenLocked) => "der Bildschirm gesperrt wird".to_string(),
        (Lang::En, Trigger::ScreenUnlocked) => "the screen unlocks".to_string(),
        (Lang::De, Trigger::ScreenUnlocked) => "der Bildschirm entsperrt wird".to_string(),
        (Lang::En, Trigger::Webhook { .. }) => "a webhook request arrives".to_string(),
        (Lang::De, Trigger::Webhook { .. }) => "eine Webhook-Anfrage eingeht".to_string(),
    }
}

fn describe_action(lang: Lang, action: &Action) -> String {
    match (lang, action) {
        (Lang::En, Action::On) => "turn on".to_string(),
        (Lang::De, Action::On) => "einschalten".to_string(),
        (Lang::En, Action::Off) => "turn off".to_string(),
        (Lang::De, Action::Off) => "ausschalten".to_string(),
        (Lang::En, Action::SetBrightness { pct }) => format!("set brightness to {pct}%"),
        (Lang::De, Action::SetBrightness { pct }) => format!("Helligkeit auf {pct}% setzen"),
        (Lang::En, Action::SetMode { pattern }) => format!("set mode to {pattern}"),
        (Lang::De, Action::SetMode { pattern }) => format!("Modus auf {pattern} setzen"),
        (Lang::En, Action::LoadProfile { slot }) => format!("load profile {slot}"),
        (Lang::De, Action::LoadProfile { slot }) => format!("Profil {slot} laden"),
        (Lang::En, Action::Notify { .. }) => "send notify pattern".to_string(),
        (Lang::De, Action::Notify { .. }) => "Benachrichtigungsmuster senden".to_string(),
        (Lang::En, Action::StartWake { seconds, soft, mode, brightness_pct }) => {
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
        (Lang::De, Action::StartWake { seconds, soft, mode, brightness_pct }) => {
            let mut parts = vec![format!("{seconds}s-Weckvorgang starten")];
            if *soft {
                parts.push("(sanft)".to_string());
            }
            if let Some(m) = mode {
                parts.push(format!("Modus {m}"));
            }
            if let Some(b) = brightness_pct {
                parts.push(format!("auf {b}%"));
            }
            parts.join(" ")
        }
        (Lang::En, Action::SendMorse { text }) => format!("send morse \"{text}\""),
        (Lang::De, Action::SendMorse { text }) => format!("Morsecode \"{text}\" senden"),
    }
}

fn status_visual(lang: Lang, connection: &str) -> (egui::Color32, &'static str) {
    match connection {
        "connected" => (egui::Color32::from_rgb(0x4A, 0xDE, 0x80), t(lang, "status.connected")),
        "connecting" => (WARNING, t(lang, "status.connecting")),
        "reconnecting" => (WARNING, t(lang, "status.reconnecting")),
        "reconnect_failed" => (egui::Color32::from_rgb(0xF2, 0x55, 0x5A), t(lang, "status.reconnectFailed")),
        "awaiting_trust_token" => (WARNING, t(lang, "status.awaitingTrust")),
        "not_in_range" => (MUTED, t(lang, "status.notInRange")),
        _ => (MUTED, t(lang, "status.disconnected")),
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

/// Is `kind`'s quick rule currently on for this lamp - i.e. does a rule
/// tagged with it, targeting *only* this lamp, already exist? Quick
/// rules are always single-target by construction (see `build_quick_rule`),
/// so "targets == [lamp_key]" is enough to identify them, no separate id
/// bookkeeping needed.
fn quick_rule_active(rules: &[Rule], kind: QuickRuleKind, lamp_key: &str) -> bool {
    rules.iter().any(|r| r.quick_rule == Some(kind) && r.targets.len() == 1 && r.targets[0] == lamp_key)
}

fn quick_rule_ids(rules: &[Rule], kind: QuickRuleKind, lamp_key: &str) -> Vec<Uuid> {
    rules
        .iter()
        .filter(|r| r.quick_rule == Some(kind) && r.targets.len() == 1 && r.targets[0] == lamp_key)
        .map(|r| r.id)
        .collect()
}

/// Builds the actual `Rule`s a quick rule expands into for one lamp - the
/// console does this (not `core`) specifically so the generated names are
/// in the user's own language via `i18n`, not a hardcoded English string
/// baked into the model layer.
fn build_quick_rule(lang: Lang, kind: QuickRuleKind, lamp_key: &str) -> Vec<Rule> {
    kind.rule_specs()
        .into_iter()
        .map(|(trigger, action)| {
            let name_key = match (kind, &trigger) {
                (QuickRuleKind::OnAir, Trigger::MicActive) => "quickRule.onAirRuleNameOn",
                (QuickRuleKind::OnAir, Trigger::MicInactive) => "quickRule.onAirRuleNameOff",
                (QuickRuleKind::TeamsNotification, _) => "quickRule.teamsNotificationRuleName",
                _ => "quickRule.onAir",
            };
            Rule {
                id: Uuid::new_v4(),
                name: t(lang, name_key).to_string(),
                enabled: true,
                triggers: vec![trigger],
                targets: vec![lamp_key.to_string()],
                action,
                quick_rule: Some(kind),
            }
        })
        .collect()
}

/// One quick-rule toggle row on a lamp card: a label, a switch reflecting
/// whether this kind's tagged rules already exist for this lamp, and a
/// hint line underneath - the same three-piece layout every quick rule
/// uses, factored out once a second one (`TeamsNotification`) joined the
/// original `OnAir` toggle so the two wouldn't just be copy-pasted blocks
/// drifting apart over time.
#[allow(clippy::too_many_arguments)]
fn quick_rule_toggle_row(
    ui: &mut egui::Ui,
    lang: Lang,
    rules: &[Rule],
    lamp_key: &str,
    to_core: &tokio_mpsc::UnboundedSender<UiToCore>,
    kind: QuickRuleKind,
    label_key: &'static str,
    hint_key: &'static str,
) {
    ui.horizontal(|ui| {
        ui.label(t(lang, label_key));
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            let mut on = quick_rule_active(rules, kind, lamp_key);
            if toggle_ui(ui, &mut on).changed() {
                if on {
                    for rule in build_quick_rule(lang, kind, lamp_key) {
                        let _ = to_core.send(UiToCore::SaveRule(rule));
                    }
                } else {
                    for id in quick_rule_ids(rules, kind, lamp_key) {
                        let _ = to_core.send(UiToCore::DeleteRule(id));
                    }
                }
            }
        });
    });
    ui.label(egui::RichText::new(t(lang, hint_key)).small().color(ui.visuals().weak_text_color()));
}

/// The "found nearby, not yet paired" list a scan produces - framed like a
/// card (same `extreme_bg_color`/rounded-corner treatment as `lamp_card`)
/// so it reads as its own distinct block directly under the lamp grid the
/// Scan button belongs to, instead of a couple of loose labels floating at
/// the bottom of the page below unrelated content.
fn discovered_lamps_ui(
    ui: &mut egui::Ui,
    lang: Lang,
    scan_results: &[ScannedLamp],
    to_core: &tokio_mpsc::UnboundedSender<UiToCore>,
) {
    egui::Frame::NONE.fill(ui.visuals().extreme_bg_color).corner_radius(12.0).inner_margin(16.0).show(ui, |ui| {
        ui.vertical(|ui| {
            ui.spacing_mut().item_spacing.y = 8.0;
            ui.label(egui::RichText::new(t(lang, "lamps.discoveredHeading")).small().strong());
            ui.label(egui::RichText::new(t(lang, "lamps.discoveredHint")).small().color(ui.visuals().weak_text_color()));
            ui.separator();
            for (i, found) in scan_results.iter().enumerate() {
                if i > 0 {
                    ui.separator();
                }
                ui.horizontal(|ui| {
                    ui.label(&found.name);
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.button(t(lang, "lamps.connectButton")).clicked() {
                            let _ = to_core.send(UiToCore::Connect {
                                lamp_key: found.lamp_key.clone(),
                                name: found.name.clone(),
                            });
                        }
                    });
                });
            }
        });
    });
}

/// One lamp's card: name + status, inline power/brightness/mode/profile
/// quick-actions, all disabled unless the lamp is actually connected, plus
/// any quick-rule toggles (see `QuickRuleKind`) at the bottom - those stay
/// available even when disconnected, since toggling one just saves/deletes
/// ordinary rules through the daemon, the same as the rule editor would.
/// A free function (not a `ConsoleApp` method) since it only needs the
/// lamp's own data, the current rule list, and a way to send commands -
/// keeps the borrow simple when called from `connections_ui(&mut self)`.
fn lamp_card(
    ui: &mut egui::Ui,
    lamp: &LampSummary,
    to_core: &tokio_mpsc::UnboundedSender<UiToCore>,
    lang: Lang,
    rules: &[Rule],
) {
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
                        // The daemon gave up retrying on its own (see
                        // `ConnectionState::ReconnectFailed`) - a button
                        // here instead of the usual status label/dot is
                        // the only way to get it trying again, since
                        // there's no more timer doing it automatically.
                        if lamp.connection == "reconnect_failed" {
                            if ui.button(t(lang, "lampCard.reconnectButton")).clicked() {
                                let _ = to_core.send(UiToCore::Reconnect(lamp.lamp_key.clone()));
                            }
                        } else {
                            let (color, label) = status_visual(lang, &lamp.connection);
                            ui.label(label);
                            ui.colored_label(color, "\u{25cf}");
                        }
                    });
                });

                if let Some(err) = &lamp.last_error {
                    ui.colored_label(egui::Color32::from_rgb(0xF2, 0x55, 0x5A), err);
                }

                ui.separator();

                ui.horizontal(|ui| {
                    ui.label(t(lang, "lampCard.power"));
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
                    ui.label(t(lang, "lampCard.brightness"));
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
                    ui.label(t(lang, "lampCard.mode"));
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

                ui.label(t(lang, "lampCard.profile"));
                ui.horizontal(|ui| {
                    ui.add_enabled_ui(connected, |ui| {
                        for slot in 1u8..=6 {
                            if ui.small_button(slot.to_string()).clicked() {
                                send(format!("profile load {slot}"));
                            }
                        }
                    });
                });

                ui.separator();
                quick_rule_toggle_row(
                    ui,
                    lang,
                    rules,
                    &lamp.lamp_key,
                    to_core,
                    QuickRuleKind::OnAir,
                    "quickRule.onAir",
                    "quickRule.onAirHint",
                );
                quick_rule_toggle_row(
                    ui,
                    lang,
                    rules,
                    &lamp.lamp_key,
                    to_core,
                    QuickRuleKind::TeamsNotification,
                    "quickRule.teamsNotification",
                    "quickRule.teamsNotificationHint",
                );
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
                CoreToUi::Visualizer { config, last_error } => {
                    self.visualizer_enabled = config.is_some();
                    if let Some(cfg) = &config {
                        self.visualizer_targets = cfg.targets.iter().cloned().collect();
                        self.visualizer_gain = cfg.gain;
                    }
                    self.visualizer_unsupported = last_error;
                }
                CoreToUi::UpdateStatus(available) => self.update_available = available,
                CoreToUi::Status(s) => self.status = s,
                CoreToUi::WebhookToken(token) => self.webhook_token = Some(token),
            }
        }

        let lang = self.lang;
        let nav_frame = egui::Frame::NONE.fill(ui.visuals().extreme_bg_color).inner_margin(14.0);
        // `exact_size`, not `default_size` + `resizable(false)` - the panel
        // remembers its width across frames keyed by id, and `default_size`
        // only applies the very first time it's shown. Once the window got
        // narrow enough to clamp it smaller, nothing pulled it back to
        // 180px on widening again. `exact_size` pins the allowed range to a
        // single point, so it can't drift in either direction.
        egui::Panel::left("nav").exact_size(180.0).frame(nav_frame).show(ui, |ui| {
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                ui.add(egui::Image::new(&self.icon_texture).fit_to_exact_size(egui::vec2(20.0, 20.0)));
                ui.add_space(4.0);
                ui.strong("Quarzlampe");
            });
            ui.add_space(18.0);
            if nav_button(ui, self.section == Section::Connections, t(lang, "nav.connections")) {
                self.section = Section::Connections;
            }
            ui.add_space(4.0);
            if nav_button(ui, self.section == Section::Rules, t(lang, "nav.rules")) {
                self.section = Section::Rules;
            }
            ui.add_space(4.0);
            if nav_button(ui, self.section == Section::Visualizer, t(lang, "nav.visualizer")) {
                self.section = Section::Visualizer;
            }
            ui.with_layout(egui::Layout::bottom_up(egui::Align::LEFT), |ui| {
                ui.add_space(10.0);
                ui.label(egui::RichText::new(&self.status).small().color(ui.visuals().weak_text_color()));
                // All configuration (profiles, patterns, hardware settings,
                // MIDI, OTA...) lives in the web app, not here - the console
                // is deliberately a thin quick-control/automation surface,
                // not a reimplementation of it.
                ui.hyperlink_to(t(lang, "nav.openWeb"), WEB_INTERFACE_URL);
                if let Some(info) = &self.update_available {
                    ui.add_space(4.0);
                    ui.hyperlink_to(t(lang, "nav.updateAvailable"), &info.html_url);
                }
                ui.add_space(4.0);
            });
        });

        egui::CentralPanel::default().show(ui, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| match self.section {
                Section::Connections => self.connections_ui(ui),
                Section::Rules => self.rules_ui(ui),
                Section::Visualizer => self.visualizer_ui(ui),
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
fn decode_icon() -> image::RgbaImage {
    let bytes = include_bytes!("../assets/icon.png");
    image::load_from_memory(bytes).expect("bundled icon.png is valid").into_rgba8()
}

fn load_window_icon() -> egui::IconData {
    let image = decode_icon();
    let (width, height) = image.dimensions();
    egui::IconData { rgba: image.into_raw(), width, height }
}

/// Same icon, as an egui texture this time - for showing it *inside* the
/// UI (the sidebar header), not just as the OS window/taskbar icon.
fn load_icon_texture(ctx: &egui::Context) -> egui::TextureHandle {
    let image = decode_icon();
    let (width, height) = image.dimensions();
    let color_image = egui::ColorImage::from_rgba_unmultiplied([width as usize, height as usize], &image.into_raw());
    ctx.load_texture("app-icon", color_image, egui::TextureOptions::default())
}

/// Dark theme with the brand's warm accent color, plus a bit more breathing
/// room than egui's defaults (spacing/padding), matching the approved
/// console sketch - see PLAN.md's "Console UI direction" note. Not a claim
/// that this looks like a native OS widget set; see that note for why.
fn apply_visuals(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.override_text_color = Some(TEXT);
    visuals.weak_text_color = Some(MUTED);
    visuals.panel_fill = BG;
    visuals.window_fill = BG;
    visuals.extreme_bg_color = PANEL;
    visuals.faint_bg_color = PANEL;
    visuals.selection.bg_fill = ACCENT;
    // Accent is light (brand cyan) - text sitting on it needs a dark color,
    // not egui's default light-on-selection text.
    visuals.selection.stroke.color = BG;
    visuals.hyperlink_color = ACCENT;
    visuals.widgets.noninteractive.bg_stroke.color = BORDER;
    ctx.set_visuals(visuals);

    ctx.all_styles_mut(|style| {
        style.spacing.item_spacing = egui::vec2(10.0, 10.0);
        style.spacing.button_padding = egui::vec2(10.0, 6.0);
        style.spacing.interact_size.y = 24.0;
    });
}

fn main() -> eframe::Result<()> {
    let lang = Lang::detect();
    let (to_core, from_ui) = tokio_mpsc::unbounded_channel::<UiToCore>();
    let (to_ui, from_core) = std_mpsc::channel::<CoreToUi>();

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
            let icon_texture = load_icon_texture(&cc.egui_ctx);
            // Spawned here, not earlier, so run_core can take a clone of the
            // real egui::Context and poke it directly on every update
            // instead of waiting for ConsoleApp::ui's periodic repaint.
            let ctx_for_core = cc.egui_ctx.clone();
            std::thread::spawn(move || {
                let rt = tokio::runtime::Runtime::new().expect("failed to start tokio runtime");
                rt.block_on(run_core(from_ui, to_ui, lang, ctx_for_core));
            });
            Ok(Box::new(ConsoleApp {
                lang,
                icon_texture,
                to_core,
                from_core,
                lamps: Vec::new(),
                scan_results: Vec::new(),
                rules: Vec::new(),
                status: t(lang, "console.connectingToDaemon").to_string(),
                section: Section::Connections,
                show_new_rule: false,
                new_rule_name: String::new(),
                new_rule_targets: HashSet::new(),
                new_rule_triggers: Vec::new(),
                new_rule_trigger_kind: TriggerKind::MicActive,
                new_rule_app_name: String::new(),
                new_rule_notification_app_filter: String::new(),
                new_rule_time_hour: 20,
                new_rule_time_minute: 0,
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
                editing_rule_id: None,
                editing_rule_enabled: true,
                editing_rule_quick_rule: None,
                visualizer_enabled: false,
                visualizer_targets: HashSet::new(),
                visualizer_gain: 4.0,
                visualizer_unsupported: None,
                update_available: None,
                webhook_token: None,
            }))
        }),
    )
}
