//! macOS camera-active detection via CoreMediaIO's
//! `kCMIODevicePropertyDeviceIsRunningSomewhere` - the camera-side sibling
//! of the CoreAudio call `core::mic::macos` uses. This module used to not
//! exist: the exact `FourCharCode` property-selector constants couldn't be
//! verified or confidently recalled from this Linux dev environment, and a
//! wrong guess would have compiled while silently always reporting
//! "inactive" forever - worse than shipping nothing. That blocker is gone:
//! `objc2-core-media-io` is a real, published, actively-maintained binding
//! crate (the CoreMediaIO sibling of `objc2-core-audio`, used by
//! `core::mic::macos`) whose constants are generated directly from
//! Apple's actual headers, so nothing here is a guess.
//!
//! Unlike CoreAudio, CoreMediaIO has no "default input device" concept - a
//! camera isn't "the" input the way a mic is. This enumerates every CMIO
//! device via `kCMIOHardwarePropertyDevices` on the system object and
//! reports `Active` if *any* of them is running somewhere (mirrors how
//! several simultaneous video calls would compose - one camera in use is
//! enough to call it "the camera is active").

use std::ffi::c_void;
use std::ptr;

use objc2_core_media_io::{
    kCMIODevicePropertyDeviceIsRunningSomewhere, kCMIOHardwarePropertyDevices, kCMIOObjectPropertyElementMain,
    kCMIOObjectPropertyScopeGlobal, kCMIOObjectSystemObject, CMIOObjectGetPropertyData, CMIOObjectGetPropertyDataSize,
    CMIOObjectID, CMIOObjectPropertyAddress,
};

use super::{CameraActivity, CameraActivityProvider};

fn property_data_size(object_id: CMIOObjectID, address: &CMIOObjectPropertyAddress) -> Result<u32, String> {
    let mut size: u32 = 0;
    // SAFETY: `address` is a valid reference for the duration of this
    // call; `size` is a valid out-pointer for the one u32 this call writes.
    let status = unsafe { CMIOObjectGetPropertyDataSize(object_id, address, 0, ptr::null(), &mut size) };
    if status != 0 {
        return Err(format!("CMIOObjectGetPropertyDataSize failed with OSStatus {status}"));
    }
    Ok(size)
}

fn device_ids() -> Result<Vec<CMIOObjectID>, String> {
    let address = CMIOObjectPropertyAddress {
        mSelector: kCMIOHardwarePropertyDevices,
        mScope: kCMIOObjectPropertyScopeGlobal,
        mElement: kCMIOObjectPropertyElementMain,
    };
    let size = property_data_size(kCMIOObjectSystemObject, &address)?;
    let count = size as usize / std::mem::size_of::<CMIOObjectID>();
    if count == 0 {
        return Ok(Vec::new());
    }
    let mut devices: Vec<CMIOObjectID> = vec![0; count];
    let mut data_used: u32 = 0;
    // SAFETY: `devices` has exactly `size` bytes of capacity (just measured
    // above) and lives for the duration of this call; `data_used` is a
    // valid out-pointer.
    let status = unsafe {
        CMIOObjectGetPropertyData(
            kCMIOObjectSystemObject,
            &address,
            0,
            ptr::null(),
            size,
            &mut data_used,
            devices.as_mut_ptr() as *mut c_void,
        )
    };
    if status != 0 {
        return Err(format!("CMIOObjectGetPropertyData (device list) failed with OSStatus {status}"));
    }
    Ok(devices)
}

fn is_running_somewhere(device_id: CMIOObjectID) -> Result<bool, String> {
    let address = CMIOObjectPropertyAddress {
        mSelector: kCMIODevicePropertyDeviceIsRunningSomewhere,
        mScope: kCMIOObjectPropertyScopeGlobal,
        mElement: kCMIOObjectPropertyElementMain,
    };
    let mut value: u32 = 0;
    let mut data_used: u32 = 0;
    let size = std::mem::size_of::<u32>() as u32;
    // SAFETY: `value`/`data_used` are valid, correctly-sized out-pointers
    // for the duration of this one call.
    let status = unsafe {
        CMIOObjectGetPropertyData(
            device_id,
            &address,
            0,
            ptr::null(),
            size,
            &mut data_used,
            &mut value as *mut u32 as *mut c_void,
        )
    };
    if status != 0 {
        return Err(format!("CMIOObjectGetPropertyData (is-running) failed with OSStatus {status}"));
    }
    Ok(value != 0)
}

#[derive(Default)]
pub struct MacosCameraProvider;

impl MacosCameraProvider {
    pub fn new() -> Self {
        Self
    }
}

impl CameraActivityProvider for MacosCameraProvider {
    fn poll(&mut self) -> Result<CameraActivity, String> {
        for device_id in device_ids()? {
            if is_running_somewhere(device_id)? {
                return Ok(CameraActivity::Active);
            }
        }
        Ok(CameraActivity::Inactive)
    }
}
