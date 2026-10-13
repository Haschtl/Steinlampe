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
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum IpcResponse {
    Ok,
    Error { message: String },
    Lamps { lamps: Vec<LampSummary> },
    ScanResults { lamps: Vec<ScannedLamp> },
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
