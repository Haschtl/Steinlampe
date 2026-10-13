//! Single source of truth for `Action` -> firmware command-line syntax (see
//! `src/command.cpp` for the authoritative command set). Both the console's
//! manual quick-actions and the future rule engine (M4) call this - never
//! build these strings ad hoc elsewhere.

use crate::rules::model::Action;

pub fn action_to_command(action: &Action) -> String {
    match action {
        Action::On => "on".to_string(),
        Action::Off => "off".to_string(),
        Action::SetBrightness { pct } => format!("bri {pct}"),
        Action::SetMode { pattern } => format!("mode {pattern}"),
        Action::LoadProfile { slot } => format!("profile load {slot}"),
        Action::Notify { pattern_ms, fade_ms } => {
            let mut parts = vec!["notify".to_string()];
            if let Some(ms) = pattern_ms {
                parts.extend(ms.iter().map(|v| v.to_string()));
            }
            if let Some(fade) = fade_ms {
                parts.push(format!("fade={fade}"));
            }
            parts.join(" ")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn simple_actions() {
        assert_eq!(action_to_command(&Action::On), "on");
        assert_eq!(action_to_command(&Action::Off), "off");
        assert_eq!(action_to_command(&Action::SetBrightness { pct: 55 }), "bri 55");
        assert_eq!(action_to_command(&Action::SetMode { pattern: 3 }), "mode 3");
        assert_eq!(action_to_command(&Action::LoadProfile { slot: 2 }), "profile load 2");
    }

    #[test]
    fn notify_default() {
        assert_eq!(action_to_command(&Action::Notify { pattern_ms: None, fade_ms: None }), "notify");
    }

    #[test]
    fn notify_with_pattern_and_fade() {
        let action = Action::Notify { pattern_ms: Some(vec![120, 60, 120]), fade_ms: Some(200) };
        assert_eq!(action_to_command(&action), "notify 120 60 120 fade=200");
    }
}
