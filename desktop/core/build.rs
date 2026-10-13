//! Bakes the git commit this binary was built from into `core::update::BUILD_SHA`
//! (via `env!("QUARZLAMPE_BUILD_SHA")`), read from CI's `GITHUB_SHA` - never
//! from a local `git rev-parse`, so an ordinary `cargo build` on a dev
//! machine always gets `"dev"` instead of a real-looking SHA that would
//! make `core::update::check_for_update` compare it against actual
//! releases and show false "update available" noise while developing.

fn main() {
    println!("cargo:rerun-if-env-changed=GITHUB_SHA");
    let sha = std::env::var("GITHUB_SHA").unwrap_or_else(|_| "dev".to_string());
    println!("cargo:rustc-env=QUARZLAMPE_BUILD_SHA={sha}");
}
