//! "Start on boot" - a tray menu toggle backed by whatever each OS's own
//! native autostart mechanism is, not a cross-platform abstraction with
//! its own config file. Only the Linux path is verified (this dev
//! environment); Windows/macOS are written against documented mechanisms
//! but unrun, like every other cross-platform piece in this project - see
//! PLAN.md.

#[cfg(target_os = "linux")]
mod imp {
    //! XDG autostart: a `.desktop` file under `~/.config/autostart/`,
    //! picked up by every major desktop environment (GNOME, KDE, XFCE...)
    //! without any further registration step.

    use std::path::PathBuf;

    fn desktop_file_path() -> Option<PathBuf> {
        let dirs = directories::BaseDirs::new()?;
        Some(dirs.config_dir().join("autostart").join("dev.quarzlampe.tray.desktop"))
    }

    pub fn is_enabled() -> bool {
        desktop_file_path().is_some_and(|p| p.exists())
    }

    pub fn set_enabled(enabled: bool) -> anyhow::Result<()> {
        let path = desktop_file_path().ok_or_else(|| anyhow::anyhow!("no config directory on this platform"))?;
        if !enabled {
            if path.exists() {
                std::fs::remove_file(&path)?;
            }
            return Ok(());
        }
        let exe = std::env::current_exe()?;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let contents = format!(
            "[Desktop Entry]\nType=Application\nName=Quarzlampe\nComment=Quarzlampe lamp tray daemon\nExec={}\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n",
            exe.display()
        );
        std::fs::write(&path, contents)?;
        Ok(())
    }
}

#[cfg(target_os = "windows")]
mod imp {
    //! A value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` -
    //! the standard per-user autostart mechanism; doesn't need admin rights
    //! or a service install.

    use winreg::enums::HKEY_CURRENT_USER;
    use winreg::RegKey;

    const RUN_KEY_PATH: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
    const VALUE_NAME: &str = "Quarzlampe";

    fn open_run_key() -> std::io::Result<RegKey> {
        RegKey::predef(HKEY_CURRENT_USER).open_subkey_with_flags(RUN_KEY_PATH, winreg::enums::KEY_READ | winreg::enums::KEY_WRITE)
    }

    pub fn is_enabled() -> bool {
        open_run_key().and_then(|key| key.get_value::<String, _>(VALUE_NAME)).is_ok()
    }

    pub fn set_enabled(enabled: bool) -> anyhow::Result<()> {
        let key = open_run_key()?;
        if enabled {
            let exe = std::env::current_exe()?;
            key.set_value(VALUE_NAME, &exe.to_string_lossy().to_string())?;
        } else {
            let _ = key.delete_value(VALUE_NAME);
        }
        Ok(())
    }
}

#[cfg(target_os = "macos")]
mod imp {
    //! A per-user LaunchAgent plist under `~/Library/LaunchAgents/` -
    //! loaded by `launchd` at login, the standard non-admin autostart
    //! mechanism on macOS.

    use std::path::PathBuf;

    fn plist_path() -> Option<PathBuf> {
        let home = directories::BaseDirs::new()?.home_dir().to_path_buf();
        Some(home.join("Library").join("LaunchAgents").join("dev.quarzlampe.tray.plist"))
    }

    pub fn is_enabled() -> bool {
        plist_path().is_some_and(|p| p.exists())
    }

    pub fn set_enabled(enabled: bool) -> anyhow::Result<()> {
        let path = plist_path().ok_or_else(|| anyhow::anyhow!("no home directory on this platform"))?;
        if !enabled {
            if path.exists() {
                std::fs::remove_file(&path)?;
            }
            return Ok(());
        }
        let exe = std::env::current_exe()?;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let contents = format!(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
             <!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
             <plist version=\"1.0\">\n<dict>\n\
             \t<key>Label</key>\n\t<string>dev.quarzlampe.tray</string>\n\
             \t<key>ProgramArguments</key>\n\t<array>\n\t\t<string>{}</string>\n\t</array>\n\
             \t<key>RunAtLoad</key>\n\t<true/>\n\
             </dict>\n</plist>\n",
            exe.display()
        );
        std::fs::write(&path, contents)?;
        Ok(())
    }
}

#[cfg(not(any(target_os = "linux", target_os = "windows", target_os = "macos")))]
mod imp {
    pub fn is_enabled() -> bool {
        false
    }

    pub fn set_enabled(_enabled: bool) -> anyhow::Result<()> {
        Err(anyhow::anyhow!("start-on-boot is not implemented on this platform"))
    }
}

pub use imp::{is_enabled, set_enabled};
