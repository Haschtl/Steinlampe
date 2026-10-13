//! System-audio *loudness* capture for the music-reactive visualizer - not
//! a microphone, a *loopback* of whatever the system is currently playing
//! (Spotify, a browser tab, anything). Deliberately scoped to loudness
//! only (RMS), not full FFT/beat-detection - see PLAN.md's "Visualizer
//! scope" note for why: it's the simplest thing that's genuinely "music-
//! reactive" without a DSP pipeline that can't be verified without real
//! audio test signals.

pub mod unsupported;

#[cfg(target_os = "linux")]
pub mod linux;
#[cfg(target_os = "windows")]
pub mod windows;

pub trait LoudnessProvider {
    /// Returns the RMS loudness (unscaled, typically well under `1.0` for
    /// normal listening volumes - the caller applies its own gain) of
    /// whatever audio arrived since the last call. `Ok(None)` means no new
    /// samples arrived this tick (nothing is playing, or the poll is
    /// faster than the audio buffer fills) - not an error, and distinct
    /// from `Err`, which means detection itself failed.
    fn poll(&mut self) -> Result<Option<f32>, String>;
}

/// Linux (PulseAudio/PipeWire monitor-source capture, verified live) and
/// Windows (WASAPI loopback via `cpal`, which handles the loopback-mode
/// details for us - unverified, no Windows target here) have real
/// backends. macOS has none: unlike WASAPI, CoreAudio has no built-in
/// loopback capture mode at all - the usual workarounds (a virtual
/// loopback driver like BlackHole, or `ScreenCaptureKit`'s audio capture
/// on macOS 13+) are both a meaningfully bigger, separate piece of work,
/// not a small gap to paper over blind.
pub fn default_provider() -> Box<dyn LoudnessProvider> {
    #[cfg(target_os = "linux")]
    {
        Box::new(linux::PulseLoudnessProvider::new())
    }
    #[cfg(target_os = "windows")]
    {
        Box::new(windows::WasapiLoudnessProvider::new())
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    {
        Box::new(unsupported::UnsupportedLoudnessProvider)
    }
}
