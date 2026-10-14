//! A single hidden window this process owns, used for two things at once:
//! giving `main()`'s loop something to call so `tray-icon`'s and
//! `global-hotkey`'s *own* hidden windows (each created the same way, on the
//! same thread - see this module's parent doc comment) actually get their
//! messages dispatched, and receiving the session/power messages the
//! sleep/shutdown/screen-lock triggers need. One window instead of two
//! separate ones: Win32 message dispatch is per-thread (`PeekMessageW` with
//! a null `hwnd` filter drains messages for every window this thread owns,
//! not just one), so there's no benefit to splitting it, and registering
//! for session-change notifications needs a window to register with anyway.
//!
//! Written against the same `windows-sys` APIs `global-hotkey`'s own
//! Windows backend already uses (same hidden-window-plus-`WndProc` shape,
//! confirmed by reading `global-hotkey-0.8.0/src/platform_impl/windows/mod.rs`
//! directly) - not guessed. Still unverified end to end: never compiled on
//! this Linux dev machine, same as every other Windows-only module here.

use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::OnceLock;

use windows_sys::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::System::RemoteDesktop::{NOTIFY_FOR_THIS_SESSION, WTSRegisterSessionNotification};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    CW_USEDEFAULT, CreateWindowExW, DefWindowProcW, DispatchMessageW, MSG, PBT_APMSUSPEND, PM_REMOVE,
    PeekMessageW, RegisterClassW, TranslateMessage, WM_POWERBROADCAST, WM_QUERYENDSESSION, WM_WTSSESSION_CHANGE,
    WNDCLASSW, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TRANSPARENT, WS_OVERLAPPED,
    WTS_SESSION_LOCK, WTS_SESSION_UNLOCK,
};

use super::SystemEvent;

static EVENT_TX: OnceLock<Sender<SystemEvent>> = OnceLock::new();

fn encode_wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

unsafe extern "system" fn event_wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_POWERBROADCAST if wparam as u32 == PBT_APMSUSPEND => send(SystemEvent::BeforeSleep),
        // Returning TRUE (the default `DefWindowProcW` behavior below) tells
        // Windows this process doesn't object to the session ending - this
        // is purely a "the system is about to shut down/log off" notice for
        // the trigger, never used to delay or veto it.
        WM_QUERYENDSESSION => send(SystemEvent::BeforeShutdown),
        WM_WTSSESSION_CHANGE => match wparam as u32 {
            WTS_SESSION_LOCK => send(SystemEvent::ScreenLocked),
            WTS_SESSION_UNLOCK => send(SystemEvent::ScreenUnlocked),
            _ => {}
        },
        _ => {}
    }
    unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
}

fn send(event: SystemEvent) {
    if let Some(tx) = EVENT_TX.get() {
        let _ = tx.send(event);
    }
}

/// Creates the hidden event window and registers it for session-change
/// notifications - must be called on the same thread `pump()` is called
/// from afterward (and the same thread `TrayIconBuilder::build()`/
/// `GlobalHotKeyManager::new()` are called from), since Win32 only ever
/// delivers a window's messages to the thread that created it.
pub fn init() -> Receiver<SystemEvent> {
    let (tx, rx) = channel();
    let _ = EVENT_TX.set(tx);

    let class_name = encode_wide("quarzlampe_event_window");
    unsafe {
        let hinstance = GetModuleHandleW(std::ptr::null());
        let wnd_class =
            WNDCLASSW { lpfnWndProc: Some(event_wnd_proc), lpszClassName: class_name.as_ptr(), hInstance: hinstance, ..std::mem::zeroed() };
        RegisterClassW(&wnd_class);

        let hwnd = CreateWindowExW(
            // Same reasoning as `global-hotkey`'s own hidden window: never
            // activatable, never shown, kept out of the taskbar.
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
            class_name.as_ptr(),
            std::ptr::null(),
            WS_OVERLAPPED,
            CW_USEDEFAULT,
            0,
            CW_USEDEFAULT,
            0,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            hinstance,
            std::ptr::null(),
        );
        if !hwnd.is_null() {
            WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        }
    }
    rx
}

/// Drains every pending Win32 message for this thread's windows (ours,
/// `tray-icon`'s, `global-hotkey`'s) without blocking - called once per
/// `main()` loop iteration, right before that iteration reads
/// `TrayIconEvent`/`MenuEvent`/`GlobalHotKeyEvent`, since those only get
/// pushed into their channels *during* `DispatchMessageW` here.
pub fn pump() {
    unsafe {
        let mut msg: MSG = std::mem::zeroed();
        while PeekMessageW(&mut msg, std::ptr::null_mut(), 0, 0, PM_REMOVE) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
