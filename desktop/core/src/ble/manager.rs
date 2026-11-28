//! Scans for lamps and owns the set of spawned per-lamp actors
//! (`lamp_actor.rs`). Lives in the daemon process - see PLAN.md's "Decisions
//! already made" note on why BLE ownership moved there instead of the
//! console.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Context;
use btleplug::api::{Central, CentralEvent, CentralState, Manager as _, Peripheral as _, ScanFilter};
use btleplug::platform::{Adapter, Manager, Peripheral};
use tokio::sync::{mpsc, watch, Mutex};
use tokio::task::JoinHandle;

use crate::ipc::server::DaemonState;

use crate::ble::lamp_actor::{run_lamp_actor, LampCommand};
use crate::ble::uuids;
use crate::config::{self, KnownLamp};
use crate::state::LampState;

/// A lamp seen during a scan, not yet connected. The underlying
/// `Peripheral` handle is intentionally not exposed here - it isn't
/// meaningful outside this process (the daemon, in the two-process
/// architecture), so callers across the IPC boundary only ever see
/// `lamp_key`/`name`; `LampManager` keeps its own cache to resolve a later
/// `connect_by_key` back to the real handle.
pub struct DiscoveredLamp {
    pub lamp_key: String,
    pub name: String,
}

pub struct LampHandle {
    pub name: String,
    pub cmd_tx: mpsc::UnboundedSender<LampCommand>,
    pub state_rx: watch::Receiver<LampState>,
    join: JoinHandle<()>,
}

pub struct LampManager {
    adapter: Adapter,
    lamps: HashMap<String, LampHandle>,
    /// Peripherals seen by the most recent `scan()`, keyed by lamp_key, so
    /// `connect_by_key` (driven by an IPC request that only carries the
    /// key) can look the real handle back up.
    scanned: HashMap<String, Peripheral>,
    /// Every lamp ever successfully connected to, by key -> name, loaded
    /// from `config::load_known_lamps()` at startup and persisted again on
    /// every new one - so the console can still list a lamp as "known" the
    /// moment the daemon restarts, before any scan has run and even if the
    /// lamp never comes back into range this session. Deliberately *not*
    /// used to widen `app::spawn_auto_reconnect_loop`'s background-scan set
    /// (which stays scoped to rule/visualizer-relevant lamps on purpose,
    /// see its own doc comment) - this is purely about what the console
    /// *shows*, not what the daemon actively goes looking for.
    known: HashMap<String, String>,
    /// Whether the OS's own Bluetooth adapter is currently powered on -
    /// `None` until the first real answer comes in (see
    /// `spawn_bluetooth_adapter_watch` below, which owns writing this).
    /// Distinct from any per-lamp `ConnectionState`: this is "can BLE work
    /// at all right now", not "is a specific lamp reachable" - the console
    /// surfaces it as its own banner rather than folding it into every
    /// lamp's own status.
    bluetooth_powered: Option<bool>,
}

impl LampManager {
    pub async fn new() -> anyhow::Result<Self> {
        let manager = Manager::new().await?;
        let adapter = manager
            .adapters()
            .await?
            .into_iter()
            .next()
            .context("no Bluetooth adapter found")?;
        let known = config::load_known_lamps()
            .unwrap_or_else(|e| {
                eprintln!("[ble] failed to load known_lamps.toml, starting with none: {e}");
                Vec::new()
            })
            .into_iter()
            .map(|k| (k.lamp_key, k.name))
            .collect();
        Ok(Self { adapter, lamps: HashMap::new(), scanned: HashMap::new(), known, bluetooth_powered: None })
    }

    /// An owned, cloneable handle to the same real adapter this manager
    /// scans/connects with - btleplug's `Adapter` is a thin `Clone`
    /// wrapper, so this doesn't open a second adapter. Used only by
    /// `spawn_bluetooth_adapter_watch` below to run its own `events()`
    /// stream independently of whatever's holding `DaemonState`'s lock at
    /// any given moment.
    pub fn adapter_handle(&self) -> Adapter {
        self.adapter.clone()
    }

    pub fn bluetooth_powered(&self) -> Option<bool> {
        self.bluetooth_powered
    }

    pub fn set_bluetooth_powered(&mut self, powered: Option<bool>) {
        self.bluetooth_powered = powered;
    }

    /// Records a lamp as known (if it isn't already, under this exact
    /// name) and persists the updated list. Called every time a connect
    /// succeeds, not just the first time - a renamed lamp (via `name
    /// <text>`, reported back the next time it's connected to) should
    /// update the stored name too, not get stuck with whatever it was
    /// first seen as.
    fn remember(&mut self, lamp_key: &str, name: &str) {
        if self.known.get(lamp_key).map(String::as_str) == Some(name) {
            return;
        }
        self.known.insert(lamp_key.to_string(), name.to_string());
        let list: Vec<KnownLamp> =
            self.known.iter().map(|(k, n)| KnownLamp { lamp_key: k.clone(), name: n.clone() }).collect();
        if let Err(e) = config::save_known_lamps(&list) {
            eprintln!("[ble] failed to persist known_lamps.toml: {e}");
        }
    }

    /// Known lamps with no live handle right now - i.e. the ones the
    /// console should still list, just with no live state, since
    /// `lamps()` alone would otherwise make it look like nothing was ever
    /// paired at all until the next successful scan+connect.
    pub fn known_but_not_live(&self) -> impl Iterator<Item = (&String, &String)> {
        self.known.iter().filter(|(k, _)| !self.lamps.contains_key(k.as_str()))
    }

    /// Scans for `scan_time` and returns any lamps seen, matched the same
    /// way `core/examples/probe.rs` does (service-UUID filtering happens
    /// *after* an unfiltered scan - filtering the adapter scan itself by
    /// service UUID missed a real lamp in testing, see PLAN.md's M1 notes).
    pub async fn scan(&mut self, scan_time: Duration) -> anyhow::Result<Vec<DiscoveredLamp>> {
        self.adapter.start_scan(ScanFilter::default()).await.context("failed to start BLE scan")?;
        tokio::time::sleep(scan_time).await;
        self.adapter.stop_scan().await.ok();

        let mut by_service = Vec::new();
        let mut by_name_only = Vec::new();
        for p in self.adapter.peripherals().await? {
            let Some(props) = p.properties().await? else { continue };
            let lamp_key = format!("{:?}", p.id());
            let Some(name) = props.local_name else { continue };
            self.scanned.insert(lamp_key.clone(), p);
            if props.services.contains(&uuids::SERVICE) {
                by_service.push(DiscoveredLamp { lamp_key, name });
            } else {
                by_name_only.push(DiscoveredLamp { lamp_key, name });
            }
        }
        Ok(if !by_service.is_empty() { by_service } else { by_name_only })
    }

    /// Spawns a lamp actor for a lamp found by a previous `scan()` and
    /// starts it connecting. Replacing an existing entry for the same
    /// `lamp_key` shuts the old actor down first.
    pub fn connect_by_key(&mut self, lamp_key: &str, name: String) -> anyhow::Result<watch::Receiver<LampState>> {
        let peripheral = self
            .scanned
            .get(lamp_key)
            .cloned()
            .context("lamp not seen in the most recent scan - scan again")?;

        if let Some(old) = self.lamps.remove(lamp_key) {
            let _ = old.cmd_tx.send(LampCommand::Shutdown);
        }

        self.remember(lamp_key, &name);

        let (cmd_tx, cmd_rx) = mpsc::unbounded_channel();
        let (state_tx, state_rx) = watch::channel(LampState::default());
        let join = tokio::spawn(run_lamp_actor(peripheral, cmd_rx, state_tx));

        self.lamps.insert(
            lamp_key.to_string(),
            LampHandle { name, cmd_tx, state_rx: state_rx.clone(), join },
        );
        Ok(state_rx)
    }

    pub fn send(&self, lamp_key: &str, cmd: LampCommand) -> bool {
        self.lamps.get(lamp_key).is_some_and(|h| h.cmd_tx.send(cmd).is_ok())
    }

    pub fn lamps(&self) -> impl Iterator<Item = (&String, &LampHandle)> {
        self.lamps.iter()
    }

    pub async fn shutdown_all(&mut self) {
        for (_, handle) in self.lamps.drain() {
            let _ = handle.cmd_tx.send(LampCommand::Shutdown);
            let _ = handle.join.await;
        }
    }
}

/// Keeps `DaemonState`'s `bluetooth_powered` in sync with the OS's own
/// Bluetooth adapter power state. Lives here (not in `app`) because
/// `btleplug` is only ever a dependency of this crate - `app` orchestrates
/// but never touches `btleplug` types directly, same as everywhere else in
/// this daemon. Cross-platform (unlike the D-Bus-based watches `app` runs
/// alongside this one): `Central::adapter_state`/`events()` are part of
/// `btleplug`'s common API, not a Linux-only backend detail.
///
/// Queries once immediately (so the console doesn't sit at "unknown" for a
/// whole scan cycle on a fresh daemon), then follows `CentralEvent::
/// StateUpdate` for as long as the daemon runs. If the adapter is pulled
/// out mid-session (or the backend's event stream itself errors), this task
/// just ends - `bluetooth_powered` keeps showing its last known value
/// rather than silently flipping back to "on" by going unanswered forever.
pub async fn spawn_bluetooth_adapter_watch(state: Arc<Mutex<DaemonState>>) {
    let adapter = state.lock().await.manager.adapter_handle();

    match adapter.adapter_state().await {
        Ok(initial) => {
            state.lock().await.manager.set_bluetooth_powered(Some(initial == CentralState::PoweredOn));
        }
        Err(e) => eprintln!("[bluetooth] could not read initial adapter state: {e}"),
    }

    tokio::spawn(async move {
        use futures::StreamExt;

        let mut events = match adapter.events().await {
            Ok(e) => e,
            Err(e) => {
                eprintln!("[bluetooth] could not subscribe to adapter events, power-state banner won't update: {e}");
                return;
            }
        };
        while let Some(event) = events.next().await {
            if let CentralEvent::StateUpdate(new_state) = event {
                state.lock().await.manager.set_bluetooth_powered(Some(new_state == CentralState::PoweredOn));
            }
        }
    });
}
