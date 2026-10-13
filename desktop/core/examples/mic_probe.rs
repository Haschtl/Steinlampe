//! Terminal-only exerciser for `core::mic`, mirroring `probe.rs`'s role for
//! `core::ble` - polls the platform's mic-activity provider once a second
//! and prints what it sees, so the detection logic can be sanity-checked
//! against a real microphone capture (`arecord`, a video call, etc.)
//! without needing the full daemon running.

use std::time::Duration;

use quarzlampe_core::mic;

fn main() {
    let mut provider = mic::default_provider();
    println!("polling mic activity every 1s - start/stop a recording to see it change (Ctrl+C to quit)");
    loop {
        match provider.poll() {
            Ok(mic::MicActivity::Active) => println!("ACTIVE"),
            Ok(mic::MicActivity::Inactive) => println!("inactive"),
            Err(e) => println!("error: {e}"),
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}
