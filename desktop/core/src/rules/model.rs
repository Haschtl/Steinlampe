//! `Action` (M3) and now `Trigger`/`Rule` (M4) - the "when X happens, do Y
//! to lamp(s) Z" automation layer. `Trigger` only has `MicActive` so far
//! (the one concrete trigger the user asked for); `Notification`/future
//! triggers slot in later as new enum variants with no restructuring of
//! `Rule` or its persistence (see PLAN.md's "Explicitly out of scope for
//! v1").

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
}

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Trigger {
    MicActive,
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
