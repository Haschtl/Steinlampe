//! Reassembles newline-terminated text lines out of a stream of raw BLE
//! notification payloads. Mirrors `frontend/src/hooks/bleClient.ts`'s
//! `subscribeToLines`: a notification can arrive mid-line (the firmware
//! chunks long lines across several BLE notifications), so bytes are
//! buffered until a `\n` shows up, then each line is trimmed (which also
//! absorbs a trailing `\r`, so `\r\n` and bare `\n` both work) and empty
//! lines are dropped.

#[derive(Debug, Default)]
pub struct LineCodec {
    buffer: String,
}

impl LineCodec {
    pub fn new() -> Self {
        Self::default()
    }

    /// Feed raw bytes from one BLE notification. Returns any complete lines
    /// found, in order. Bytes that don't yet end in a newline are held back
    /// in the internal buffer for the next call.
    pub fn feed(&mut self, bytes: &[u8]) -> Vec<String> {
        self.buffer.push_str(&String::from_utf8_lossy(bytes));

        let mut lines = Vec::new();
        while let Some(idx) = self.buffer.find('\n') {
            let line: String = self.buffer.drain(..=idx).collect();
            let trimmed = line.trim();
            if !trimmed.is_empty() {
                lines.push(trimmed.to_string());
            }
        }
        lines
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn single_line_in_one_chunk() {
        let mut codec = LineCodec::new();
        assert_eq!(codec.feed(b"hello\n"), vec!["hello".to_string()]);
    }

    #[test]
    fn line_split_across_two_chunks() {
        let mut codec = LineCodec::new();
        assert_eq!(codec.feed(b"hel"), Vec::<String>::new());
        assert_eq!(codec.feed(b"lo\n"), vec!["hello".to_string()]);
    }

    #[test]
    fn multiple_lines_in_one_chunk() {
        let mut codec = LineCodec::new();
        assert_eq!(
            codec.feed(b"STATUS|a=1\nSTATUS1|b=2\n"),
            vec!["STATUS|a=1".to_string(), "STATUS1|b=2".to_string()]
        );
    }

    #[test]
    fn crlf_terminated() {
        let mut codec = LineCodec::new();
        assert_eq!(codec.feed(b"hello\r\n"), vec!["hello".to_string()]);
    }

    #[test]
    fn empty_lines_are_dropped() {
        let mut codec = LineCodec::new();
        assert_eq!(codec.feed(b"\n\n  \nok\n"), vec!["ok".to_string()]);
    }

    #[test]
    fn trailing_partial_segment_held_back() {
        let mut codec = LineCodec::new();
        assert_eq!(codec.feed(b"a\nb"), vec!["a".to_string()]);
        // "b" has no newline yet - nothing should have been emitted for it,
        // and it should still be there waiting on the next feed.
        assert_eq!(codec.feed(b"\n"), vec!["b".to_string()]);
    }

    #[test]
    fn long_line_split_across_many_small_chunks() {
        // Mirrors the real firmware chunking a long STATUS| line across
        // several small BLE notifications before the terminating newline.
        let mut codec = LineCodec::new();
        let full = "STATUS|".to_string() + &"x".repeat(50) + "|fw_ver=1.0.0\n";
        let mut got = Vec::new();
        for chunk in full.as_bytes().chunks(7) {
            got.extend(codec.feed(chunk));
        }
        assert_eq!(got.len(), 1);
        assert!(got[0].starts_with("STATUS|"));
        assert!(got[0].ends_with("fw_ver=1.0.0"));
    }
}
