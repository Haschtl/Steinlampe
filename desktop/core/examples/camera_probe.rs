//! Terminal-only exerciser for `core::camera`, mirroring `mic_probe.rs` -
//! polls the platform's camera-activity provider once a second so the
//! `/proc`-scan detection can be sanity-checked against a real webcam
//! open/close without needing the full daemon running.

use std::time::Duration;

use quarzlampe_core::camera;

fn main() {
    let mut provider = camera::default_provider();
    println!("polling camera activity every 1s - open/close a webcam app to see it change (Ctrl+C to quit)");
    loop {
        match provider.poll() {
            Ok(camera::CameraActivity::Active) => println!("ACTIVE"),
            Ok(camera::CameraActivity::Inactive) => println!("inactive"),
            Err(e) => println!("error: {e}"),
        }
        std::thread::sleep(Duration::from_secs(1));
    }
}
