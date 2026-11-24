//! The trust-check a client goes through on every connect, replicated from
//! `frontend/src/hooks/useBle.ts`'s `connectWithDevice`:
//!
//! 1. Subscribe to the status characteristic (caller's job, before calling
//!    into here - ordering matters, the firmware may start pushing data
//!    immediately on connect).
//! 2. Send `status raw`, race it against a `STATUS_END|` line with a
//!    timeout (one retry in the web client; this module does a single
//!    attempt and lets the caller decide whether to retry).
//!
//! The firmware now does real pairing/encryption (NimBLE bonding) before
//! any of this - `comms.cpp`'s `onConnect` forces it via
//! `NimBLEDevice::startSecurity()`, and both GATT characteristics require
//! encryption (`_ENC` properties) at the stack level, so step 1's
//! subscribe and step 2's write only succeed once that's done. None of
//! that needs anything from this client beyond what `btleplug`/the OS
//! Bluetooth stack already does automatically for a bonded link - no more
//! app-level token to send.
//!
//! If `STATUS_END|` never arrives, the lamp is either outside its ~60s
//! post-boot trust window and not yet on its trust list (so it's holding
//! the link open pending a physical switch/poti confirmation - it may send
//! `BLE_PAIR_PENDING|<addr>` to say so explicitly, see
//! `confirmPendingPairing` in `src/comms.cpp`), or something else is
//! wrong. Either way the firmware disconnects on its own if nothing
//! resolves this in time (`BLE_PAIR_TIMEOUT_MS` in `src/comms.cpp`) - a
//! normal, expected outcome for an unrecognized lamp, not an error to
//! panic over. See `HandshakeOutcome::NotTrusted`.

use std::time::Duration;

use btleplug::api::{Characteristic, Peripheral, WriteType};
use tokio::sync::mpsc::UnboundedReceiver;
use tokio::time::timeout;

/// How long to wait for `STATUS_END|` after sending `status raw`, matching
/// the web client's `useBle.ts` timeout.
pub const STATUS_WAIT: Duration = Duration::from_millis(1800);

/// Extended deadline once `BLE_PAIR_PENDING|` is seen - matches the
/// firmware's own `BLE_PAIR_TIMEOUT_MS` (20s physical-gesture window,
/// `src/comms.cpp`) plus slack. Same value chosen in `useBle.ts` for the
/// same reason - keep the two in sync.
pub const PENDING_PAIR_WAIT: Duration = Duration::from_millis(24000);

pub async fn send_line<P: Peripheral>(peripheral: &P, char: &Characteristic, line: &str) -> btleplug::Result<()> {
    let mut payload = line.as_bytes().to_vec();
    if !payload.ends_with(b"\n") {
        payload.push(b'\n');
    }
    peripheral.write(char, &payload, WriteType::WithoutResponse).await
}

#[derive(Debug)]
pub enum HandshakeOutcome {
    /// Got a full status snapshot - the lamp trusts this connection.
    Trusted,
    /// No `STATUS_END|` within the deadline. The lamp is most likely
    /// holding the connection pending trust (grace window missed, nobody
    /// confirmed a physical-gesture prompt) and will disconnect on its
    /// own shortly.
    NotTrusted,
}

/// Runs one handshake attempt. `line_rx` must already be receiving decoded
/// lines from the status characteristic's notifications (see
/// `LineCodec` + the caller's notification-forwarding task) - this function
/// only writes commands and waits on that channel, it doesn't touch the
/// subscription itself.
pub async fn perform_handshake<P: Peripheral>(
    peripheral: &P,
    cmd_char: &Characteristic,
    line_rx: &mut UnboundedReceiver<String>,
) -> anyhow::Result<HandshakeOutcome> {
    send_line(peripheral, cmd_char, "status raw").await?;

    let mut deadline = tokio::time::Instant::now() + STATUS_WAIT;
    let got_status_end = loop {
        let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
        if remaining.is_zero() {
            break false;
        }
        match timeout(remaining, line_rx.recv()).await {
            Ok(Some(line)) if line.starts_with("STATUS_END|") => break true,
            Ok(Some(line)) if line.starts_with("BLE_PAIR_PENDING|") => {
                // Deliberately held pending a physical gesture, not a
                // generic timeout - give it the longer budget instead of
                // giving up and reconnecting, which would just restart the
                // firmware's own pending window from zero.
                deadline = tokio::time::Instant::now() + PENDING_PAIR_WAIT;
                continue;
            }
            Ok(Some(_)) => continue, // some other status/state line - keep waiting
            _ => break false,        // timed out or channel closed
        }
    };

    Ok(if got_status_end { HandshakeOutcome::Trusted } else { HandshakeOutcome::NotTrusted })
}
