//! Windows camera-active detection - the exact same registry mechanism as
//! `core::mic::windows`, just under `...\ConsentStore\webcam` instead of
//! `...\microphone`. Same confidence level: `winreg` API read from its
//! real source, never compiled (no Windows target available here).

use winreg::enums::HKEY_CURRENT_USER;
use winreg::RegKey;

use super::{CameraActivity, CameraActivityProvider};

const CONSENT_STORE_PATH: &str = r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam";

fn any_active_under(key: &RegKey) -> bool {
    for name in key.enum_keys().filter_map(Result::ok) {
        let Ok(sub) = key.open_subkey(&name) else { continue };
        if name.eq_ignore_ascii_case("NonPackaged") {
            if any_active_under(&sub) {
                return true;
            }
            continue;
        }
        if let Ok(stop) = sub.get_value::<u64, _>("LastUsedTimeStop")
            && stop == 0
        {
            return true;
        }
    }
    false
}

#[derive(Default)]
pub struct WindowsCameraProvider;

impl WindowsCameraProvider {
    pub fn new() -> Self {
        Self
    }
}

impl CameraActivityProvider for WindowsCameraProvider {
    fn poll(&mut self) -> Result<CameraActivity, String> {
        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        let key = hkcu
            .open_subkey(CONSENT_STORE_PATH)
            .map_err(|e| format!("could not open the webcam ConsentStore registry key: {e}"))?;
        Ok(if any_active_under(&key) { CameraActivity::Active } else { CameraActivity::Inactive })
    }
}
