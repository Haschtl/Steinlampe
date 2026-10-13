//! `Action` (M3) and `Trigger`/`Rule` (M4/M5) - the "when X happens, do Y
//! to lamp(s) Z" automation layer. New variants of either enum slot in with
//! no restructuring of `Rule` or its persistence - that's the whole point
//! of the `#[serde(tag = "type")]` shape.

use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Action {
    On,
    Off,
    SetBrightness { pct: u8 },
    SetMode { pattern: u32 },
    LoadProfile { slot: u8 },
    Notify { pattern_ms: Option<Vec<u32>>, fade_ms: Option<u32> },
    /// `wake [soft] [mode=<n>] [bri=<x>] <seconds>` - firmware's wake fade.
    /// `mode`/`brightness_pct` are only sent if set (otherwise the firmware
    /// keeps the current pattern / picks its own default ramp target).
    StartWake { seconds: u32, soft: bool, mode: Option<u32>, brightness_pct: Option<u8> },
    /// `morse <text>` - the firmware uppercases and maps A-Z/0-9 itself;
    /// nothing to validate here beyond "don't send an empty string" (the
    /// console's save button already requires non-empty).
    SendMorse { text: String },
}

/// Only `MicActive` fires through the real evaluation loop today (M5);
/// `OnBeforeSleep`/`BeforeShutdown`/`OnNotification` are Linux-only (logind
/// D-Bus signals / notification-bus eavesdropping - see `app`'s trigger
/// loops), `OnKeyPress` needs X11 or Windows/macOS (no Wayland global
/// hotkey API exists - see PLAN.md). None of that is encoded in the type
/// itself; a rule using an unsupported-on-this-platform trigger simply
/// never fires here, same as a `MicActive` rule would on a platform with
/// no mic provider.
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Trigger {
    MicActive,
    /// The falling edge of the same signal `MicActive` watches for the
    /// rising edge of - "the mic just stopped being used", fired at most
    /// once per deactivation, same as `MicActive` fires at most once per
    /// activation.
    MicInactive,
    /// Rising/falling edge of "some process has the camera open" - same
    /// `core::camera` provider shape as `core::mic`, see its own doc
    /// comment for the per-OS detection story and gaps.
    CameraActive,
    CameraInactive,
    OnBoot,
    OnBeforeSleep,
    BeforeShutdown,
    OnNotification,
    /// `app_name` matches a running process's name, case-insensitively
    /// (see `app`'s `sysinfo`-based poller) - not a full path, just the
    /// executable name as the OS reports it (e.g. "firefox", "steam.exe").
    OnAppOpen { app_name: String },
    /// A `global-hotkey` combo string, e.g. `"control+alt+KeyL"` - see
    /// `app`'s key-press trigger loop for the exact format (modifiers
    /// lowercase + `+`, then a `keyboard_types::Code` name).
    OnKeyPress { key: String },
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct Rule {
    pub id: Uuid,
    pub name: String,
    pub enabled: bool,
    pub trigger: Trigger,
    /// Lamp keys (same identifier `LampSummary`/`ScannedLamp` use over
    /// IPC), not live handles - a rule must survive the daemon restarting
    /// and lamps connecting/disconnecting in between.
    pub targets: Vec<String>,
    pub action: Action,
}
