//! The daemon side of the IPC link - see `ipc/mod.rs`'s module doc for why
//! this exists and why it's loopback TCP + newline-JSON.

use std::sync::Arc;
use std::time::Duration;

use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Mutex;

use crate::ble::{LampCommand, LampManager};
use crate::ipc::discovery_file_path;
use crate::ipc::protocol::{IpcRequest, IpcResponse, LampSummary, RequestKind, ScannedLamp};
use crate::state::ConnectionState;

pub struct IpcServer;

impl IpcServer {
    /// Binds a loopback TCP listener, publishes `{port}\n{auth_token}` to
    /// the discovery file, and serves requests forever (intended to run as
    /// a background tokio task in the daemon - this never returns under
    /// normal operation).
    pub async fn bind_and_serve(manager: Arc<Mutex<LampManager>>) -> anyhow::Result<()> {
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let port = listener.local_addr()?.port();
        let auth_token = uuid::Uuid::new_v4().to_string();
        std::fs::write(discovery_file_path()?, format!("{port}\n{auth_token}"))?;

        loop {
            let (stream, _) = listener.accept().await?;
            let manager = Arc::clone(&manager);
            let auth_token = auth_token.clone();
            tokio::spawn(async move {
                if let Err(e) = handle_connection(stream, manager, auth_token).await {
                    eprintln!("[ipc] connection error: {e}");
                }
            });
        }
    }
}

async fn handle_connection(
    stream: TcpStream,
    manager: Arc<Mutex<LampManager>>,
    auth_token: String,
) -> anyhow::Result<()> {
    let (reader, mut writer) = stream.into_split();
    let mut lines = BufReader::new(reader).lines();

    while let Some(line) = lines.next_line().await? {
        let response = match serde_json::from_str::<IpcRequest>(&line) {
            Ok(req) if req.auth_token == auth_token => handle_request(req.kind, &manager).await,
            Ok(_) => IpcResponse::Error { message: "bad auth token".to_string() },
            Err(e) => IpcResponse::Error { message: format!("malformed request: {e}") },
        };
        let mut payload = serde_json::to_string(&response).unwrap_or_else(|_| "{}".to_string());
        payload.push('\n');
        writer.write_all(payload.as_bytes()).await?;
    }
    Ok(())
}

fn connection_state_label(state: &ConnectionState) -> String {
    match state {
        ConnectionState::Disconnected => "disconnected",
        ConnectionState::Connecting => "connecting",
        ConnectionState::AwaitingTrustToken => "awaiting_trust_token",
        ConnectionState::Connected => "connected",
        ConnectionState::Reconnecting => "reconnecting",
    }
    .to_string()
}

async fn handle_request(kind: RequestKind, manager: &Arc<Mutex<LampManager>>) -> IpcResponse {
    match kind {
        RequestKind::ListLamps => {
            let manager = manager.lock().await;
            let lamps = manager
                .lamps()
                .map(|(key, handle)| {
                    let state = handle.state_rx.borrow();
                    LampSummary {
                        lamp_key: key.clone(),
                        name: handle.name.clone(),
                        connection: connection_state_label(&state.connection),
                        lamp_on: state.lamp_on,
                        brightness_pct: state.brightness_pct,
                        pattern: state.pattern,
                        last_error: state.last_error.clone(),
                    }
                })
                .collect();
            IpcResponse::Lamps { lamps }
        }
        RequestKind::Scan { seconds } => {
            let mut manager = manager.lock().await;
            match manager.scan(Duration::from_secs(seconds)).await {
                Ok(found) => IpcResponse::ScanResults {
                    lamps: found.into_iter().map(|l| ScannedLamp { lamp_key: l.lamp_key, name: l.name }).collect(),
                },
                Err(e) => IpcResponse::Error { message: e.to_string() },
            }
        }
        RequestKind::Connect { lamp_key, name } => {
            let mut manager = manager.lock().await;
            match manager.connect_by_key(&lamp_key, name) {
                Ok(_state_rx) => IpcResponse::Ok,
                Err(e) => IpcResponse::Error { message: e.to_string() },
            }
        }
        RequestKind::SendCommand { lamp_key, line } => {
            let manager = manager.lock().await;
            if manager.send(&lamp_key, LampCommand::SendLine(line)) {
                IpcResponse::Ok
            } else {
                IpcResponse::Error { message: format!("unknown lamp_key {lamp_key}") }
            }
        }
        RequestKind::Disconnect { lamp_key } => {
            let manager = manager.lock().await;
            if manager.send(&lamp_key, LampCommand::Disconnect) {
                IpcResponse::Ok
            } else {
                IpcResponse::Error { message: format!("unknown lamp_key {lamp_key}") }
            }
        }
    }
}
