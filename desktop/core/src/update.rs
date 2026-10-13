//! Checks whether a newer desktop build than the one currently running has
//! been published to this repo's rolling `desktop-latest` GitHub release,
//! and nothing more - deliberately notify-only, not a self-replacing
//! updater. Two reasons:
//!
//! 1. There's no code signing yet (see `.github/workflows/desktop-build.yml`'s
//!    guarded signing steps). Silently downloading and swapping in a new
//!    binary would mean trusting an unsigned file fetched over plain HTTPS
//!    with nothing to verify it against - exactly the kind of
//!    supply-chain-sensitive step that should wait until signing exists,
//!    not be built around its absence.
//! 2. Replacing a *running* executable's file is a genuinely different
//!    operation per OS (Linux/macOS allow it outright; Windows generally
//!    won't let you overwrite an open `.exe` without a separate
//!    swap-on-restart dance), and none of that can be exercised from this
//!    Linux dev environment anyway.
//!
//! What this does do: tell the daemon there's something newer, with a
//! direct link to the release page, so the tray menu/console can surface
//! it instead of updates going unnoticed.
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

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct UpdateInfo {
    /// The release page a person can open to download the new build by hand.
    pub html_url: String,
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
        Ok(Some(UpdateInfo { html_url: release.html_url }))
    }
}
