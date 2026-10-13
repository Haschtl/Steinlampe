//! `Action` only, for now - the console's manual quick-actions (M3) are the
//! first consumer. `Trigger`/`Rule` (the "when X happens, do Y" automation
//! layer) are M4's job; adding them now, unused, would be exactly the kind
//! of speculative code this project avoids elsewhere. The shape sketched in
//! PLAN.md still holds: `Trigger`/`Rule` will reference `Action` the same
//! way, no rework expected here when M4 lands.

use serde::{Deserialize, Serialize};

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
