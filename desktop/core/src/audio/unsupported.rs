//! Stub for platforms without a loopback-capture backend (currently:
//! everything except Linux and Windows - see `mod.rs`'s doc comment for
//! why macOS specifically is a bigger problem than "just unverified").

use super::LoudnessProvider;

pub struct UnsupportedLoudnessProvider;

impl LoudnessProvider for UnsupportedLoudnessProvider {
    fn poll(&mut self) -> Result<Option<f32>, String> {
        Err("system-audio loudness capture is not implemented on this platform yet".to_string())
    }
}
