//! Stub for platforms without a real camera-activity backend (currently:
//! everything except Linux and Windows - see `mod.rs`'s doc comment for
//! why macOS specifically is skipped rather than guessed at).

use super::{CameraActivity, CameraActivityProvider};

pub struct UnsupportedCameraProvider;

impl CameraActivityProvider for UnsupportedCameraProvider {
    fn poll(&mut self) -> Result<CameraActivity, String> {
        Err("camera-active detection is not implemented on this platform yet".to_string())
    }
}
