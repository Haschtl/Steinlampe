//! macOS needs a real `NSApplication` run loop on the main thread for the
//! same reason Windows needs a message pump (see this module's parent doc
//! comment): `tray-icon`'s `NSStatusItem` and `global-hotkey`'s Carbon
//! event handler (`InstallEventHandler(GetApplicationEventTarget(), ...)`,
//! confirmed by reading `global-hotkey-0.8.0/src/platform_impl/macos/mod.rs`
//! directly) only ever receive anything once something actually runs that
//! loop - a bare `CFRunLoop` pump without `NSApplication` actually
//! `-run`ning is not the same thing and was deliberately not used here,
//! since it's genuinely unclear whether `NSStatusItem` click dispatch (which
//! goes through `NSApplication`'s own event-fetch-and-dispatch cycle, not a
//! raw run-loop source) would work without it - not a risk worth taking on
//! the one thing (the tray icon) this whole module exists to unblock.
//!
//! Because `-[NSApplication run]` never returns control to its caller until
//! the app quits, this inverts `main()`'s control flow on macOS specifically:
//! instead of `main()`'s own `loop { tick(); sleep(...); }`, a repeating
//! `NSTimer` on the main run loop calls `MainLoopCtx::tick` directly, and
//! `app.stop(None)` (called once `tick` reports "quit") is what lets `run()`
//! finally return.
//!
//! Written against the real typed `objc2-app-kit`/`objc2-foundation`
//! bindings already in this dependency tree (via `tray-icon`/`muda`/
//! `global-hotkey` themselves - same crate versions, see `Cargo.toml`'s
//! comment), not hand-rolled `objc2::msg_send!` calls - but, like every
//! other macOS-only module in this codebase (`core::mic::macos`,
//! `core::camera::macos`), never compiled on this Linux dev machine.
//! Unverified until tried on a real Mac.

use std::cell::RefCell;
use std::ptr::NonNull;
use std::rc::Rc;

use block2::RcBlock;
use objc2::MainThreadMarker;
use objc2_app_kit::{
    NSApplication, NSApplicationActivationPolicy, NSWorkspace, NSWorkspaceWillPowerOffNotification,
    NSWorkspaceWillSleepNotification,
};
use objc2_foundation::{NSDistributedNotificationCenter, NSNotification, NSNotificationCenter, NSTimer, ns_string};

use super::SystemEvent;
use crate::MainLoopCtx;

/// Registers one `NSNotificationCenter` observer whose block forwards
/// straight into `MainLoopCtx::handle_system_event`.
/// `addObserverForName_object_queue_usingBlock` hands its own copy of the
/// block to the notification center (standard Cocoa block-parameter
/// contract - the callee copies/retains what it needs past the call, same
/// as `NSTimer`'s scheduling method below), so neither the block nor the
/// returned observer token need to be kept alive here; the registration
/// itself is what's permanent, for this daemon's whole lifetime, which is
/// exactly what's wanted (never explicitly unregistered). `queue: None`
/// means the block runs synchronously, on whatever thread posted the
/// notification - for both centers used here, that's already the main
/// thread, so no further hop is needed before touching `ctx`.
fn observe(
    center: &NSNotificationCenter,
    name: &objc2_foundation::NSNotificationName,
    ctx: Rc<RefCell<MainLoopCtx>>,
    event: SystemEvent,
) {
    let block: RcBlock<dyn Fn(NonNull<NSNotification>)> = RcBlock::new(move |_note: NonNull<NSNotification>| {
        ctx.borrow_mut().handle_system_event(event);
    });
    unsafe {
        let _ = center.addObserverForName_object_queue_usingBlock(Some(name), None, None, &block);
    }
}

/// Must run before *anything* in the process touches AppKit or
/// CoreBluetooth - in particular, before `MainLoopCtx::new()` (which builds
/// the tray icon) and before `spawn_core_runtime()` (which initializes
/// `btleplug`'s CoreBluetooth central manager on a background thread).
/// Found live: with that ordering reversed (as it was before this
/// function existed - BLE init running on its own thread while the main
/// thread hadn't yet set up `NSApplication` at all), the Bluetooth
/// permission prompt never appeared - not denied, not granted, simply
/// never asked, and the app never even showed up in System Settings'
/// Bluetooth list. CoreBluetooth's authorization dialog needs a properly
/// registered, running application to attach to; asking before that
/// exists apparently just drops the request instead of queuing it.
/// Idempotent (`sharedApplication` always returns the same singleton), so
/// `run` below calling it again afterward is harmless.
pub fn init_app() {
    let Some(mtm) = MainThreadMarker::new() else {
        eprintln!("[platform/macos] init_app() must be called from the main thread");
        return;
    };
    let app = NSApplication::sharedApplication(mtm);
    // Accessory: a menu-bar-only process - no Dock icon, no application
    // menu bar of its own. The standard policy for a tray-only utility.
    let _ = app.setActivationPolicy(NSApplicationActivationPolicy::Accessory);
}

/// Takes over the main thread until `ctx.tick()` reports "quit" (returns
/// `false`), calling it roughly every 100ms via an `NSTimer` - the same
/// cadence `main()`'s plain `loop { ...; sleep(100ms); }` uses on
/// Linux/Windows. Never returns at all if this isn't actually the main
/// thread (logs and bails instead, same graceful-degradation shape as every
/// other platform-detection gap in this codebase) - `main()` must call
/// `init_app()` above, then build `ctx`, then call this, all from its own
/// entry point, not from a spawned thread.
pub fn run(ctx: MainLoopCtx) {
    let Some(mtm) = MainThreadMarker::new() else {
        eprintln!(
            "[platform/macos] run() must be called from the main thread - tray/hotkeys/sleep/shutdown/screen-lock won't work"
        );
        return;
    };

    let app = NSApplication::sharedApplication(mtm);

    let ctx = Rc::new(RefCell::new(ctx));

    let workspace_center = NSWorkspace::sharedWorkspace().notificationCenter();
    // Reading an `extern "C" static` (what these two notification-name
    // constants are under the hood) needs its own `unsafe` block as of the
    // 2024 edition, even though the value itself - a `&'static
    // NSNotificationName` pointing at an immutable, Apple-owned constant -
    // is about as safe a thing to read as an extern static gets.
    let will_sleep = unsafe { NSWorkspaceWillSleepNotification };
    let will_power_off = unsafe { NSWorkspaceWillPowerOffNotification };
    observe(&workspace_center, will_sleep, Rc::clone(&ctx), SystemEvent::BeforeSleep);
    observe(&workspace_center, will_power_off, Rc::clone(&ctx), SystemEvent::BeforeShutdown);

    // `com.apple.screenIsLocked`/`com.apple.screenIsUnlocked` on the
    // *distributed* notification center aren't formally public API, but
    // they're the long-standing, extremely widely relied-upon mechanism
    // every screen-lock-aware macOS app uses for exactly this - there is no
    // documented `NSWorkspace` equivalent for lock/unlock specifically (its
    // "session active" notifications are for fast user switching, a
    // different thing). Same "best-effort, not formally guaranteed by
    // Apple" caveat this codebase already applies to Linux's D-Bus
    // notification eavesdrop.
    let distributed_center = NSDistributedNotificationCenter::defaultCenter();
    observe(&distributed_center, ns_string!("com.apple.screenIsLocked"), Rc::clone(&ctx), SystemEvent::ScreenLocked);
    observe(&distributed_center, ns_string!("com.apple.screenIsUnlocked"), Rc::clone(&ctx), SystemEvent::ScreenUnlocked);

    let app_for_timer = app.clone();
    let timer_block: RcBlock<dyn Fn(NonNull<NSTimer>)> = RcBlock::new(move |_timer: NonNull<NSTimer>| {
        if !ctx.borrow_mut().tick() {
            app_for_timer.stop(None);
        }
    });
    unsafe {
        let _ = NSTimer::scheduledTimerWithTimeInterval_repeats_block(0.1, true, &timer_block);
    }

    // Blocks until `app.stop(None)` above lets it return - see this
    // module's doc comment for why this, and not a bare `CFRunLoop` pump,
    // is what actually drives `NSStatusItem` clicks and Carbon hotkey
    // dispatch on this platform.
    app.run();
}
