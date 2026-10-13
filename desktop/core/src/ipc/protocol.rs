//! The wire protocol between the daemon (`app`, which owns BLE/rules/mic -
//! see PLAN.md's "Decisions already made") and the console (`console`, a
//! thin client). Newline-delimited JSON over loopback TCP - chosen over a
//! Unix-socket/named-pipe crate or D-Bus specifically for identical
//! cross-platform behavior via plain `tokio::net`, since none of this can be
//! tested on Windows/macOS from this dev environment.
//!
//! Request/response only for now (no server-push events) - the console
//! polls `ListLamps` periodically. Fine for v1's "on/off only" scope; a
//! push channel can be added later without touching these types if polling
//! latency ever matters enough to justify it.

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::rules::model::Rule;

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct IpcRequest {
    /// Local hygiene, not a real security boundary (see PLAN.md) - just
    /// stops some other process on the same machine from issuing commands.
    pub auth_token: String,
    pub kind: RequestKind,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum RequestKind {
    /// Lamps already connected (or attempting to), with live state.
    ListLamps,
    /// Scan for nearby lamps not yet in the connected set.
    Scan { seconds: u64 },
    /// Start connecting to a lamp found by a previous `Scan`.
    Connect { lamp_key: String, name: String },
    /// Send a raw command line to an already-connected lamp (e.g. "on").
    SendCommand { lamp_key: String, line: String },
    Disconnect { lamp_key: String },
    ListRules,
    /// Create (if `rule.id` is new) or update (if it matches an existing
    /// rule's id) a rule, and persist the whole list.
    SaveRule { rule: Rule },
    DeleteRule { id: Uuid },
    /// Dispatches a rule's action to its targets right now, bypassing
    /// trigger detection entirely - validates the Rule -> Action -> lamp
    /// dispatch pipeline independent of any real trigger (M4's "manually
    /// fired" scope; trigger detection itself is M5).
    FireRule { id: Uuid },
    /// Starts (or updates, if already running) the music-reactive
    /// visualizer against `config.targets`; `None` stops it. Not
    /// persisted anywhere - a live mode, not a rule, reset to off on
    /// every daemon restart.
    SetVisualizer { config: Option<VisualizerConfig> },
    GetVisualizer,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum IpcResponse {
    Ok,
    Error { message: String },
    Lamps { lamps: Vec<LampSummary> },
    ScanResults { lamps: Vec<ScannedLamp> },
    Rules { rules: Vec<Rule> },
    Visualizer { config: Option<VisualizerConfig>, last_error: Option<String> },
}

/// Loudness (RMS of system audio) drives brightness, linearly, between
/// `min_brightness` and `max_brightness` - `gain` scales the raw RMS
/// (typically well under `1.0`) up before that mapping, since "unscaled"
/// loudness would barely move the lamp at normal listening volumes. See
/// `core::audio`'s doc comment for why it's loudness-only, not full
/// FFT/beat-detection.
#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct VisualizerConfig {
    pub targets: Vec<String>,
    pub gain: f32,
    pub min_brightness: u8,
    pub max_brightness: u8,
}

/// Wire-friendly view of `core::state::LampState` plus identity - built
/// fresh from a `LampHandle` on each request rather than reusing
/// `LampState` directly, since that type holds an `Instant` (not
/// serializable, and meaningless across a process boundary anyway).
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct LampSummary {
    pub lamp_key: String,
    pub name: String,
    pub connection: String,
    pub lamp_on: Option<bool>,
    pub brightness_pct: Option<f32>,
    pub pattern: Option<u32>,
    pub last_error: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ScannedLamp {
    pub lamp_key: String,
    pub name: String,
}
