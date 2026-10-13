//! One tokio task per connected lamp. Owns the `btleplug` peripheral handle,
//! (re)connects with capped exponential backoff, runs the trust/token
//! handshake (`handshake.rs`), forwards `STATE|` updates into a
//! `watch::Sender<LampState>`, and relays outgoing commands from an
//! `mpsc` channel. This is the piece the M1 probe deliberately didn't
//! build (it proved the handshake logic standalone) - the actor/channel
//! wiring a real daemon needs.

use std::sync::Arc;
use std::time::Duration;

use btleplug::api::{Characteristic, Peripheral as _, WriteType};
use btleplug::platform::Peripheral;
use futures::StreamExt;
use tokio::sync::{mpsc, watch};

use crate::ble::handshake::{self, HandshakeOutcome};
use crate::ble::line_codec::LineCodec;
use crate::ble::trust::FileTokenStore;
use crate::ble::uuids;
use crate::state::{parse_state_line, ConnectionState, LampState};

pub enum LampCommand {
    SendLine(String),
    Disconnect,
    Shutdown,
}

const INITIAL_BACKOFF: Duration = Duration::from_millis(500);
const MAX_BACKOFF: Duration = Duration::from_secs(30);

fn set_connection(state_tx: &watch::Sender<LampState>, conn: ConnectionState) {
    state_tx.send_modify(|s| s.connection = conn);
}

fn set_disconnected_with_error(state_tx: &watch::Sender<LampState>, err: impl ToString) {
    state_tx.send_modify(|s| {
        s.connection = ConnectionState::Disconnected;
        s.last_error = Some(err.to_string());
    });
}

/// Runs for the lifetime of one lamp. Exits only on `LampCommand::Shutdown`
/// or once `cmd_rx`'s sender is dropped - any other failure (handshake
/// rejection, GATT error, notification stream ending) triggers a
/// reconnect-with-backoff instead of returning.
pub async fn run_lamp_actor(
    lamp_key: String,
    peripheral: Peripheral,
    mut cmd_rx: mpsc::UnboundedReceiver<LampCommand>,
    state_tx: watch::Sender<LampState>,
    tokens: Arc<FileTokenStore>,
) {
    let mut backoff = INITIAL_BACKOFF;

    loop {
        set_connection(&state_tx, ConnectionState::Connecting);

        match connect_once(&peripheral, &lamp_key, &tokens).await {
            Ok((cmd_char, mut line_rx)) => {
                backoff = INITIAL_BACKOFF; // reset after a successful connect
                set_connection(&state_tx, ConnectionState::Connected);
                match run_connected(&peripheral, &cmd_char, &mut cmd_rx, &mut line_rx, &state_tx).await {
                    RunOutcome::Shutdown => {
                        set_connection(&state_tx, ConnectionState::Disconnected);
                        return;
                    }
                    RunOutcome::Disconnected => {
                        set_connection(&state_tx, ConnectionState::Reconnecting);
                    }
                }
            }
            Err(ConnectError::PendingToken) => {
                // Not a bug - the lamp just didn't trust this connection
                // (outside its boot grace window, no valid stored token).
                // Keep retrying with backoff in case the user reboots it.
                state_tx.send_modify(|s| {
                    s.connection = ConnectionState::Reconnecting;
                    s.last_error = Some(
                        "lamp did not trust this connection - reboot it and reconnect within ~60s, \
                         or wait for a stored token to work next time"
                            .to_string(),
                    );
                });
            }
            Err(ConnectError::Other(e)) => {
                set_disconnected_with_error(&state_tx, e);
                set_connection(&state_tx, ConnectionState::Reconnecting);
            }
        }

        // Wait out the backoff, but return early on an explicit Shutdown.
        tokio::select! {
            _ = tokio::time::sleep(backoff) => {}
            cmd = cmd_rx.recv() => {
                if matches!(cmd, Some(LampCommand::Shutdown) | None) {
                    let _ = peripheral.disconnect().await;
                    set_connection(&state_tx, ConnectionState::Disconnected);
                    return;
                }
                // SendLine/Disconnect while not connected: nothing to do.
            }
        }
        backoff = (backoff * 2).min(MAX_BACKOFF);
    }
}

enum ConnectError {
    /// The handshake completed but the lamp didn't trust us - not a
    /// transport-level failure, so kept distinct from `Other`.
    PendingToken,
    Other(anyhow::Error),
}

impl From<anyhow::Error> for ConnectError {
    fn from(e: anyhow::Error) -> Self {
        Self::Other(e)
    }
}
impl From<btleplug::Error> for ConnectError {
    fn from(e: btleplug::Error) -> Self {
        Self::Other(e.into())
    }
}

async fn connect_once(
    peripheral: &Peripheral,
    lamp_key: &str,
    tokens: &FileTokenStore,
) -> Result<(Characteristic, mpsc::UnboundedReceiver<String>), ConnectError> {
    peripheral.connect().await?;
    peripheral.discover_services().await?;

    let chars = peripheral.characteristics();
    let cmd_char = chars
        .iter()
        .find(|c| c.uuid == uuids::COMMAND_CHAR)
        .ok_or_else(|| ConnectError::Other(anyhow::anyhow!("command characteristic not found")))?
        .clone();
    let status_char = chars
        .iter()
        .find(|c| c.uuid == uuids::STATUS_CHAR)
        .ok_or_else(|| ConnectError::Other(anyhow::anyhow!("status characteristic not found")))?
        .clone();

    // Subscribe before any write, matching useBle.ts's connect ordering.
    peripheral.subscribe(&status_char).await?;

    let (line_tx, line_rx) = mpsc::unbounded_channel::<String>();
    {
        let mut notifications = peripheral.notifications().await?;
        let status_uuid = uuids::STATUS_CHAR;
        tokio::spawn(async move {
            let mut codec = LineCodec::new();
            while let Some(notif) = notifications.next().await {
                if notif.uuid != status_uuid {
                    continue;
                }
                for line in codec.feed(&notif.value) {
                    if line_tx.send(line).is_err() {
                        return;
                    }
                }
            }
            // Stream ended (disconnect) - dropping line_tx here closes the
            // channel, which is how run_connected notices.
        });
    }

    let stored_token = tokens.load(lamp_key);
    let mut line_rx = line_rx;
    match handshake::perform_handshake(peripheral, &cmd_char, &mut line_rx, stored_token.as_deref()).await {
        Ok(HandshakeOutcome::Trusted { fresh_token }) => {
            if let Some(tok) = fresh_token {
                let _ = tokens.save(lamp_key, &tok);
            }
            Ok((cmd_char, line_rx))
        }
        Ok(HandshakeOutcome::PendingToken) => Err(ConnectError::PendingToken),
        Err(e) => Err(ConnectError::Other(e)),
    }
}

enum RunOutcome {
    Disconnected,
    Shutdown,
}

async fn run_connected(
    peripheral: &Peripheral,
    cmd_char: &Characteristic,
    cmd_rx: &mut mpsc::UnboundedReceiver<LampCommand>,
    line_rx: &mut mpsc::UnboundedReceiver<String>,
    state_tx: &watch::Sender<LampState>,
) -> RunOutcome {
    loop {
        tokio::select! {
            line = line_rx.recv() => {
                match line {
                    Some(line) => {
                        if let Some(update) = parse_state_line(&line) {
                            state_tx.send_modify(|s| s.apply_state_update(update));
                        }
                    }
                    None => return RunOutcome::Disconnected,
                }
            }
            cmd = cmd_rx.recv() => {
                match cmd {
                    Some(LampCommand::SendLine(line)) => {
                        let mut payload = line.into_bytes();
                        if !payload.ends_with(b"\n") {
                            payload.push(b'\n');
                        }
                        if peripheral.write(cmd_char, &payload, WriteType::WithoutResponse).await.is_err() {
                            return RunOutcome::Disconnected;
                        }
                    }
                    Some(LampCommand::Disconnect) => {
                        let _ = peripheral.disconnect().await;
                        return RunOutcome::Disconnected;
                    }
                    Some(LampCommand::Shutdown) | None => {
                        let _ = peripheral.disconnect().await;
                        return RunOutcome::Shutdown;
                    }
                }
            }
        }
    }
}
