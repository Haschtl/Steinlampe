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
use std::time::Duration;

/// How long the two self-update trigger paths (tray menu, IPC server) each
/// wait after a successful `apply_update()` before calling
/// `std::process::exit`, so an in-flight response (an IPC write, or just
/// stdout) has time to actually reach its reader first. One shared constant
/// specifically because these two call sites previously drifted (the IPC
/// path delayed 500ms, the tray path exited immediately with a comment
/// claiming they matched) - each call site still does its own sleep (one
/// sync, one async - see `app::main`'s tray handler vs.
/// `ipc::server::handle_request`'s `ApplyUpdate` arm), but now from the same
/// duration instead of two independently-chosen ones.
pub const EXIT_GRACE: Duration = Duration::from_millis(500);

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
    // ureq 3.4.2 defaults every timeout (global/connect/recv_body) to
    // `None` - with no bound here, a stalled connection (dropped Wi-Fi,
    // GitHub throttling) would hang this call forever. A 15-20MB file over
    // even a slow connection finishes well inside this.
    let response = ureq::get(url)
        .header("User-Agent", "quarzlampe-desktop")
        .config()
        .timeout_global(Some(Duration::from_secs(120)))
        .build()
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
/// launches `target_exe`. Shared by Linux and macOS - both have `/bin/sh`,
/// `kill -0` and a POSIX-ish `ps`. The helper is a genuinely separate
/// process (not a Tokio task or thread), since it must keep running after
/// this process exits.
#[cfg(unix)]
fn spawn_relaunch_waiter(target_exe: &Path) -> Result<(), String> {
    let pid = std::process::id();
    // `kill -0 $pid` alone only proves *some* process has this PID right
    // now - if the OS recycles a just-freed PID for an unrelated process
    // during the 0.2s poll window, that check alone would wait on the
    // wrong process indefinitely (or not at all). Also matching the
    // command name narrows that to "a process named quarzlampe-tray with
    // this PID", which an unrelated reused-PID process essentially never
    // is. `ps -p PID -o comm=` is supported by both GNU ps (Linux) and BSD
    // ps (macOS) with no flag differences.
    let script = format!(
        "while ps -p {pid} -o comm= 2>/dev/null | grep -q quarzlampe-tray; do sleep 0.2; done; exec \"{}\"",
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
    const BINS: [&str; 2] = ["quarzlampe-tray", "quarzlampe-console"];

    // Back up both originals before touching anything live, so a failure
    // partway through (second binary missing/unreadable, disk full, a
    // permissions error) can be undone instead of leaving the bundle in a
    // mixed-version, unsealed state that only a full reinstall fixes.
    for bin in BINS {
        let backup = macos_dir.join(format!("{bin}.bak"));
        fs::copy(macos_dir.join(bin), &backup).map_err(|e| format!("could not back up {bin}: {e}"))?;
    }
    let restore_backups = || {
        for bin in BINS {
            let _ = fs::rename(macos_dir.join(format!("{bin}.bak")), macos_dir.join(bin));
        }
    };

    for bin in BINS {
        let src = new_bundle_macos.join(bin);
        if !src.exists() {
            restore_backups();
            return Err(format!("update archive is missing {bin} - rolled back"));
        }
        let staged = macos_dir.join(format!("{bin}.new"));
        if let Err(e) = fs::copy(&src, &staged).map_err(|e| e.to_string()).and_then(|_| make_executable(&staged)) {
            restore_backups();
            return Err(format!("could not stage {bin}: {e} - rolled back"));
        }
        if let Err(e) = fs::rename(&staged, macos_dir.join(bin)) {
            restore_backups();
            return Err(format!("could not replace {bin}: {e} - rolled back"));
        }
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
        // The new binaries are already in place and each individually
        // signed (from the already-signed zip) - only the bundle-level
        // seal is stale. Restoring the old binaries here would just
        // re-create that same staleness against the old versions, so
        // there's no clean rollback for this specific failure; surface it
        // clearly instead.
        return Err(
            "re-signing the updated app bundle failed - the new binaries are in place but the bundle's \
             signature seal is now stale; the app may refuse to launch until reinstalled"
                .to_string(),
        );
    }
    for bin in BINS {
        let _ = fs::remove_file(macos_dir.join(format!("{bin}.bak")));
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
    // Paths are arbitrary (user-chosen install dir, or the default
    // per-user AppData one) and may legitimately contain `%` (e.g. a
    // folder literally named "100% Lamps") - quoting alone doesn't stop
    // cmd.exe from expanding `%...%` inside a quoted string, only doubling
    // a literal `%` to `%%` does.
    let esc = |p: &Path| p.display().to_string().replace('%', "%%");

    // A separate tiny script, outside tmp_dir (in the shared temp root,
    // not the per-update subdirectory), whose only job is deleting
    // tmp_dir after a delay - apply_update.cmd itself can't do that to its
    // own containing directory (cmd.exe re-reads a running .bat/.cmd from
    // disk as it executes, including for the loops above, so deleting out
    // from under itself mid-run is not safe) or even to a same-directory
    // sibling script reliably. Left behind afterward rather than trying to
    // self-delete too (a handful of bytes, not worth the extra quoting
    // risk this close to the %-escaping fix above).
    let cleanup_script_path = std::env::temp_dir().join(format!("quarzlampe-cleanup-{pid}.cmd"));
    let cleanup_script = format!(
        "@echo off\r\ntimeout /t 3 /nobreak >NUL\r\nrd /s /q \"{}\"\r\n",
        esc(&tmp_dir)
    );
    fs::write(&cleanup_script_path, cleanup_script).map_err(|e| format!("could not write cleanup helper: {e}"))?;

    let script = format!(
        "@echo off\r\n\
         :wait\r\n\
         tasklist /FI \"PID eq {pid}\" 2>NUL | find \"{pid}\" >NUL\r\n\
         if not errorlevel 1 (\r\n\
         \ttimeout /t 1 /nobreak >NUL\r\n\
         \tgoto wait\r\n\
         )\r\n\
         rem quarzlampe-console.exe is a separate process the tray only\r\n\
         rem spawns on demand - if its window is still open (the common\r\n\
         rem case, since the console's own \"Update now\" button is the\r\n\
         rem primary trigger), it still holds its own exe file locked even\r\n\
         rem though the tray (this process) has already exited.\r\n\
         tasklist /FI \"IMAGENAME eq quarzlampe-console.exe\" 2>NUL | find /I \"quarzlampe-console.exe\" >NUL\r\n\
         if not errorlevel 1 (\r\n\
         \ttimeout /t 1 /nobreak >NUL\r\n\
         \tgoto wait\r\n\
         )\r\n\
         set TRAY_DONE=0\r\n\
         set CONSOLE_DONE=0\r\n\
         set RETRY=0\r\n\
         :moveloop\r\n\
         if %TRAY_DONE%==0 (\r\n\
         \tmove /y \"{staged_tray}\" \"{tray_exe}\" >NUL 2>NUL\r\n\
         \tif not errorlevel 1 set TRAY_DONE=1\r\n\
         )\r\n\
         if %CONSOLE_DONE%==0 (\r\n\
         \tmove /y \"{staged_console}\" \"{console_exe}\" >NUL 2>NUL\r\n\
         \tif not errorlevel 1 set CONSOLE_DONE=1\r\n\
         )\r\n\
         if %TRAY_DONE%==1 if %CONSOLE_DONE%==1 goto movedone\r\n\
         set /a RETRY+=1\r\n\
         if %RETRY% lss 10 (\r\n\
         \ttimeout /t 1 /nobreak >NUL\r\n\
         \tgoto moveloop\r\n\
         )\r\n\
         :movedone\r\n\
         rem Whichever of the two moves never succeeded just stays on its\r\n\
         rem old version (the pre-update file was never touched) rather\r\n\
         rem than this script hanging forever - matches apply_update's own\r\n\
         rem fail-safe rather than fail-loud posture elsewhere.\r\n\
         start \"\" \"{tray_exe}\"\r\n\
         start \"\" \"{cleanup_script}\"\r\n",
        pid = pid,
        staged_tray = esc(&staged_tray),
        tray_exe = esc(&tray_exe),
        staged_console = esc(&staged_console),
        console_exe = esc(&console_exe),
        cleanup_script = esc(&cleanup_script_path),
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
