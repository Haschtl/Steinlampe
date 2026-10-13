//! Parses the firmware's lightweight `STATE|...` push line into the fields
//! the console needs live (on/off, brightness, pattern). Mirrors (a small
//! subset of) `frontend/src/hooks/status.ts`'s `parseStatusLine` - v1 only
//! needs on/off/brightness/pattern, not the full structured snapshot (see
//! the "Live state" note in PLAN.md's protocol-facts section for why that's
//! a deliberate scope choice).
//!
//! Example line (from `src/print.cpp`'s `emitLiveState`):
//! `STATE|lamp=ON|bri=55.0|pattern=3|out=52.1|poti=N/A`

use std::time::Instant;

#[derive(Clone, Debug, Default, PartialEq)]
pub enum ConnectionState {
    #[default]
    Disconnected,
    Connecting,
    /// Connection is up at the GATT level, but the lamp hasn't trusted it
    /// yet - see `handshake::HandshakeOutcome::PendingToken`. The firmware
    /// will likely disconnect on its own ~8s timeout if no valid token
    /// arrives.
    AwaitingTrustToken,
    Connected,
    Reconnecting,
    /// Gave up after `lamp_actor::MAX_CONSECUTIVE_FAILURES` connection
    /// attempts in a row failed - the actor is idle, waiting for an
    /// explicit `LampCommand::Reconnect` instead of retrying itself on a
    /// timer. Distinct from `Disconnected` so the UI can show a manual
    /// "Reconnect" action specifically here, not on every disconnected
    /// lamp (e.g. one the user disconnected on purpose).
    ReconnectFailed,
}

#[derive(Clone, Debug, Default)]
pub struct LampState {
    pub connection: ConnectionState,
    pub lamp_on: Option<bool>,
    pub brightness_pct: Option<f32>,
    pub pattern: Option<u32>,
    pub last_state_at: Option<Instant>,
    pub last_error: Option<String>,
}

/// Parses a `STATE|...` line's key=value pairs into the fields `LampState`
/// cares about. Returns `None` if `line` isn't a `STATE|` line at all (so
/// the caller can tell "not this kind of line" apart from "a STATE| line
/// with fields we don't recognize", which just yields a mostly-empty
/// `StateUpdate`).
pub struct StateUpdate {
    pub lamp_on: Option<bool>,
    pub brightness_pct: Option<f32>,
    pub pattern: Option<u32>,
}

pub fn parse_state_line(line: &str) -> Option<StateUpdate> {
    let rest = line.strip_prefix("STATE|")?;
    let mut update = StateUpdate { lamp_on: None, brightness_pct: None, pattern: None };
    for field in rest.split('|') {
        let Some((key, value)) = field.split_once('=') else { continue };
        match key {
            "lamp" => update.lamp_on = Some(value == "ON"),
            "bri" => update.brightness_pct = value.parse().ok(),
            "pattern" => update.pattern = value.parse().ok(),
            _ => {}
        }
    }
    Some(update)
}

impl LampState {
    pub fn apply_state_update(&mut self, update: StateUpdate) {
        if update.lamp_on.is_some() {
            self.lamp_on = update.lamp_on;
        }
        if update.brightness_pct.is_some() {
            self.brightness_pct = update.brightness_pct;
        }
        if update.pattern.is_some() {
            self.pattern = update.pattern;
        }
        self.last_state_at = Some(Instant::now());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_full_state_line() {
        let update = parse_state_line("STATE|lamp=ON|bri=55.0|pattern=3|out=52.1|poti=N/A").unwrap();
        assert_eq!(update.lamp_on, Some(true));
        assert_eq!(update.brightness_pct, Some(55.0));
        assert_eq!(update.pattern, Some(3));
    }

    #[test]
    fn parses_lamp_off() {
        let update = parse_state_line("STATE|lamp=OFF|bri=0.0|pattern=1|out=0.0").unwrap();
        assert_eq!(update.lamp_on, Some(false));
    }

    #[test]
    fn ignores_unknown_fields() {
        let update = parse_state_line("STATE|lamp=ON|poti=N/A|poti_raw=1234").unwrap();
        assert_eq!(update.lamp_on, Some(true));
        assert_eq!(update.brightness_pct, None);
    }

    #[test]
    fn non_state_line_returns_none() {
        assert!(parse_state_line("STATUS|pattern=1|...").is_none());
        assert!(parse_state_line("TRUST_TOKEN|abc123").is_none());
    }

    #[test]
    fn apply_merges_without_clobbering_unset_fields() {
        let mut state = LampState::default();
        state.apply_state_update(StateUpdate { lamp_on: Some(true), brightness_pct: Some(80.0), pattern: Some(2) });
        assert_eq!(state.lamp_on, Some(true));
        // A later update that only reports lamp_on shouldn't blank out the
        // brightness/pattern we already know.
        state.apply_state_update(StateUpdate { lamp_on: Some(false), brightness_pct: None, pattern: None });
        assert_eq!(state.lamp_on, Some(false));
        assert_eq!(state.brightness_pct, Some(80.0));
        assert_eq!(state.pattern, Some(2));
    }
}
