//! `quarzlampe-tray --install` / `--uninstall` - a simple, no-wrapper
//! installer: running the binary you downloaded with this flag copies
//! itself (and the sibling console binary) into the right per-OS location
//! and adds the right per-OS launcher entry, instead of shipping a
//! separate installer executable or a packaging format per platform
//! (.deb/.msi/.dmg). Deliberately not that - see the user's own "simpel
//! aber funktional" framing. Does not touch autostart (the tray menu's
//! "Start on boot" toggle, `autostart.rs`, is a separate, explicit opt-in);
//! this only makes "download and run with one flag" give you a real,
//! discoverable, launchable app instead of two bare executables.
//!
//! Only the Linux path is verified (this dev environment, by running it
//! for real). Windows/macOS are written against each platform's
//! documented mechanism but unrun, same caveat as everywhere else in this
//! project - see PLAN.md.

use std::path::{Path, PathBuf};

/// The icon bytes to install alongside the binaries - the same PNG the
/// console embeds as its window icon (`console/assets/icon.png`), copied
/// here rather than shared across crates for one small file.
const ICON_PNG: &[u8] = include_bytes!("../assets/icon.png");

pub fn run(action: &str) -> anyhow::Result<()> {
    let tray_exe = std::env::current_exe()?;
    let console_exe = crate::console_binary_path()?;
    match action {
        "--install" => imp::install(&tray_exe, &console_exe),
        "--uninstall" => imp::uninstall(),
        _ => unreachable!("checked by caller"),
    }
}

fn copy_if_different(from: &Path, to: &Path) -> std::io::Result<()> {
    if let Some(dir) = to.parent() {
        std::fs::create_dir_all(dir)?;
    }
    std::fs::copy(from, to)?;
    Ok(())
}

#[cfg(target_os = "linux")]
mod imp {
    use super::*;

    fn bin_dir() -> Option<PathBuf> {
        Some(directories::BaseDirs::new()?.home_dir().join(".local").join("bin"))
    }

    fn icon_path() -> Option<PathBuf> {
        Some(
            directories::BaseDirs::new()?
                .home_dir()
                .join(".local/share/icons/hicolor/256x256/apps/dev.quarzlampe.png"),
        )
    }

    fn launcher_path() -> Option<PathBuf> {
        Some(directories::BaseDirs::new()?.data_dir().join("applications/dev.quarzlampe.tray.desktop"))
    }

    pub fn install(tray_exe: &Path, console_exe: &Path) -> anyhow::Result<()> {
        let bin_dir = bin_dir().ok_or_else(|| anyhow::anyhow!("no home directory"))?;
        let installed_tray = bin_dir.join("quarzlampe-tray");
        let installed_console = bin_dir.join("quarzlampe-console");
        copy_if_different(tray_exe, &installed_tray)?;
        copy_if_different(console_exe, &installed_console)?;

        if let Some(icon_path) = icon_path() {
            if let Some(dir) = icon_path.parent() {
                std::fs::create_dir_all(dir)?;
            }
            std::fs::write(&icon_path, ICON_PNG)?;
        }

        let launcher = launcher_path().ok_or_else(|| anyhow::anyhow!("no config directory"))?;
        if let Some(dir) = launcher.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let contents = format!(
            "[Desktop Entry]\nType=Application\nName=Quarzlampe\nComment=Control and automate your Quarzlampe lamps\nExec={}\nIcon=dev.quarzlampe\nTerminal=false\nCategories=Utility;\n",
            installed_tray.display()
        );
        std::fs::write(&launcher, contents)?;

        println!("[install] copied binaries to {}", bin_dir.display());
        println!("[install] added an app menu entry (Quarzlampe)");
        println!("[install] you may want to add {} to your PATH", bin_dir.display());
        println!("[install] \"Start on boot\" is a separate toggle in the tray menu - not enabled automatically");
        Ok(())
    }

    pub fn uninstall() -> anyhow::Result<()> {
        if let Some(dir) = bin_dir() {
            let _ = std::fs::remove_file(dir.join("quarzlampe-tray"));
            let _ = std::fs::remove_file(dir.join("quarzlampe-console"));
        }
        if let Some(path) = icon_path() {
            let _ = std::fs::remove_file(path);
        }
        if let Some(path) = launcher_path() {
            let _ = std::fs::remove_file(path);
        }
        println!("[uninstall] removed installed binaries, icon and app menu entry");
        println!("[uninstall] \"Start on boot\", if enabled, was left as-is - toggle it off in the tray menu first if you want it gone too");
        Ok(())
    }
}

#[cfg(target_os = "windows")]
mod imp {
    use super::*;

    fn install_dir() -> Option<PathBuf> {
        Some(PathBuf::from(std::env::var_os("LOCALAPPDATA")?).join("Programs").join("Quarzlampe"))
    }

    fn shortcut_path() -> Option<PathBuf> {
        Some(
            PathBuf::from(std::env::var_os("APPDATA")?)
                .join(r"Microsoft\Windows\Start Menu\Programs\Quarzlampe.lnk"),
        )
    }

    /// Creating a `.lnk` file needs either raw `IShellLink` COM interop or
    /// a short PowerShell one-liner - shelling out to the latter is the
    /// "simpel" half of "simpel aber funktional": every Windows install
    /// has `powershell.exe` and `WScript.Shell`, so this needs no new
    /// Win32/COM FFI surface at all for something that is otherwise a
    /// genuinely small, well-known scripting idiom.
    fn create_shortcut(target: &Path, shortcut: &Path) -> anyhow::Result<()> {
        if let Some(dir) = shortcut.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let script = format!(
            "$s = (New-Object -ComObject WScript.Shell).CreateShortcut('{}'); $s.TargetPath = '{}'; $s.Save()",
            shortcut.display(),
            target.display()
        );
        let status = std::process::Command::new("powershell")
            .args(["-NoProfile", "-NonInteractive", "-Command", &script])
            .status()?;
        if !status.success() {
            return Err(anyhow::anyhow!("powershell exited with status {status}"));
        }
        Ok(())
    }

    pub fn install(tray_exe: &Path, console_exe: &Path) -> anyhow::Result<()> {
        let dir = install_dir().ok_or_else(|| anyhow::anyhow!("%LOCALAPPDATA% is not set"))?;
        let installed_tray = dir.join("quarzlampe-tray.exe");
        let installed_console = dir.join("quarzlampe-console.exe");
        copy_if_different(tray_exe, &installed_tray)?;
        copy_if_different(console_exe, &installed_console)?;

        if let Some(shortcut) = shortcut_path() {
            create_shortcut(&installed_tray, &shortcut)?;
            println!("[install] created a Start Menu shortcut");
        }

        println!("[install] copied binaries to {}", dir.display());
        println!("[install] \"Start on boot\" is a separate toggle in the tray menu - not enabled automatically");
        Ok(())
    }

    pub fn uninstall() -> anyhow::Result<()> {
        if let Some(dir) = install_dir() {
            let _ = std::fs::remove_dir_all(&dir);
        }
        if let Some(shortcut) = shortcut_path() {
            let _ = std::fs::remove_file(shortcut);
        }
        println!("[uninstall] removed installed binaries and Start Menu shortcut");
        Ok(())
    }
}

#[cfg(target_os = "macos")]
mod imp {
    use super::*;

    /// A real (if minimal) `.app` bundle rather than bare executables -
    /// that's what makes it double-clickable from Finder/Launchpad at all
    /// on macOS. No `.icns` here (would need an icon-conversion step this
    /// dev environment can't exercise or verify) - it'll show the generic
    /// app icon, a cosmetic gap, not a functional one.
    fn app_bundle_dir() -> Option<PathBuf> {
        Some(directories::BaseDirs::new()?.home_dir().join("Applications").join("Quarzlampe.app"))
    }

    pub fn install(tray_exe: &Path, console_exe: &Path) -> anyhow::Result<()> {
        let bundle = app_bundle_dir().ok_or_else(|| anyhow::anyhow!("no home directory"))?;
        let macos_dir = bundle.join("Contents/MacOS");
        std::fs::create_dir_all(&macos_dir)?;
        copy_if_different(tray_exe, &macos_dir.join("quarzlampe-tray"))?;
        copy_if_different(console_exe, &macos_dir.join("quarzlampe-console"))?;

        let info_plist = bundle.join("Contents/Info.plist");
        let contents = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
             <!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
             <plist version=\"1.0\">\n<dict>\n\
             \t<key>CFBundleName</key>\n\t<string>Quarzlampe</string>\n\
             \t<key>CFBundleIdentifier</key>\n\t<string>dev.quarzlampe.tray</string>\n\
             \t<key>CFBundleExecutable</key>\n\t<string>quarzlampe-tray</string>\n\
             \t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n\
             \t<key>LSUIElement</key>\n\t<true/>\n\
             </dict>\n</plist>\n";
        std::fs::write(&info_plist, contents)?;

        println!("[install] installed Quarzlampe.app to {}", bundle.display());
        println!("[install] \"Start on boot\" is a separate toggle in the tray menu - not enabled automatically");
        Ok(())
    }

    pub fn uninstall() -> anyhow::Result<()> {
        if let Some(bundle) = app_bundle_dir() {
            let _ = std::fs::remove_dir_all(&bundle);
        }
        println!("[uninstall] removed Quarzlampe.app");
        Ok(())
    }
}

#[cfg(not(any(target_os = "linux", target_os = "windows", target_os = "macos")))]
mod imp {
    use super::*;

    pub fn install(_tray_exe: &Path, _console_exe: &Path) -> anyhow::Result<()> {
        Err(anyhow::anyhow!("installing is not implemented on this platform"))
    }

    pub fn uninstall() -> anyhow::Result<()> {
        Err(anyhow::anyhow!("uninstalling is not implemented on this platform"))
    }
}
