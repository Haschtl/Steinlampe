//! Checks whether a newer desktop build than the one currently running has
//! been published to this repo's rolling `desktop-latest` GitHub release.
//!
//! Also resolves the direct download URL for *this platform's* self-update
//! asset (`UpdateInfo::asset_url`) from that same release, for
//! `self_update::apply_update` to use. There is still no real code signing
//! (ad-hoc on macOS, optional/inactive SignPath on Windows) - downloading
//! and swapping in an unsigned binary fetched over plain HTTPS, with
//! nothing further to verify it against, is a real supply-chain tradeoff
//! this project is knowingly accepting rather than overlooking; see
//! `self_update`'s own doc comment. `html_url` (the release page) always
//! stays available as the manual fallback regardless.
//!
//! What this module does: tell the daemon there's something newer and
//! where to get it, so the tray menu/console can surface it (and offer to
//! apply it) instead of updates going unnoticed.
//!
//! "Newer" is decided by comparing the running build's embedded git commit
//! SHA (`BUILD_SHA`, baked in by `build.rs` from the `GITHUB_SHA` env var -
//! `"dev"` for any plain local `cargo build`, which this module always
//! treats as "nothing to check against", so ordinary development never
//! shows a false update prompt) against a small `BUILD_SHA.txt` asset the
//! release workflow publishes alongside the binaries - not against the
//! GitHub release/tag's own commit ref, which `action-gh-release` isn't
//! documented to reliably move when republishing to an already-existing
//! tag like `desktop-latest`.

use serde::{Deserialize, Serialize};

/// Baked in at compile time by `build.rs`.
pub const BUILD_SHA: &str = env!("QUARZLAMPE_BUILD_SHA");

const RELEASE_API_URL: &str = "https://api.github.com/repos/Haschtl/Steinlampe/releases/tags/desktop-latest";
const BUILD_SHA_ASSET_NAME: &str = "BUILD_SHA.txt";

/// Name of this platform's self-update asset, as published by
/// `.github/workflows/desktop-build.yml`'s `publish-release` job - a single
/// file per OS that needs no installer (see `self_update`'s doc comment for
/// why that matters: it's also the thing self-update downloads and swaps
/// into place).
#[cfg(target_os = "windows")]
const SELF_UPDATE_ASSET_NAME: &str = "Quarzlampe-portable-windows.zip";
#[cfg(target_os = "macos")]
const SELF_UPDATE_ASSET_NAME: &str = "Quarzlampe-portable-macos.zip";
#[cfg(target_os = "linux")]
const SELF_UPDATE_ASSET_NAME: &str = "Quarzlampe-x86_64.AppImage";

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct UpdateInfo {
    /// The release page a person can open to download the new build by hand.
    pub html_url: String,
    /// Direct download URL for this platform's self-update asset, if the
    /// release has one (absent only if the release is missing the expected
    /// asset for this OS, which shouldn't normally happen once the build
    /// workflow has run to completion).
    pub asset_url: Option<String>,
}

#[derive(Deserialize)]
struct ReleaseResponse {
    html_url: String,
    assets: Vec<ReleaseAsset>,
}

#[derive(Deserialize)]
struct ReleaseAsset {
    name: String,
    browser_download_url: String,
}

/// Blocking (plain `ureq` calls) - call from a dedicated thread (see
/// `app`'s `spawn_update_check_loop`), never from inside a tokio task.
pub fn check_for_update() -> Result<Option<UpdateInfo>, String> {
    if BUILD_SHA == "dev" {
        return Ok(None);
    }

    let release: ReleaseResponse = ureq::get(RELEASE_API_URL)
        .header("User-Agent", "quarzlampe-desktop")
        .call()
        .map_err(|e| format!("GitHub API request failed: {e}"))?
        .body_mut()
        .read_json()
        .map_err(|e| format!("GitHub API response was not the expected shape: {e}"))?;

    let Some(sha_asset) = release.assets.iter().find(|a| a.name == BUILD_SHA_ASSET_NAME) else {
        return Err(format!("release has no {BUILD_SHA_ASSET_NAME} asset"));
    };
    let latest_sha = ureq::get(sha_asset.browser_download_url.as_str())
        .header("User-Agent", "quarzlampe-desktop")
        .call()
        .map_err(|e| format!("failed to fetch {BUILD_SHA_ASSET_NAME}: {e}"))?
        .body_mut()
        .read_to_string()
        .map_err(|e| format!("failed to read {BUILD_SHA_ASSET_NAME}: {e}"))?;

    if latest_sha.trim() == BUILD_SHA {
        Ok(None)
    } else {
        let asset_url = release
            .assets
            .iter()
            .find(|a| a.name == SELF_UPDATE_ASSET_NAME)
            .map(|a| a.browser_download_url.clone());
        Ok(Some(UpdateInfo { html_url: release.html_url, asset_url }))
    }
}
