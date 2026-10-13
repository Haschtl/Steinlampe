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
//! This is, by a clear margin, the least verifiable code in this project:
//! `coreaudio-sys` generates its bindings via `bindgen` against the real
//! macOS SDK *at build time*, which only happens on an actual Mac (or this
//! GitHub Actions `macos-latest` runner) - there is no local way to even
//! check the generated symbol names, let alone compile this, from this
//! Linux dev environment. The C API used below (`AudioObjectGetPropertyData`,
//! the `AudioObjectPropertyAddress` struct, the `kAudioHardwareProperty...`/
//! `kAudioDeviceProperty...` constants) has been stable in CoreAudio since
//! Mac OS X 10.4 and is written from direct knowledge of that C API, not
//! guessed - but expect this specific file, more than any other
//! cross-platform code here, to need a real fix-up pass against actual CI
//! feedback once it runs on a real Mac.

use std::os::raw::c_void;
use std::ptr;

use coreaudio_sys::{
    kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal,
    kAudioObjectSystemObject, AudioDeviceID, AudioObjectGetPropertyData, AudioObjectPropertyAddress,
};

use super::{MicActivity, MicActivityProvider};

/// `mElement` for a whole-device (not per-channel) property. Using the raw
/// value `0` rather than the named `kAudioObjectPropertyElementMain`/
/// `...ElementMaster` constant on purpose - Apple renamed it between SDK
/// versions and both names are numerically `0`, so this sidesteps betting
/// on which one `coreaudio-sys` exposes for whatever SDK the build machine
/// has.
const ELEMENT_MAIN: u32 = 0;

fn get_u32_property(object_id: AudioDeviceID, selector: u32) -> Result<u32, String> {
    let address = AudioObjectPropertyAddress {
        mSelector: selector,
        mScope: kAudioObjectPropertyScopeGlobal,
        mElement: ELEMENT_MAIN,
    };
    let mut value: u32 = 0;
    let mut size = std::mem::size_of::<u32>() as u32;
    // SAFETY: `address` describes a fixed-size u32 property; `value`/`size`
    // are valid, correctly-sized out-pointers for the duration of this one
    // call, matching `AudioObjectGetPropertyData`'s documented contract.
    let status = unsafe {
        AudioObjectGetPropertyData(
            object_id,
            &address,
            0,
            ptr::null(),
            &mut size,
            &mut value as *mut u32 as *mut c_void,
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
            get_u32_property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice)?;
        let running = get_u32_property(default_input, kAudioDevicePropertyDeviceIsRunningSomewhere)?;
        Ok(if running != 0 { MicActivity::Active } else { MicActivity::Inactive })
    }
}
