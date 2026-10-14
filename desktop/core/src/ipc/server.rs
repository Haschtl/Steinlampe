//! The daemon side of the IPC link - see `ipc/mod.rs`'s module doc for why
//! this exists and why it's loopback TCP + newline-JSON.

use std::sync::Arc;
use std::time::Duration;

use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Mutex;

use crate::ble::{LampCommand, LampManager};
use crate::config;
use crate::ipc::discovery_file_path;
use crate::ipc::protocol::{IpcRequest, IpcResponse, LampSummary, RequestKind, ScannedLamp, VisualizerConfig};
use crate::rules::{action_to_command, Rule};
use crate::state::ConnectionState;
use crate::update::UpdateInfo;

/// Everything the IPC server needs mutable access to. Bundled into one
/// struct (behind one `Mutex`) rather than two separate locks since every
/// `FireRule` request needs to read `rules` and act on `manager` together.
pub struct DaemonState {
    pub manager: LampManager,
    pub rules: Vec<Rule>,
    /// The music-reactive visualizer's live config, or `None` when off -
    /// read by `app`'s visualizer loop every ~100ms, written only here.
    /// Not persisted; always starts `None` on a fresh daemon.
    pub visualizer: Option<VisualizerConfig>,
    /// The visualizer loop's own last detection error (e.g. "unsupported
    /// platform"), surfaced back to the console so enabling it somewhere
    /// it can't actually work doesn't just silently do nothing. Written
    /// by the visualizer loop itself, not by any IPC handler.
    pub visualizer_error: Option<String>,
    /// The last result of `app::spawn_update_check_loop`'s background
    /// check - `None` until the first check completes (or forever, on a
    /// local dev build; see `core::update`). Written only by that loop,
    /// read here for `GetUpdateStatus`.
    pub update_available: Option<UpdateInfo>,
    /// Generated once at daemon startup (`app::spawn_core_runtime`) and
    /// shared by both servers that need to authenticate a caller: this IPC
    /// server (written to the discovery file, as before) and
    /// `app::spawn_webhook_server` (checked against a webhook URL's
    /// `?token=` parameter). One token for both, rather than a second one
    /// just for webhooks, since they're the same "something only this
    /// machine's own user could have" security property.
    pub auth_token: String,
    /// Set/cleared only by the tray menu's "Pause automations" toggle
    /// (`app`'s own event loop, same process, no IPC round-trip needed for
    /// that) - checked once at the top of `fire_rules_where`/
    /// `fire_rules_where_async` so every trigger source (mic, camera,
    /// boot, sleep/shutdown, notifications, hotkeys, the scheduled-time
    /// loop, the webhook server, screen lock/unlock) is covered by a
    /// single choke point rather than needing its own pause check. Not
    /// persisted - always starts `false` on a fresh daemon, same as
    /// `visualizer`, so a restart can't leave automations silently paused
    /// from a setting nobody remembers toggling days ago. Manually firing
    /// a rule from the console (`FireRule`) deliberately bypasses this -
    /// that's an explicit click, not an automation.
    pub automations_paused: bool,
}

pub struct IpcServer;

impl IpcServer {
    /// Binds a loopback TCP listener, publishes `{port}\n{auth_token}` to
    /// the discovery file, and serves requests forever (intended to run as
    /// a background tokio task in the daemon - this never returns under
    /// normal operation).
    pub async fn bind_and_serve(state: Arc<Mutex<DaemonState>>) -> anyhow::Result<()> {
        let auth_token = state.lock().await.auth_token.clone();
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let port = listener.local_addr()?.port();
        std::fs::write(discovery_file_path()?, format!("{port}\n{auth_token}"))?;

        loop {
            let (stream, _) = listener.accept().await?;
            let state = Arc::clone(&state);
            let auth_token = auth_token.clone();
            tokio::spawn(async move {
                if let Err(e) = handle_connection(stream, state, auth_token).await {
                    eprintln!("[ipc] connection error: {e}");
                }
            });
        }
    }
}

async fn handle_connection(
    stream: TcpStream,
    state: Arc<Mutex<DaemonState>>,
    auth_token: String,
) -> anyhow::Result<()> {
    let (reader, mut writer) = stream.into_split();
    let mut lines = BufReader::new(reader).lines();

    while let Some(line) = lines.next_line().await? {
        let response = match serde_json::from_str::<IpcRequest>(&line) {
            Ok(req) if req.auth_token == auth_token => handle_request(req.kind, &state).await,
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
        ConnectionState::ReconnectFailed => "reconnect_failed",
    }
    .to_string()
}

async fn handle_request(kind: RequestKind, state: &Arc<Mutex<DaemonState>>) -> IpcResponse {
    match kind {
        RequestKind::ListLamps => {
            let state = state.lock().await;
            let mut lamps: Vec<LampSummary> = state
                .manager
                .lamps()
                .map(|(key, handle)| {
                    let lamp_state = handle.state_rx.borrow();
                    LampSummary {
                        lamp_key: key.clone(),
                        name: handle.name.clone(),
                        connection: connection_state_label(&lamp_state.connection),
                        lamp_on: lamp_state.lamp_on,
                        brightness_pct: lamp_state.brightness_pct,
                        pattern: lamp_state.pattern,
                        last_error: lamp_state.last_error.clone(),
                    }
                })
                .collect();
            // Known from a previous run (or earlier this session) but not
            // currently live - still listed, "not_in_range", with no live
            // state, so the console shows it as a known device rather than
            // making it look like nothing was ever paired at all. See
            // `LampManager::known_but_not_live`'s doc comment.
            lamps.extend(state.manager.known_but_not_live().map(|(key, name)| LampSummary {
                lamp_key: key.clone(),
                name: name.clone(),
                connection: "not_in_range".to_string(),
                lamp_on: None,
                brightness_pct: None,
                pattern: None,
                last_error: None,
            }));
            IpcResponse::Lamps { lamps }
        }
        RequestKind::Scan { seconds } => {
            let mut state = state.lock().await;
            match state.manager.scan(Duration::from_secs(seconds)).await {
                Ok(found) => IpcResponse::ScanResults {
                    lamps: found.into_iter().map(|l| ScannedLamp { lamp_key: l.lamp_key, name: l.name }).collect(),
                },
                Err(e) => IpcResponse::Error { message: e.to_string() },
            }
        }
        RequestKind::Connect { lamp_key, name } => {
            let mut state = state.lock().await;
            match state.manager.connect_by_key(&lamp_key, name) {
                Ok(_state_rx) => IpcResponse::Ok,
                Err(e) => IpcResponse::Error { message: e.to_string() },
            }
        }
        RequestKind::SendCommand { lamp_key, line } => {
            let state = state.lock().await;
            if state.manager.send(&lamp_key, LampCommand::SendLine(line)) {
                IpcResponse::Ok
            } else {
                IpcResponse::Error { message: format!("unknown lamp_key {lamp_key}") }
            }
        }
        RequestKind::Disconnect { lamp_key } => {
            let state = state.lock().await;
            if state.manager.send(&lamp_key, LampCommand::Disconnect) {
                IpcResponse::Ok
            } else {
                IpcResponse::Error { message: format!("unknown lamp_key {lamp_key}") }
            }
        }
        RequestKind::Reconnect { lamp_key } => {
            let state = state.lock().await;
            if state.manager.send(&lamp_key, LampCommand::Reconnect) {
                IpcResponse::Ok
            } else {
                IpcResponse::Error { message: format!("unknown lamp_key {lamp_key}") }
            }
        }
        RequestKind::ListRules => {
            let state = state.lock().await;
            IpcResponse::Rules { rules: state.rules.clone() }
        }
        RequestKind::SaveRule { rule } => {
            let mut state = state.lock().await;
            if let Some(existing) = state.rules.iter_mut().find(|r| r.id == rule.id) {
                *existing = rule;
            } else {
                state.rules.push(rule);
            }
            match config::save_rules(&state.rules) {
                Ok(()) => IpcResponse::Ok,
                Err(e) => IpcResponse::Error { message: format!("saved in memory but failed to persist: {e}") },
            }
        }
        RequestKind::DeleteRule { id } => {
            let mut state = state.lock().await;
            state.rules.retain(|r| r.id != id);
            match config::save_rules(&state.rules) {
                Ok(()) => IpcResponse::Ok,
                Err(e) => IpcResponse::Error { message: format!("deleted in memory but failed to persist: {e}") },
            }
        }
        RequestKind::FireRule { id } => {
            let state = state.lock().await;
            let Some(rule) = state.rules.iter().find(|r| r.id == id) else {
                return IpcResponse::Error { message: format!("unknown rule id {id}") };
            };
            let line = action_to_command(&rule.action);
            for lamp_key in &rule.targets {
                state.manager.send(lamp_key, LampCommand::SendLine(line.clone()));
            }
            IpcResponse::Ok
        }
        RequestKind::SetVisualizer { config } => {
            let mut state = state.lock().await;
            state.visualizer = config;
            state.visualizer_error = None;
            IpcResponse::Ok
        }
        RequestKind::GetVisualizer => {
            let state = state.lock().await;
            IpcResponse::Visualizer { config: state.visualizer.clone(), last_error: state.visualizer_error.clone() }
        }
        RequestKind::GetUpdateStatus => {
            let state = state.lock().await;
            IpcResponse::UpdateStatus { available: state.update_available.clone() }
        }
        RequestKind::GetWebhookInfo => {
            let state = state.lock().await;
            IpcResponse::WebhookInfo { token: state.auth_token.clone() }
        }
    }
}
