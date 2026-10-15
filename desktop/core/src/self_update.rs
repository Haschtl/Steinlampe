//! Downloads the platform-specific asset `update::check_for_update` found
//! and swaps it into the currently running install, then relaunches.
//!
//! This is the deliberate, risk-accepting counterpart to `update.rs`'s own
//! doc comment (which explains why that module stays notify-only): there is
//! still no real code signing (ad-hoc on macOS, optional/inactive SignPath
//! on Windows), so this trusts the downloaded file no further than plain
//! HTTPS + GitHub already does. Manual download from the release page
//! (`UpdateInfo::html_url`) always remains the fallback if this fails.
//!
//! Same-directory download-then-rename everywhere a rename is possible
//! (Linux/macOS: directly, since Unix allows replacing a file that's still
//! mapped/running; macOS additionally re-signs the bundle afterward, since
//! swapping files inside it invalidates the outer seal `build-dmg.sh`
//! created). Windows can't overwrite its own running .exe at all, so that
//! path stages the new files under a `.new` suffix and hands off to a
//! detached helper that waits for this process to exit before finishing the
//! swap and relaunching - the same "wait for this PID to vanish, then
//! relaunch" trick is used on every platform for the *relaunch* step
//! specifically (not just Windows), to avoid a race against the tray
//! daemon's single-instance TCP listener (`app::acquire_single_instance_lock`):
//! spawning the replacement before this process has actually exited and
//! released the port would make the new instance think another one is
//! already running and quit immediately.
//!
//! Unverified beyond compiling on Linux - no Windows or macOS machine in
//! this dev environment to actually exercise either of those branches.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Downloads `asset_url` (the matching platform asset from the GitHub
/// release `update::check_for_update` already found) and swaps it into the
/// currently running install. On success, the caller should shut down and
/// exit this process shortly after - the new version is either already
/// running in its place (Linux/macOS) or waiting for this process to exit
/// before it finishes taking over (Windows).
pub fn apply_update(asset_url: &str) -> Result<(), String> {
    #[cfg(target_os = "linux")]
    return apply_update_linux(asset_url);
    #[cfg(target_os = "macos")]
    return apply_update_macos(asset_url);
    #[cfg(windows)]
    return apply_update_windows(asset_url);
    #[cfg(not(any(target_os = "linux", target_os = "macos", windows)))]
    {
        let _ = asset_url;
        Err("self-update is not implemented for this platform".to_string())
    }
}

fn download_to_file(url: &str, dest: &Path) -> Result<(), String> {
    let response = ureq::get(url)
        .header("User-Agent", "quarzlampe-desktop")
        .call()
        .map_err(|e| format!("download failed: {e}"))?;
    let mut reader = response.into_body().into_reader();
    let mut file = fs::File::create(dest).map_err(|e| format!("could not create {}: {e}", dest.display()))?;
    std::io::copy(&mut reader, &mut file).map_err(|e| format!("download write failed: {e}"))?;
    Ok(())
}

#[cfg(unix)]
fn make_executable(path: &Path) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;
    let mut perms = fs::metadata(path).map_err(|e| e.to_string())?.permissions();
    perms.set_mode(0o755);
    fs::set_permissions(path, perms).map_err(|e| e.to_string())
}

/// Spawns a detached helper that waits for this process to exit, then
/// launches `target_exe`. Shared by Linux and macOS - both have `/bin/sh`
/// and `kill -0`. The helper is a genuinely separate process (not a Tokio
/// task or thread), since it must keep running after this process exits.
#[cfg(unix)]
fn spawn_relaunch_waiter(target_exe: &Path) -> Result<(), String> {
    let pid = std::process::id();
    let script = format!(
        "while kill -0 {pid} 2>/dev/null; do sleep 0.2; done; exec \"{}\"",
        target_exe.display()
    );
    Command::new("sh")
        .arg("-c")
        .arg(script)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| format!("could not spawn relaunch helper: {e}"))?;
    Ok(())
}

#[cfg(target_os = "linux")]
fn apply_update_linux(asset_url: &str) -> Result<(), String> {
    // Set by the AppImage runtime to the mounted image's own original path
    // before launching the contained app - not set at all for a plain
    // `cargo run`/loose-binary dev build, which self-update doesn't support.
    let appimage_path = std::env::var("APPIMAGE")
        .map_err(|_| "not running from an AppImage - download the update manually".to_string())?;
    let appimage_path = PathBuf::from(appimage_path);

    // Same directory as the real file, so the rename below is same-device
    // (atomic, no cross-filesystem copy needed) regardless of where the
    // system temp dir happens to be mounted.
    let staged = appimage_path.with_extension("new");
    download_to_file(asset_url, &staged)?;
    make_executable(&staged)?;
    fs::rename(&staged, &appimage_path).map_err(|e| format!("could not replace AppImage: {e}"))?;

    spawn_relaunch_waiter(&appimage_path)?;
    Ok(())
}

#[cfg(target_os = "macos")]
fn apply_update_macos(asset_url: &str) -> Result<(), String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    // exe = .../Quarzlampe.app/Contents/MacOS/quarzlampe-tray
    let macos_dir = exe.parent().ok_or("unexpected exe path")?;
    let contents_dir = macos_dir.parent().ok_or("unexpected exe path")?;
    let bundle_dir = contents_dir.parent().ok_or("unexpected exe path")?;

    let tmp_dir = std::env::temp_dir().join(format!("quarzlampe-update-{}", std::process::id()));
    fs::create_dir_all(&tmp_dir).map_err(|e| e.to_string())?;
    let cleanup = scopeguard(&tmp_dir);

    let zip_path = tmp_dir.join("update.zip");
    download_to_file(asset_url, &zip_path)?;

    let extract_dir = tmp_dir.join("extracted");
    fs::create_dir_all(&extract_dir).map_err(|e| e.to_string())?;
    // `ditto`, not `unzip` - matches `build-dmg.sh`'s own choice for the
    // same reason (preserves the bundle's extended attributes correctly).
    let status = Command::new("ditto")
        .args(["-x", "-k"])
        .arg(&zip_path)
        .arg(&extract_dir)
        .status()
        .map_err(|e| format!("could not run ditto: {e}"))?;
    if !status.success() {
        return Err("ditto failed to extract the update archive".to_string());
    }

    let new_bundle_macos = extract_dir.join("Quarzlampe.app/Contents/MacOS");
    for bin in ["quarzlampe-tray", "quarzlampe-console"] {
        let src = new_bundle_macos.join(bin);
        if !src.exists() {
            return Err(format!("update archive is missing {bin}"));
        }
        let staged = macos_dir.join(format!("{bin}.new"));
        fs::copy(&src, &staged).map_err(|e| format!("could not stage {bin}: {e}"))?;
        make_executable(&staged)?;
        fs::rename(&staged, macos_dir.join(bin)).map_err(|e| format!("could not replace {bin}: {e}"))?;
    }
    drop(cleanup);

    // Re-sign the whole bundle, same as `build-dmg.sh` does when it's first
    // built - the files just swapped in are each individually signed
    // already (the zip was built from an already-signed bundle), but the
    // outer bundle-level seal (`_CodeSignature/CodeResources`, the hash
    // manifest of every file in the bundle) now points at stale hashes for
    // the two files we just replaced.
    let status = Command::new("codesign")
        .args(["--force", "--deep", "--sign", "-"])
        .arg(bundle_dir)
        .status()
        .map_err(|e| format!("could not run codesign: {e}"))?;
    if !status.success() {
        return Err("re-signing the updated app bundle failed".to_string());
    }

    spawn_relaunch_waiter(&macos_dir.join("quarzlampe-tray"))?;
    Ok(())
}

/// Tiny RAII guard so the extraction temp dir gets cleaned up on every
/// return path (including the early error returns via `?`) without
/// repeating `let _ = fs::remove_dir_all(...)` at each one.
#[cfg(target_os = "macos")]
struct TmpDirGuard(PathBuf);
#[cfg(target_os = "macos")]
impl Drop for TmpDirGuard {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
#[cfg(target_os = "macos")]
fn scopeguard(dir: &Path) -> TmpDirGuard {
    TmpDirGuard(dir.to_path_buf())
}

#[cfg(windows)]
fn apply_update_windows(asset_url: &str) -> Result<(), String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let install_dir = exe.parent().ok_or("unexpected exe path")?.to_path_buf();

    let tmp_dir = std::env::temp_dir().join(format!("quarzlampe-update-{}", std::process::id()));
    fs::create_dir_all(&tmp_dir).map_err(|e| e.to_string())?;

    let zip_path = tmp_dir.join("update.zip");
    download_to_file(asset_url, &zip_path)?;

    let extract_dir = tmp_dir.join("extracted");
    fs::create_dir_all(&extract_dir).map_err(|e| e.to_string())?;
    // `tar` has shipped on Windows (as a thin wrapper, able to extract zip
    // too) since the 1803 update / Server 2019 - avoids a zip crate
    // dependency just for this one call.
    let status = Command::new("tar")
        .arg("-xf")
        .arg(&zip_path)
        .arg("-C")
        .arg(&extract_dir)
        .status()
        .map_err(|e| format!("could not run tar: {e}"))?;
    if !status.success() {
        return Err("tar failed to extract the update archive".to_string());
    }

    let new_tray = extract_dir.join("quarzlampe-tray.exe");
    let new_console = extract_dir.join("quarzlampe-console.exe");
    if !new_tray.exists() || !new_console.exists() {
        return Err("update archive did not contain the expected exes".to_string());
    }

    // Can only stage these under the live names - the running tray.exe has
    // an exclusive mapping on its own file, so the actual overwrite has to
    // wait until this process has exited (see the helper script below).
    let staged_tray = install_dir.join("quarzlampe-tray.exe.new");
    let staged_console = install_dir.join("quarzlampe-console.exe.new");
    fs::copy(&new_tray, &staged_tray).map_err(|e| format!("could not stage tray exe: {e}"))?;
    fs::copy(&new_console, &staged_console).map_err(|e| format!("could not stage console exe: {e}"))?;

    let pid = std::process::id();
    let tray_exe = install_dir.join("quarzlampe-tray.exe");
    let console_exe = install_dir.join("quarzlampe-console.exe");
    let script_path = tmp_dir.join("apply_update.cmd");
    let script = format!(
        "@echo off\r\n\
         :wait\r\n\
         tasklist /FI \"PID eq {pid}\" 2>NUL | find \"{pid}\" >NUL\r\n\
         if not errorlevel 1 (\r\n\
         \ttimeout /t 1 /nobreak >NUL\r\n\
         \tgoto wait\r\n\
         )\r\n\
         move /y \"{staged_tray}\" \"{tray_exe}\" >NUL\r\n\
         move /y \"{staged_console}\" \"{console_exe}\" >NUL\r\n\
         start \"\" \"{tray_exe}\"\r\n",
        pid = pid,
        staged_tray = staged_tray.display(),
        tray_exe = tray_exe.display(),
        staged_console = staged_console.display(),
        console_exe = console_exe.display(),
    );
    fs::write(&script_path, script).map_err(|e| format!("could not write swap helper: {e}"))?;

    // `start /MIN` on the .cmd detaches it from this process's console (it
    // has none, being GUI-subsystem - see app/src/main.rs's own
    // windows_subsystem comment - but detaching explicitly here avoids
    // relying on that) so it survives this process exiting.
    Command::new("cmd")
        .args(["/C", "start", "/MIN", ""])
        .arg(&script_path)
        .spawn()
        .map_err(|e| format!("could not spawn swap helper: {e}"))?;

    Ok(())
}
