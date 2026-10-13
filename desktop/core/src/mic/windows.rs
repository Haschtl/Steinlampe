//! Windows mic-active detection via the same registry location Windows'
//! own privacy dashboard ("Microphone" under Settings > Privacy) reads
//! from: `HKCU\...\CapabilityAccessManager\ConsentStore\microphone`. Each
//! app that has ever used the microphone gets a subkey there (desktop/
//! "classic" apps nest one level deeper under a `NonPackaged` subkey,
//! UWP/packaged apps sit directly under `microphone`) holding
//! `LastUsedTimeStart`/`LastUsedTimeStop` (Windows `FILETIME`, as a
//! REG_QWORD) - `LastUsedTimeStop == 0` means that app is using the mic
//! *right now*, not that it stopped at time zero. This is the same
//! documented heuristic PLAN.md called for; it misses legacy/driver-level
//! captures that bypass this consent-tracking layer entirely (there's no
//! lower-level portable signal to fall back to without admin rights).
//!
//! Written against the `winreg` API read directly from its source (not
//! from memory) but never compiled - no Windows target available in this
//! dev environment. Treat this module as the least-verified mic backend
//! in the project, more so than the usual "written but unrun" caveat.

use winreg::enums::HKEY_CURRENT_USER;
use winreg::RegKey;

use super::{MicActivity, MicActivityProvider};

const CONSENT_STORE_PATH: &str = r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone";

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
pub struct WindowsMicProvider;

impl WindowsMicProvider {
    pub fn new() -> Self {
        Self
    }
}

impl MicActivityProvider for WindowsMicProvider {
    fn poll(&mut self) -> Result<MicActivity, String> {
        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        let key = hkcu
            .open_subkey(CONSENT_STORE_PATH)
            .map_err(|e| format!("could not open the microphone ConsentStore registry key: {e}"))?;
        Ok(if any_active_under(&key) { MicActivity::Active } else { MicActivity::Inactive })
    }
}
