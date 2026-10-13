//! Stub for platforms without a real mic-activity backend yet (Windows,
//! macOS - see PLAN.md's M5 note on both being unverified/unwritten).
//! Always returns `Err`, never a fabricated `Inactive` - the evaluation
//! loop must not mistake "unsupported" for "mic is off".

use super::{MicActivity, MicActivityProvider};

pub struct UnsupportedMicProvider;

impl MicActivityProvider for UnsupportedMicProvider {
    fn poll(&mut self) -> Result<MicActivity, String> {
        Err("mic-active detection is not implemented on this platform yet".to_string())
    }
}
