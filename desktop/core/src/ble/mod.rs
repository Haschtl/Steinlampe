pub mod handshake;
pub mod lamp_actor;
pub mod line_codec;
pub mod manager;
pub mod uuids;

pub use handshake::{perform_handshake, send_line, HandshakeOutcome};
pub use lamp_actor::LampCommand;
pub use line_codec::LineCodec;
pub use manager::{spawn_bluetooth_adapter_watch, DiscoveredLamp, LampHandle, LampManager};
