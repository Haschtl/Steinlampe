//! Minimal, in-house i18n for the desktop app - English and German, picked
//! up from the OS locale by default (no manual language switcher; that's
//! a reasonable follow-up, not done here). Not a framework (no fluent/
//! gettext dependency) - a flat static table, the same spirit as the web
//! frontend's own `i18n.tsx`, just in Rust and shared between the tray
//! daemon and the console, since both need some of the same strings
//! (status labels in particular).
//!
//! Scope: this covers every *static* label (buttons, headings, field
//! names). The handful of strings that interpolate live data (the tray's
//! tooltip/status text, the console's rule-description sentences) aren't
//! in this table - they're built with a `match (lang, ...)` directly where
//! they're used, since a tiny templating engine would be more machinery
//! than four or five sentences are worth.

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lang {
    En,
    De,
}

impl Lang {
    /// `sys-locale` does the actual per-OS lookup (`$LANG` on Linux,
    /// `GetUserDefaultLocaleName` on Windows, `NSLocale` on macOS) - this
    /// just checks whether the result starts with "de"; anything else
    /// (including "couldn't detect anything") falls back to English, which
    /// is always a safe default, not an error.
    pub fn detect() -> Self {
        match sys_locale::get_locale() {
            Some(locale) if locale.to_lowercase().starts_with("de") => Lang::De,
            _ => Lang::En,
        }
    }
}

macro_rules! dict {
    ($($key:literal => [$en:literal, $de:literal]),* $(,)?) => {
        /// Looks up a static label. Falls back to the key itself if it's
        /// somehow not in the table (visible-but-harmless, same fallback
        /// behavior the web frontend's `t()` uses for a missing key) -
        /// easier to spot a typo'd key during development than a panic.
        /// `key` is `&'static str` because every call site passes a string
        /// literal - there's no case where a translation key is computed
        /// at runtime.
        pub fn t(lang: Lang, key: &'static str) -> &'static str {
            match key {
                $($key => match lang {
                    Lang::En => $en,
                    Lang::De => $de,
                },)*
                other => other,
            }
        }
    };
}

dict! {
    // Shared connection/power status words - used by both the tray menu
    // and the console's lamp cards, so they read the same everywhere.
    "status.connected" => ["Connected", "Verbunden"],
    "status.connecting" => ["Connecting\u{2026}", "Verbindet\u{2026}"],
    "status.reconnecting" => ["Reconnecting\u{2026}", "Verbindet erneut\u{2026}"],
    "status.awaitingTrust" => ["Awaiting trust token\u{2026}", "Warte auf Trust-Token\u{2026}"],
    "status.disconnected" => ["Disconnected", "Getrennt"],
    "status.on" => ["On", "An"],
    "status.off" => ["Off", "Aus"],
    "status.unknown" => ["?", "?"],

    // Tray menu.
    "tray.showConsole" => ["Show console", "Konsole \u{f6}ffnen"],
    "tray.startOnBoot" => ["Start on boot", "Beim Start ausf\u{fc}hren"],
    "tray.quit" => ["Quit", "Beenden"],
    "tray.starting" => ["Starting\u{2026}", "Startet\u{2026}"],
    "tray.noLamps" => ["No lamps connected", "Keine Lampen verbunden"],
    "tray.turnOn" => ["Turn on", "Einschalten"],
    "tray.turnOff" => ["Turn off", "Ausschalten"],
    "tray.brightness" => ["Brightness", "Helligkeit"],
    "tray.tooltipConnectedWord" => ["connected", "verbunden"],
    "tray.tooltipOnWord" => ["on", "an"],
    "tray.tooltipNoLamps" => ["no lamps connected", "keine Lampen verbunden"],

    // Console: sidebar nav + chrome.
    "nav.connections" => ["Connections", "Verbindungen"],
    "nav.rules" => ["Rules", "Regeln"],
    "nav.openWeb" => ["Open web interface \u{2197}", "Weboberfl\u{e4}che \u{f6}ffnen \u{2197}"],
    "console.connectingToDaemon" => ["connecting to daemon...", "verbindet mit Daemon\u{2026}"],
    "console.connectedToDaemon" => ["connected to daemon", "mit Daemon verbunden"],
    "console.waitingForDaemon" => ["waiting for daemon", "warte auf Daemon"],

    // Console: Connections section.
    "lamps.heading" => ["Lamps", "Lampen"],
    "lamps.connectedSuffix" => ["connected", "verbunden"],
    "lamps.scanButton" => ["Scan for lamps", "Nach Lampen suchen"],
    "lamps.discoveredHeading" => ["DISCOVERED NEARBY", "IN DER N\u{c4}HE GEFUNDEN"],
    "lamps.discoveredHint" =>
        ["Pair within ~60s of boot, or it already has a saved token",
         "Innerhalb von ~60s nach dem Start koppeln, oder sie hat bereits ein gespeichertes Token"],
    "lamps.connectButton" => ["Connect", "Verbinden"],
    "lampCard.power" => ["Power", "Ein/Aus"],
    "lampCard.brightness" => ["Brightness", "Helligkeit"],
    "lampCard.mode" => ["Mode", "Modus"],
    "lampCard.profile" => ["Profile", "Profil"],

    // Console: Rules section.
    "rules.heading" => ["Rules", "Regeln"],
    "rules.newRuleButton" => ["+ New rule", "+ Neue Regel"],
    "rules.name" => ["Name:", "Name:"],
    "rules.when" => ["When:", "Wenn:"],
    "rules.then" => ["Then:", "Dann:"],
    "rules.targets" => ["Targets:", "Ziele:"],
    "rules.cancel" => ["Cancel", "Abbrechen"],
    "rules.saveRule" => ["Save rule", "Regel speichern"],
    "rules.delete" => ["Delete", "L\u{f6}schen"],
    "rules.fireNow" => ["Fire now", "Jetzt ausf\u{fc}hren"],
    "rules.appNameLabel" =>
        ["App name (as the OS reports it, e.g. \"firefox\"):",
         "App-Name (wie vom Betriebssystem gemeldet, z. B. \"firefox\"):"],
    "rules.ctrl" => ["Ctrl", "Strg"],
    "rules.alt" => ["Alt", "Alt"],
    "rules.shift" => ["Shift", "Umschalt"],
    "rules.superCmd" => ["Super/Cmd", "Super/Cmd"],
    "rules.patternLabel" =>
        ["Pattern (ms, space-separated on/off):", "Muster (ms, durch Leerzeichen getrennt, an/aus):"],
    "rules.patternHint" =>
        ["Leave empty for the firmware's default (120 60 120 200)",
         "Leer lassen f\u{fc}r die Standardwerte der Firmware (120 60 120 200)"],
    "rules.fadeSuffix" => [" ms fade", " ms Fade"],
    "rules.repeatPrefix" => ["repeat ", "Wiederholungen "],
    "rules.soft" => ["soft", "sanft"],
    "rules.modeCheckbox" => ["mode", "Modus"],
    "rules.modePrefix" => ["mode ", "Modus "],
    "rules.brightnessPercentLabel" => ["brightness %", "Helligkeit %"],
    "rules.profileSlotPrefix" => ["profile slot ", "Profil-Slot "],
    "rules.secondsSuffix" => [" s", " s"],
    "rules.textLabel" => ["Text:", "Text:"],

    // New-rule trigger picker (TriggerKind labels).
    "trigger.micActive" => ["Mic becomes active", "Mikrofon wird aktiv"],
    "trigger.micInactive" => ["Mic becomes inactive", "Mikrofon wird inaktiv"],
    "trigger.cameraActive" => ["Camera becomes active", "Kamera wird aktiv"],
    "trigger.cameraInactive" => ["Camera becomes inactive", "Kamera wird inaktiv"],
    "trigger.onBoot" => ["Daemon starts (on boot)", "Daemon startet (beim Hochfahren)"],
    "trigger.onBeforeSleep" =>
        ["System is about to sleep (Linux only)", "System geht gleich in den Ruhezustand (nur Linux)"],
    "trigger.beforeShutdown" =>
        ["System is about to shut down (Linux only)", "System f\u{e4}hrt gleich herunter (nur Linux)"],
    "trigger.onNotification" =>
        ["A notification appears (Linux, best-effort)", "Eine Benachrichtigung erscheint (Linux, bestm\u{f6}glich)"],
    "trigger.onAppOpen" => ["An app starts", "Eine App startet"],
    "trigger.onKeyPress" =>
        ["A hotkey is pressed (not on Wayland)", "Eine Tastenkombination wird gedr\u{fc}ckt (nicht unter Wayland)"],

    // New-rule action picker (ActionKind labels).
    "actionKind.on" => ["On", "Ein"],
    "actionKind.off" => ["Off", "Aus"],
    "actionKind.brightness" => ["Set brightness", "Helligkeit einstellen"],
    "actionKind.mode" => ["Set mode", "Modus einstellen"],
    "actionKind.profile" => ["Load profile", "Profil laden"],
    "actionKind.notifyPattern" => ["Send notify pattern", "Benachrichtigungsmuster senden"],
    "actionKind.startWake" => ["Start wake-up", "Weckvorgang starten"],
    "actionKind.sendMorse" => ["Send morse code", "Morsecode senden"],

    // Console: music-reactive visualizer panel.
    "visualizer.heading" => ["Music Visualizer", "Musik-Visualizer"],
    "visualizer.hint" =>
        ["Loudness of whatever's playing on this PC drives brightness, live.",
         "Lautst\u{e4}rke dessen, was auf diesem PC l\u{e4}uft, steuert live die Helligkeit."],
    "visualizer.enable" => ["Enable", "Aktivieren"],
    "visualizer.gain" => ["Gain", "Verst\u{e4}rkung"],
    "visualizer.targets" => ["Targets:", "Ziele:"],
    "visualizer.unsupported" =>
        ["Not available on this platform/setup", "Auf dieser Plattform/Konfiguration nicht verf\u{fc}gbar"],

    // Console: quick rules (pre-packaged automations toggled on the lamp
    // card itself, for casual users who'd rather flip one switch than
    // build a rule pair by hand).
    "quickRule.onAir" => ["On-Air", "On-Air"],
    "quickRule.onAirHint" =>
        ["Turns this lamp on while the mic is active, off again once it isn't",
         "Schaltet diese Lampe ein, solange das Mikrofon aktiv ist, und wieder aus, sobald nicht mehr"],
    "quickRule.onAirRuleNameOn" => ["On-Air: turn on", "On-Air: einschalten"],
    "quickRule.onAirRuleNameOff" => ["On-Air: turn off", "On-Air: ausschalten"],
}
