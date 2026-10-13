//! macOS mic-active detection via CoreAudio's
//! `kAudioDevicePropertyDeviceIsRunningSomewhere` on the system's default
//! input device - the same community-documented heuristic PLAN.md called
//! for (there is no public, documented "who is recording" API; the
//! orange-dot privacy indicator itself uses private TCC internals Apple
//! doesn't expose). "Running somewhere" means *some* process has an active
//! IO stream on that device, not necessarily one actually producing
//! audible/recorded output - close enough for "is the mic in use" in
//! practice, and the same tradeoff every other open-source tool doing this
//! check makes.
//!
//! Built on `objc2-core-audio` rather than hand-rolled `coreaudio-sys` FFI
//! (an earlier version of this file used that, flagged at the time as "the
//! least verifiable code in this project" since `coreaudio-sys` generates
//! its bindings via `bindgen` fresh against whatever SDK the build machine
//! happens to have). `objc2-core-audio` ships pre-generated, versioned
//! bindings maintained by the `objc2` project - the same one most of the
//! Rust macOS ecosystem already depends on for Cocoa/AppKit - so every
//! name used below (confirmed against that crate's published docs, not
//! typed from memory) is checked against a real artifact rather than
//! generated fresh per build. The underlying C API itself
//! (`AudioObjectGetPropertyData`, `AudioObjectPropertyAddress`, the
//! `kAudioHardwareProperty...`/`kAudioDeviceProperty...` constants) has
//! been stable in CoreAudio since Mac OS X 10.4.

use std::ffi::c_void;
use std::ptr;
use std::ptr::NonNull;

use objc2_core_audio::{
    kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioHardwarePropertyDefaultInputDevice,
    kAudioObjectPropertyElementMain, kAudioObjectPropertyScopeGlobal, kAudioObjectSystemObject,
    AudioObjectGetPropertyData, AudioObjectID, AudioObjectPropertyAddress,
};

use super::{MicActivity, MicActivityProvider};

fn get_u32_property(object_id: AudioObjectID, selector: u32) -> Result<u32, String> {
    let address = AudioObjectPropertyAddress {
        mSelector: selector,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: kAudioObjectPropertyElementMain,
    };
    let mut value: u32 = 0;
    let mut size = std::mem::size_of::<u32>() as u32;
    // SAFETY: `address` lives for the duration of this call; `size`/
    // `value` are valid, correctly-sized out-pointers for the one u32 this
    // call reads the size of and writes into, matching
    // `AudioObjectGetPropertyData`'s documented contract.
    let status = unsafe {
        AudioObjectGetPropertyData(
            object_id,
            NonNull::from(&address),
            0,
            ptr::null(),
            NonNull::from(&mut size),
            NonNull::from(&mut value).cast::<c_void>(),
        )
    };
    if status != 0 {
        return Err(format!("AudioObjectGetPropertyData failed with OSStatus {status}"));
    }
    Ok(value)
}

#[derive(Default)]
pub struct MacosMicProvider;

impl MacosMicProvider {
    pub fn new() -> Self {
        Self
    }
}

impl MicActivityProvider for MacosMicProvider {
    fn poll(&mut self) -> Result<MicActivity, String> {
        let default_input =
            get_u32_property(kAudioObjectSystemObject as AudioObjectID, kAudioHardwarePropertyDefaultInputDevice)?;
        let running = get_u32_property(default_input, kAudioDevicePropertyDeviceIsRunningSomewhere)?;
        Ok(if running != 0 { MicActivity::Active } else { MicActivity::Inactive })
    }
}
