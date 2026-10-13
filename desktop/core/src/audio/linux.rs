//! Linux system-audio loudness capture via PulseAudio's (or PipeWire's
//! pulse-compat shim's) *monitor source* - the same "listen to whatever a
//! sink is currently playing" mechanism `parec`/`pavucontrol` expose,
//! reached here directly through `libpulse-binding` rather than shelling
//! out to `parec`. This is deliberately a *recording* stream (`Stream`,
//! not the plain introspection calls `core::mic::linux` uses) - connected
//! to `"<default_sink_name>.monitor"`, not a real input device, so nothing
//! the user says into an actual microphone ever reaches this code path.

use std::cell::RefCell;
use std::ops::Deref;
use std::rc::Rc;

use libpulse_binding::context::{Context, FlagSet, State as ContextState};
use libpulse_binding::mainloop::standard::{IterateResult, Mainloop};
use libpulse_binding::proplist::{properties, Proplist};
use libpulse_binding::sample::{Format, Spec};
use libpulse_binding::stream::{PeekResult, State as StreamState, Stream};

use super::LoudnessProvider;

struct Connection {
    mainloop: Rc<RefCell<Mainloop>>,
    context: Rc<RefCell<Context>>,
}

impl Connection {
    fn connect() -> Result<Self, String> {
        let mut proplist = Proplist::new().ok_or("failed to create proplist")?;
        proplist
            .set_str(properties::APPLICATION_NAME, "Quarzlampe")
            .map_err(|()| "failed to set application name".to_string())?;

        let mainloop = Mainloop::new().ok_or("failed to create mainloop")?;
        let mainloop = Rc::new(RefCell::new(mainloop));

        let context = Context::new_with_proplist(mainloop.borrow().deref(), "quarzlampe-visualizer", &proplist)
            .ok_or("failed to create context")?;
        let context = Rc::new(RefCell::new(context));

        context
            .borrow_mut()
            .connect(None, FlagSet::NOFLAGS, None)
            .map_err(|e| format!("failed to connect: {e}"))?;

        let conn = Self { mainloop, context };
        loop {
            conn.iterate_once()?;
            match conn.context.borrow().get_state() {
                ContextState::Ready => break,
                ContextState::Failed | ContextState::Terminated => {
                    return Err("PulseAudio context failed/terminated while connecting".to_string());
                }
                _ => {}
            }
        }
        Ok(conn)
    }

    fn iterate_once(&self) -> Result<(), String> {
        match self.mainloop.borrow_mut().iterate(false) {
            IterateResult::Err(e) => Err(format!("mainloop error: {e}")),
            IterateResult::Quit(_) => Err("mainloop quit".to_string()),
            IterateResult::Success(_) => Ok(()),
        }
    }

    /// The sink currently playing audio - whatever it's outputting is what
    /// gets captured, via its `.monitor` source.
    fn default_sink_name(&self) -> Result<String, String> {
        let name = Rc::new(RefCell::new(None));
        let found = Rc::clone(&name);
        let op = self.context.borrow().introspect().get_server_info(move |info| {
            *found.borrow_mut() = info.default_sink_name.as_ref().map(|s| s.to_string());
        });
        loop {
            self.iterate_once()?;
            match op.get_state() {
                libpulse_binding::operation::State::Done => break,
                libpulse_binding::operation::State::Running => {}
                libpulse_binding::operation::State::Cancelled => {
                    return Err("get_server_info was cancelled".to_string());
                }
            }
        }
        name.borrow().clone().ok_or_else(|| "PulseAudio reported no default sink".to_string())
    }
}

#[derive(Default)]
pub struct PulseLoudnessProvider {
    connection: Option<Connection>,
    stream: Option<Stream>,
}

impl PulseLoudnessProvider {
    pub fn new() -> Self {
        Self::default()
    }

    fn ensure_stream(&mut self) -> Result<(), String> {
        if self.stream.is_some() {
            return Ok(());
        }
        if self.connection.is_none() {
            self.connection = Some(Connection::connect()?);
        }
        let connection = self.connection.as_ref().expect("just ensured Some above");
        let monitor_name = format!("{}.monitor", connection.default_sink_name()?);
        let spec = Spec { format: Format::F32le, rate: 44100, channels: 1 };
        if !spec.is_valid() {
            return Err("invalid sample spec".to_string());
        }
        let mut stream = Stream::new(&mut connection.context.borrow_mut(), "quarzlampe-visualizer", &spec, None)
            .ok_or("failed to create recording stream")?;
        stream
            .connect_record(Some(&monitor_name), None, libpulse_binding::stream::FlagSet::NOFLAGS)
            .map_err(|e| format!("failed to connect recording stream to {monitor_name}: {e}"))?;
        loop {
            connection.iterate_once()?;
            match stream.get_state() {
                StreamState::Ready => break,
                StreamState::Failed | StreamState::Terminated => {
                    return Err("recording stream failed/terminated while connecting".to_string());
                }
                _ => {}
            }
        }
        self.stream = Some(stream);
        Ok(())
    }

    /// Drains every buffered fragment (there may be more than one since
    /// the last poll - a record stream keeps filling server-side between
    /// calls) and returns the RMS of all the `F32le` samples found, so a
    /// slow poller still sees a correct average rather than only the
    /// latest fragment.
    fn drain_rms(stream: &mut Stream) -> Result<Option<f32>, String> {
        let mut sum_sq = 0.0f64;
        let mut count = 0usize;
        loop {
            match stream.peek() {
                Ok(PeekResult::Empty) => break,
                Ok(PeekResult::Hole(_)) => {
                    let _ = stream.discard();
                }
                Ok(PeekResult::Data(bytes)) => {
                    for chunk in bytes.as_chunks::<4>().0 {
                        let sample = f32::from_le_bytes(*chunk);
                        sum_sq += f64::from(sample) * f64::from(sample);
                        count += 1;
                    }
                    let _ = stream.discard();
                }
                Err(e) => return Err(format!("stream peek failed: {e}")),
            }
        }
        if count == 0 {
            Ok(None)
        } else {
            Ok(Some(((sum_sq / count as f64).sqrt()) as f32))
        }
    }
}

impl LoudnessProvider for PulseLoudnessProvider {
    fn poll(&mut self) -> Result<Option<f32>, String> {
        if let Err(e) = self.ensure_stream() {
            // Connection/stream setup may be stale (e.g. default sink
            // changed, PulseAudio/PipeWire restarted) - drop everything
            // so the next poll builds it fresh instead of failing forever.
            self.connection = None;
            self.stream = None;
            return Err(e);
        }
        let connection = self.connection.as_ref().expect("ensure_stream just set this");
        connection.iterate_once()?;
        let stream = self.stream.as_mut().expect("ensure_stream just set this");
        Self::drain_rms(stream)
    }
}
