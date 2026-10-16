//! The trust/reconnect dance a client must perform on every connect,
//! replicated step-for-step from `frontend/src/hooks/useBle.ts`'s
//! `connectWithDevice` (lines ~336-410 as of this writing):
//!
//! 1. Subscribe to the status characteristic (caller's job, before calling
//!    into here - ordering matters, the firmware may start pushing data
//!    immediately on connect).
//! 2. If a token is cached for this lamp, send it (`auth <token>`) as the
//!    very first write.
//! 3. Send `status raw`, race it against a `STATUS_END|` line with a
//!    timeout (one retry in the web client; this module does a single
//!    attempt and lets the caller decide whether to retry).
//! 4. On success, send `trust token get` to (re)mint a token and capture any
//!    `TRUST_TOKEN|<hex>` line that comes back.
//!
//! If none of this completes in time, the firmware is almost certainly
//! holding the connection open pending a valid token (unrecognized address,
//! outside the ~60s post-boot grace window - see `BT_TRUST_GRACE_MS` in
//! `include/settings.h`). It might instead send `BLE_PAIR_PENDING|<addr>`,
//! meaning it's waiting on a physical switch/poti gesture at the lamp
//! itself instead (`confirmPendingPairing` in `src/comms.cpp`, the BLE
//! counterpart of the BT-Classic pairing confirmation that already
//! existed). That case gets a much longer deadline (`PENDING_PAIR_WAIT`)
//! than the plain token wait (`STATUS_WAIT`), since it requires a human to
//! actually walk over and touch the lamp rather than just an app replaying
//! a cached token. Either way, the firmware will disconnect on its own if
//! nothing arrives in time (`BLE_TOKEN_AUTH_TIMEOUT_MS`/
//! `BLE_PAIR_TIMEOUT_MS` in `src/comms.cpp`), a normal, expected outcome
//! for an untrusted lamp, not an error to panic over - see
//! `HandshakeOutcome::PendingToken`.

use std::time::Duration;

use btleplug::api::{Characteristic, Peripheral, WriteType};
use tokio::sync::mpsc::UnboundedReceiver;
use tokio::time::timeout;

/// How long to wait for `STATUS_END|` after sending `status raw`, matching
/// the web client's `useBle.ts` timeout.
pub const STATUS_WAIT: Duration = Duration::from_millis(1800);

/// Extended deadline once `BLE_PAIR_PENDING|` is seen - matches the
/// firmware's own worst case (an 8s token-auth window, then a further 20s
/// physical-gesture window: `BLE_TOKEN_AUTH_TIMEOUT_MS`/
/// `BLE_PAIR_TIMEOUT_MS` in `src/comms.cpp`), plus slack. Same value chosen
/// in `useBle.ts` for the same reason - keep the two in sync.
pub const PENDING_PAIR_WAIT: Duration = Duration::from_millis(32000);

#[derive(Debug)]
pub enum HandshakeOutcome {
    /// Got a full status snapshot - the lamp trusts this connection. Carries
    /// a freshly-minted token if one arrived in response to `trust token
    /// get`, so the caller can persist it.
    Trusted { fresh_token: Option<String> },
    /// No `STATUS_END|` within `STATUS_WAIT`. The lamp is most likely
    /// holding the connection pending a token it never got, and will
    /// disconnect on its own shortly.
    PendingToken,
}

pub async fn send_line<P: Peripheral>(peripheral: &P, char: &Characteristic, line: &str) -> btleplug::Result<()> {
    let mut payload = line.as_bytes().to_vec();
    if !payload.ends_with(b"\n") {
        payload.push(b'\n');
    }
    peripheral.write(char, &payload, WriteType::WithoutResponse).await
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
    stored_token: Option<&str>,
) -> anyhow::Result<HandshakeOutcome> {
    if let Some(token) = stored_token {
        send_line(peripheral, cmd_char, &format!("auth {token}")).await?;
    }

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

    if !got_status_end {
        return Ok(HandshakeOutcome::PendingToken);
    }

    send_line(peripheral, cmd_char, "trust token get").await?;
    let fresh_token = match timeout(Duration::from_millis(500), line_rx.recv()).await {
        Ok(Some(line)) => line.strip_prefix("TRUST_TOKEN|").map(|t| t.trim().to_string()),
        _ => None,
    };

    Ok(HandshakeOutcome::Trusted { fresh_token })
}
