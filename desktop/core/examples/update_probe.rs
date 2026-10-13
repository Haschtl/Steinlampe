//! Terminal-only exerciser for `core::update::check_for_update`, same spirit
//! as `mic_probe`/`camera_probe`/`audio_probe` - no GUI, just print what the
//! real GitHub API call found. `BUILD_SHA` only becomes non-"dev" when
//! `GITHUB_SHA` was set at *build* time (see `core/build.rs`), so running
//! this normally always reports "dev build, skipped" - pass
//! `GITHUB_SHA=<anything>` to `cargo build`/`cargo run` to actually exercise
//! the network path.

fn main() {
    println!("BUILD_SHA = {}", quarzlampe_core::update::BUILD_SHA);
    match quarzlampe_core::update::check_for_update() {
        Ok(Some(info)) => println!("update available: {}", info.html_url),
        Ok(None) => println!("no update (or dev build, skipped)"),
        Err(e) => println!("check failed: {e}"),
    }
}
