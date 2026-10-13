pub mod client;
pub mod protocol;
pub mod server;

pub use client::IpcClient;
pub use protocol::{IpcRequest, IpcResponse, LampSummary, RequestKind, ScannedLamp, UpdateInfo, VisualizerConfig};
pub use server::{DaemonState, IpcServer};

use std::path::PathBuf;

/// Where the daemon publishes `{port}\n{auth_token}` on startup, and where
/// the console looks for it. `runtime_dir()` isn't available on every
/// platform (notably macOS/Windows) - `cache_dir()` is always available and
/// fine for a tiny, short-lived discovery file like this.
pub fn discovery_file_path() -> anyhow::Result<PathBuf> {
    let dirs = directories::ProjectDirs::from("dev", "quarzlampe", "quarzlampe")
        .ok_or_else(|| anyhow::anyhow!("could not determine a config/cache directory on this platform"))?;
    let dir = dirs.runtime_dir().unwrap_or_else(|| dirs.cache_dir());
    std::fs::create_dir_all(dir)?;
    Ok(dir.join("daemon.conn"))
}
