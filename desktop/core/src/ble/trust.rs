//! Minimal on-disk token cache, keyed by a stable per-lamp identifier.
//!
//! This is intentionally simple (one file per lamp, plain text) - M4's
//! config module will replace it with the real `lamps.toml` registry
//! (friendly name, last-known BLE id, token, all together). Kept separate
//! and trait-free for now since the probe (M1) is the only consumer; wire
//! it behind a trait if/when the GUI needs a different backend (e.g. the
//! OS keyring).

use std::fs;
use std::path::PathBuf;

pub struct FileTokenStore {
    dir: PathBuf,
}

impl FileTokenStore {
    pub fn new(dir: impl Into<PathBuf>) -> std::io::Result<Self> {
        let dir = dir.into();
        fs::create_dir_all(&dir)?;
        Ok(Self { dir })
    }

    pub fn load(&self, lamp_key: &str) -> Option<String> {
        fs::read_to_string(self.path_for(lamp_key))
            .ok()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
    }

    pub fn save(&self, lamp_key: &str, token: &str) -> std::io::Result<()> {
        fs::write(self.path_for(lamp_key), token)
    }

    pub fn clear(&self, lamp_key: &str) {
        let _ = fs::remove_file(self.path_for(lamp_key));
    }

    fn path_for(&self, lamp_key: &str) -> PathBuf {
        let safe: String = lamp_key
            .chars()
            .map(|c| if c.is_alphanumeric() { c } else { '_' })
            .collect();
        self.dir.join(format!("{safe}.token"))
    }
}
