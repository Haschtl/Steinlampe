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

/// The best provider for this platform - `unsupported` everywhere until a
/// real one is written for it (only Linux so far; see PLAN.md's M5 note on
/// Windows/macOS being unverified/unwritten).
pub fn default_provider() -> Box<dyn MicActivityProvider> {
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::PulseMicProvider::new())
    }
    #[cfg(not(target_os = "linux"))]
    {
        Box::new(unsupported::UnsupportedMicProvider)
    }
}
