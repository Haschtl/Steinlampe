//! Camera-active detection - same shape as `core::mic`, so the same
//! `CameraActive`/`CameraInactive` rising/falling-edge handling in `app`'s
//! evaluation loop can reuse the pattern verbatim. The real "on-air" use
//! case this whole project started from (Zoom/Teams/Meet are using the
//! camera) is arguably a better fit for this trigger than mic-active is,
//! since a meeting usually turns the camera on even when muted.

pub mod unsupported;

#[cfg(target_os = "linux")]
pub mod linux;
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
/// live) and Windows (the same registry `ConsentStore` heuristic as
/// `core::mic::windows`, just under `\webcam` instead of `\microphone` -
/// unverified, no Windows target here) have real backends.
///
/// macOS deliberately has none: the equivalent CoreMediaIO API
/// (`CMIOObjectGetPropertyData` + `kCMIODevicePropertyDeviceIsRunningSomewhere`,
/// the camera-side sibling of the CoreAudio call `core::mic::macos` uses)
/// needs exact `FourCharCode` property-selector constants that - unlike
/// CoreAudio's, which are ubiquitous in tutorials and sample code - aren't
/// something that could be verified or even recalled with real confidence
/// here. Shipping a guessed constant would be worse than not shipping
/// anything: it would compile, query the *wrong* property, and silently
/// report "inactive" forever with no error - a different and worse
/// failure mode than the usual "written but unverified" caveat elsewhere
/// in this project. Left unimplemented until someone can check the real
/// constants against Apple's headers.
pub fn default_provider() -> Box<dyn CameraActivityProvider> {
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::ProcFdCameraProvider::new())
    }
    #[cfg(target_os = "windows")]
    {
        Box::new(windows::WindowsCameraProvider::new())
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    {
        Box::new(unsupported::UnsupportedCameraProvider)
    }
}
