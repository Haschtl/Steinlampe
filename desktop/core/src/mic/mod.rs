//! Mic-active detection, the one `Trigger` variant M5 wires up end to end
//! (see PLAN.md's "Mic-active detection" section). `MicActivityProvider` is
//! a plain, non-async, non-`Send` trait on purpose: every real backend
//! (PulseAudio's client API here, Windows' registry, CoreAudio) wraps a
//! platform handle that has to stay on the one thread that created it, so
//! a provider is built *inside* its dedicated polling thread (see `app`'s
//! evaluation loop), never constructed on one thread and moved to another.

pub mod unsupported;

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "macos")]
pub mod macos;
#[cfg(target_os = "windows")]
pub mod windows;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MicActivity {
    Active,
    Inactive,
}

pub trait MicActivityProvider {
    /// Called every ~300ms from a dedicated blocking thread - cheap, and
    /// allowed to block briefly, but never for long. `Err` means detection
    /// failed or isn't implemented on this platform; the caller must never
    /// treat that the same as `Ok(Inactive)` - see the evaluation loop's
    /// edge detection, which only ever fires on an observed `Inactive` ->
    /// `Active` transition, never by assuming a failed poll means off.
    fn poll(&mut self) -> Result<MicActivity, String>;
}

/// The best provider for this platform. Linux is the only one verified
/// live; Windows (registry `ConsentStore` heuristic) and macOS (CoreAudio
/// `...IsRunningSomewhere`) are written against each platform's documented
/// mechanism but unrun - see each module's own doc comment for exactly how
/// unverified, which differs a lot between the two (Windows' `winreg` API
/// was at least read from its real source; macOS's `coreaudio-sys` bindings
/// are generated at build time from the real SDK and can't be inspected at
/// all without a Mac).
pub fn default_provider() -> Box<dyn MicActivityProvider> {
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::PulseMicProvider::new())
    }
    #[cfg(target_os = "windows")]
    {
        Box::new(windows::WindowsMicProvider::new())
    }
    #[cfg(target_os = "macos")]
    {
        Box::new(macos::MacosMicProvider::new())
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows", target_os = "macos")))]
    {
        Box::new(unsupported::UnsupportedMicProvider)
    }
}
