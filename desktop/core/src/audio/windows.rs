//! Windows system-audio loudness capture via WASAPI loopback, through
//! `cpal` rather than hand-rolled WASAPI FFI - `cpal`'s Windows backend
//! already handles the loopback-mode detail: calling `build_input_stream`
//! on the *default output* device (not an input device) makes it add
//! `AUDCLNT_STREAMFLAGS_LOOPBACK` internally (confirmed by reading cpal's
//! own WASAPI backend source, not guessed), which is exactly the
//! "`device.build_input_stream` on an output device" pattern below.
//!
//! Unlike `core::mic::windows`/`core::camera::windows` (a registry read,
//! fairly low-risk even unverified) this is a real audio stream with a
//! background callback thread `cpal` manages itself - more moving parts,
//! still unverified since no Windows target exists in this dev
//! environment, but built against `cpal`'s real, published API rather
//! than raw WASAPI COM interop.

use std::sync::{Arc, Mutex};

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{SampleFormat, Stream};

use super::LoudnessProvider;

/// `(sum of squares, sample count)` since the last `poll()` - updated from
/// cpal's own audio callback thread, read and reset from `poll()`.
type Accumulator = Arc<Mutex<(f64, usize)>>;

pub struct WasapiLoudnessProvider {
    stream: Option<Stream>,
    accumulator: Accumulator,
}

impl Default for WasapiLoudnessProvider {
    fn default() -> Self {
        Self { stream: None, accumulator: Arc::new(Mutex::new((0.0, 0))) }
    }
}

impl WasapiLoudnessProvider {
    pub fn new() -> Self {
        Self::default()
    }

    fn ensure_stream(&mut self) -> Result<(), String> {
        if self.stream.is_some() {
            return Ok(());
        }
        let host = cpal::default_host();
        let device = host.default_output_device().ok_or("no default output device")?;
        let supported = device
            .default_input_config()
            .map_err(|e| format!("could not get the loopback stream format: {e}"))?;
        if supported.sample_format() != SampleFormat::F32 {
            // The vast majority of WASAPI shared-mode mix formats are
            // F32; handling every other format cpal exposes would be a
            // lot of untestable branches for a case that's very unlikely
            // to actually occur.
            return Err(format!(
                "unexpected loopback sample format {:?} (only F32 is handled)",
                supported.sample_format()
            ));
        }
        let config = supported.config();
        let accumulator = Arc::clone(&self.accumulator);
        let stream = device
            .build_input_stream(
                config,
                move |data: &[f32], _: &cpal::InputCallbackInfo| {
                    let mut acc = accumulator.lock().expect("accumulator mutex should not be poisoned");
                    for &sample in data {
                        acc.0 += f64::from(sample) * f64::from(sample);
                        acc.1 += 1;
                    }
                },
                |err| eprintln!("[visualizer] WASAPI loopback stream error: {err}"),
                None,
            )
            .map_err(|e| format!("failed to build the loopback stream: {e}"))?;
        stream.play().map_err(|e| format!("failed to start the loopback stream: {e}"))?;
        self.stream = Some(stream);
        Ok(())
    }
}

impl LoudnessProvider for WasapiLoudnessProvider {
    fn poll(&mut self) -> Result<Option<f32>, String> {
        if let Err(e) = self.ensure_stream() {
            self.stream = None;
            return Err(e);
        }
        let mut acc = self.accumulator.lock().expect("accumulator mutex should not be poisoned");
        if acc.1 == 0 {
            return Ok(None);
        }
        let rms = ((acc.0 / acc.1 as f64).sqrt()) as f32;
        *acc = (0.0, 0);
        Ok(Some(rms))
    }
}
