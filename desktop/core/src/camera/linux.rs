//! Linux camera-active detection: scan every process's open file
//! descriptors for one pointing at a `/dev/video*` (V4L2) device node -
//! the same technique `fuser /dev/video0` uses under the hood. There's no
//! PulseAudio-equivalent broker daemon for cameras in the general case
//! (V4L2 apps open the device node directly), so unlike `core::mic::linux`
//! this can't piggyback on an existing client API - it's a plain `/proc`
//! walk instead.
//!
//! Needs no new dependency - just `std::fs`. The one real limitation:
//! `/proc/<pid>/fd/*` is only readable for processes owned by the same
//! user (or as root) - a camera held open by another user's process on a
//! shared multi-user machine would be invisible here. Acceptable for the
//! personal-desktop case this app targets; silently skipped (not an
//! error) rather than failing the whole poll.

use std::fs;

use super::{CameraActivity, CameraActivityProvider};

fn any_camera_open() -> bool {
    let Ok(procs) = fs::read_dir("/proc") else { return false };
    for proc_entry in procs.flatten() {
        let is_pid = proc_entry.file_name().to_str().is_some_and(|s| s.chars().all(|c| c.is_ascii_digit()));
        if !is_pid {
            continue;
        }
        let fd_dir = proc_entry.path().join("fd");
        let Ok(fds) = fs::read_dir(&fd_dir) else { continue };
        for fd_entry in fds.flatten() {
            let Ok(target) = fs::read_link(fd_entry.path()) else { continue };
            let is_video_device = target
                .parent()
                .map(|p| p == std::path::Path::new("/dev"))
                .unwrap_or(false)
                && target.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.starts_with("video"));
            if is_video_device {
                return true;
            }
        }
    }
    false
}

#[derive(Default)]
pub struct ProcFdCameraProvider;

impl ProcFdCameraProvider {
    pub fn new() -> Self {
        Self
    }
}

impl CameraActivityProvider for ProcFdCameraProvider {
    fn poll(&mut self) -> Result<CameraActivity, String> {
        Ok(if any_camera_open() { CameraActivity::Active } else { CameraActivity::Inactive })
    }
}
