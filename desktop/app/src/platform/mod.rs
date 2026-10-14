//! Windows/macOS event-loop glue the tray daemon needs that Linux simply
//! doesn't: `tray-icon`'s Windows and macOS backends each create their own
//! hidden window / Carbon event target, same as `global-hotkey`'s do, and
//! both crates' own docs say outright that *something* has to pump a real
//! native event loop on the thread that created them before any of
//! it - tray clicks, menu clicks, hotkeys - actually does anything (confirmed
//! by reading each crate's own platform backend: `tray-icon`'s and
//! `global-hotkey`'s Windows modules each call `CreateWindowExW` with their
//! own `WndProc`, and `global-hotkey`'s macOS module installs a Carbon event
//! handler via `GetApplicationEventTarget()`, neither of which receives
//! anything without a loop). Linux needs none of this: the `ksni` tray
//! backend runs its own D-Bus worker thread, the X11 `global-hotkey` backend
//! runs its own event-pumping thread, and `main.rs`'s own zbus-based
//! sleep/shutdown/screen-lock watches are already async tasks on the tokio
//! runtime - nothing in this module applies there.
//!
//! The same missing piece also gates `OnBeforeSleep`/`BeforeShutdown`/
//! `ScreenLocked`/`ScreenUnlocked` on these two platforms, since their native
//! equivalents (`WM_POWERBROADCAST`/`WM_QUERYENDSESSION`/
//! `WM_WTSSESSION_CHANGE` on Windows, `NSWorkspace`/distributed-notification
//! observers on macOS) are delivered through that same event loop - so this
//! module's `windows`/`macos` backends fire those directly, alongside just
//! pumping messages.
//!
//! Like every other Windows/macOS-only module in this codebase
//! (`core::mic::windows`/`macos`, `core::camera::windows`/`macos`), this is
//! written carefully against each platform's documented APIs (and, for
//! macOS, the real typed `objc2-app-kit`/`objc2-foundation` bindings the
//! GUI crates above already depend on - not hand-rolled message sends) but
//! never compiled or run on this Linux dev machine. Unverified until tried
//! on real hardware.

#[cfg(target_os = "macos")]
pub mod macos;
#[cfg(windows)]
pub mod windows;

/// The non-Linux equivalents of the `Trigger` variants `main.rs` already
/// fires natively via zbus on Linux - kept as a small enum here (rather than
/// depending on `quarzlampe_core::rules::Trigger` directly from this module)
/// so this module stays a thin "what native thing just happened" signal,
/// with the actual rule-matching left to the caller. Only exists on the two
/// platforms that need it - on Linux this (and `MainLoopCtx::
/// handle_system_event`, its only consumer) would otherwise just be dead
/// code, since nothing ever constructs one there.
#[cfg(any(windows, target_os = "macos"))]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SystemEvent {
    BeforeSleep,
    BeforeShutdown,
    ScreenLocked,
    ScreenUnlocked,
}
