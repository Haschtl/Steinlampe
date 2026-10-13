//! Terminal-only exerciser for `core::audio` - prints the RMS loudness of
//! whatever the system is currently playing, a few times a second, so the
//! loopback-capture plumbing can be sanity-checked against real audio
//! (play something, pause it) without the full daemon/visualizer running.

use std::time::Duration;

use quarzlampe_core::audio;

fn main() {
    let mut provider = audio::default_provider();
    println!("polling system-audio loudness every 200ms - play/pause something to see it change (Ctrl+C to quit)");
    loop {
        match provider.poll() {
            Ok(Some(rms)) => println!("rms = {rms:.4}"),
            Ok(None) => println!("(no new samples)"),
            Err(e) => println!("error: {e}"),
        }
        std::thread::sleep(Duration::from_millis(200));
    }
}
