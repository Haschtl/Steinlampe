//! M1 terminal probe: exercise the BLE core against a real lamp without any
//! GUI. Scans for the lamp's service UUID, connects, replicates the
//! trust-check dance (`quarzlampe_core::ble::handshake`), then drops into
//! an interactive command prompt.
//!
//! Usage:
//!   cargo run -p quarzlampe-core --example probe
//!
//! Commands once connected:
//!   <any text>   -> sent as a raw command line to the lamp (e.g. "on", "off")
//!   quit         -> disconnect and exit

use std::io::Write as _;
use std::time::Duration;

use anyhow::Context;
use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter};
use btleplug::platform::Manager;
use futures::StreamExt;
use quarzlampe_core::ble::{perform_handshake, send_line, uuids, HandshakeOutcome, LineCodec};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::sync::mpsc;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let manager = Manager::new().await?;
    let adapters = manager.adapters().await?;
    let adapter = adapters.into_iter().next().context("no Bluetooth adapter found")?;

    println!("[probe] scanning for quarzlampe lamps ({})...", uuids::SERVICE);
    // Unfiltered scan: filtering adapter-side by service UUID (ScanFilter {
    // services: vec![uuids::SERVICE] }) missed a real lamp in testing here -
    // same category of issue the web frontend already works around (see the
    // acceptAllDevices fallback comment in bleClient.ts). Filter afterward
    // instead.
    adapter.start_scan(ScanFilter::default()).await.context("failed to start BLE scan")?;
    tokio::time::sleep(Duration::from_secs(4)).await;
    adapter.stop_scan().await.ok();

    let peripherals = adapter.peripherals().await?;
    let mut by_service = Vec::new();
    let mut by_name_only = Vec::new();
    for p in peripherals {
        if let Some(props) = p.properties().await? {
            let name = props.local_name.clone();
            if props.services.contains(&uuids::SERVICE) {
                by_service.push((p, name.unwrap_or_else(|| "(unnamed)".to_string())));
            } else if let Some(name) = name {
                by_name_only.push((p, name));
            }
        }
    }
    let candidates = if !by_service.is_empty() { by_service } else { by_name_only };

    if candidates.is_empty() {
        anyhow::bail!("no lamp found - is it powered on and nearby?");
    }
    for (i, (_, name)) in candidates.iter().enumerate() {
        println!("  [{i}] {name}");
    }
    let chosen = if candidates.len() == 1 {
        0
    } else {
        print!("[probe] multiple candidates - pick index: ");
        std::io::stdout().flush().ok();
        let mut input = String::new();
        std::io::stdin().read_line(&mut input).ok();
        input.trim().parse::<usize>().unwrap_or(0).min(candidates.len() - 1)
    };
    let (peripheral, name) = candidates.into_iter().nth(chosen).unwrap();
    println!("[probe] connecting to {name}...");

    peripheral.connect().await.context("GATT connect failed")?;
    peripheral.discover_services().await.context("service discovery failed")?;

    let chars = peripheral.characteristics();
    let cmd_char = chars
        .iter()
        .find(|c| c.uuid == uuids::COMMAND_CHAR)
        .context("command characteristic not found - wrong firmware/UUIDs?")?
        .clone();
    let status_char = chars
        .iter()
        .find(|c| c.uuid == uuids::STATUS_CHAR)
        .context("status characteristic not found - wrong firmware/UUIDs?")?
        .clone();

    // Subscribe BEFORE any write, matching useBle.ts's connect ordering.
    // Both characteristics require encryption now (NimBLE bonding, forced
    // by the firmware's onConnect) - this is where the OS Bluetooth stack
    // transparently pairs if this adapter/lamp pair hasn't bonded before.
    peripheral.subscribe(&status_char).await.context("subscribe failed")?;
    println!("[probe] connected, subscribed to status notifications.");

    let (line_tx, mut line_rx) = mpsc::unbounded_channel::<String>();
    {
        let mut notifications = peripheral.notifications().await.context("notifications stream failed")?;
        let status_uuid = uuids::STATUS_CHAR;
        tokio::spawn(async move {
            let mut codec = LineCodec::new();
            while let Some(notif) = notifications.next().await {
                if notif.uuid != status_uuid {
                    continue;
                }
                for line in codec.feed(&notif.value) {
                    println!("< {line}");
                    if line_tx.send(line).is_err() {
                        return;
                    }
                }
            }
        });
    }

    match perform_handshake(&peripheral, &cmd_char, &mut line_rx).await? {
        HandshakeOutcome::Trusted => {
            println!("[probe] status snapshot complete - connection trusted.");
        }
        HandshakeOutcome::NotTrusted => {
            println!(
                "[probe] no STATUS| within the timeout - the lamp is probably holding \
                 the connection pending trust (outside its ~60s boot window, and nobody \
                 confirmed a physical-gesture prompt in time either) and will disconnect \
                 shortly. Re-pair by rebooting the lamp and re-running this probe within 60s, \
                 or by toggling its switch/poti within ~20s of connecting."
            );
        }
    }

    println!("\n[probe] interactive mode - type a command (e.g. \"on\", \"off\"), or \"quit\".");
    let mut stdin_lines = BufReader::new(tokio::io::stdin()).lines();
    loop {
        print!("> ");
        std::io::stdout().flush().ok();
        tokio::select! {
            input = stdin_lines.next_line() => {
                match input {
                    Ok(Some(cmd)) => {
                        let cmd = cmd.trim();
                        if cmd.is_empty() {
                            continue;
                        }
                        if cmd == "quit" {
                            break;
                        }
                        if let Err(e) = send_line(&peripheral, &cmd_char, cmd).await {
                            eprintln!("[probe] write failed: {e}");
                        }
                    }
                    _ => break,
                }
            }
        }
    }

    println!("[probe] disconnecting...");
    peripheral.disconnect().await.ok();
    Ok(())
}
