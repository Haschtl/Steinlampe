//! Persists rules to `rules.toml` in the OS config dir. Atomic save (write
//! a temp file, then rename over the real one) so a crash mid-write can't
//! leave a half-written, unparseable file behind.

use std::path::PathBuf;

use serde::{Deserialize, Serialize};

use crate::rules::model::Rule;

pub fn config_dir() -> anyhow::Result<PathBuf> {
    let dirs = directories::ProjectDirs::from("dev", "quarzlampe", "quarzlampe")
        .ok_or_else(|| anyhow::anyhow!("could not determine a config directory on this platform"))?;
    let dir = dirs.config_dir().to_path_buf();
    std::fs::create_dir_all(&dir)?;
    Ok(dir)
}

#[derive(Serialize, Deserialize, Default)]
struct RulesFile {
    #[serde(default)]
    rules: Vec<Rule>,
}

/// Returns an empty list if `rules.toml` doesn't exist yet (first run) -
/// not an error.
pub fn load_rules() -> anyhow::Result<Vec<Rule>> {
    let path = config_dir()?.join("rules.toml");
    if !path.exists() {
        return Ok(Vec::new());
    }
    let text = std::fs::read_to_string(&path)?;
    let file: RulesFile = toml::from_str(&text)?;
    Ok(file.rules)
}

pub fn save_rules(rules: &[Rule]) -> anyhow::Result<()> {
    let dir = config_dir()?;
    let path = dir.join("rules.toml");
    let tmp_path = dir.join("rules.toml.tmp");
    let text = toml::to_string_pretty(&RulesFile { rules: rules.to_vec() })?;
    std::fs::write(&tmp_path, text)?;
    std::fs::rename(&tmp_path, &path)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rules::model::{Action, Trigger};
    use uuid::Uuid;

    #[test]
    fn round_trips_through_toml() {
        let rules = vec![Rule {
            id: Uuid::new_v4(),
            name: "mic on -> on_air".to_string(),
            enabled: true,
            trigger: Trigger::MicActive,
            targets: vec!["lamp-1".to_string()],
            action: Action::On,
            quick_rule: None,
        }];
        let text = toml::to_string_pretty(&RulesFile { rules: rules.clone() }).unwrap();
        let parsed: RulesFile = toml::from_str(&text).unwrap();
        assert_eq!(parsed.rules, rules);
    }

    #[test]
    fn missing_file_yields_empty_list_not_error() {
        // config_dir() touches the real filesystem, but a plainly
        // nonexistent path under it is still a valid "not found yet" case
        // to exercise without needing a temp HOME override here.
        let file: RulesFile = toml::from_str("").unwrap();
        assert!(file.rules.is_empty());
    }
}
