//! Camera-active detection - same shape as `core::mic`, so the same
//! `CameraActive`/`CameraInactive` rising/falling-edge handling in `app`'s
//! evaluation loop can reuse the pattern verbatim. The real "on-air" use
//! case this whole project started from (Zoom/Teams/Meet are using the
//! camera) is arguably a better fit for this trigger than mic-active is,
//! since a meeting usually turns the camera on even when muted.

pub mod unsupported;

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "macos")]
pub mod macos;
#[cfg(target_os = "windows")]
pub mod windows;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CameraActivity {
    Active,
    Inactive,
}

pub trait CameraActivityProvider {
    /// Same contract as `mic::MicActivityProvider::poll` - called
    /// periodically from a dedicated thread, `Err` must never be treated
    /// as `Inactive`.
    fn poll(&mut self) -> Result<CameraActivity, String>;
}

/// Linux (scanning `/proc/*/fd` for open `/dev/video*` handles - verified
/// live), Windows (the same registry `ConsentStore` heuristic as
/// `core::mic::windows`, just under `\webcam` instead of `\microphone` -
/// unverified, no Windows target here), and macOS (CoreMediaIO's
/// `kCMIODevicePropertyDeviceIsRunningSomewhere`, via `objc2-core-media-io`'s
/// published bindings - also unrun, no macOS target here) all have real
/// backends - see each module's own doc comment for exactly how unverified.
pub fn default_provider() -> Box<dyn CameraActivityProvider> {
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::ProcFdCameraProvider::new())
    }
    #[cfg(target_os = "windows")]
    {
        Box::new(windows::WindowsCameraProvider::new())
    }
    #[cfg(target_os = "macos")]
    {
        Box::new(macos::MacosCameraProvider::new())
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows", target_os = "macos")))]
    {
        Box::new(unsupported::UnsupportedCameraProvider)
    }
}
