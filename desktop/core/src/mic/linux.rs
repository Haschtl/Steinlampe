//! Linux mic-active detection via PulseAudio's (or PipeWire's pulse-compat
//! shim's) client API: a source output with `corked == false` is some
//! process actively recording from an input device right now - that's
//! "the mic is active" for this app's purposes, matching PLAN.md's
//! "poll `list_source_outputs()` for non-corked entries" note.
//!
//! Talks to `libpulse-binding` directly rather than through the obvious
//! higher-level wrapper (`pulsectl-rs`), which is GPL-3.0-or-later - the
//! wrong license to pull into a binary this project is about to offer for
//! download. `libpulse-binding` itself is MIT/Apache-2.0 (PulseAudio's
//! *client* library, which it binds, is LGPL - only the server is GPL), so
//! this stays clean. The connect/poll pattern below is the same minimal
//! one any PulseAudio client needs; it's just not worth a dependency.

use std::cell::RefCell;
use std::ops::Deref;
use std::rc::Rc;

use libpulse_binding::callbacks::ListResult;
use libpulse_binding::context::introspect::Introspector;
use libpulse_binding::context::{Context, FlagSet, State as ContextState};
use libpulse_binding::mainloop::standard::{IterateResult, Mainloop};
use libpulse_binding::operation::{Operation, State as OperationState};
use libpulse_binding::proplist::{properties, Proplist};

use super::{MicActivity, MicActivityProvider};

/// A live connection, kept across polls - reconnecting every ~300ms would
/// be needlessly slow. Dropped and rebuilt on the next poll if a call ever
/// fails (e.g. PipeWire/PulseAudio restarted). Holds `Rc`s (as every
/// libpulse-binding example does) so it - and therefore
/// `PulseMicProvider` - is deliberately not `Send`; see `mic::mod`'s doc
/// comment for why that's required, not just tolerated.
struct Connection {
    mainloop: Rc<RefCell<Mainloop>>,
    context: Rc<RefCell<Context>>,
    introspect: Introspector,
}

impl Connection {
    fn connect() -> Result<Self, String> {
        let mut proplist = Proplist::new().ok_or("failed to create proplist")?;
        proplist.set_str(properties::APPLICATION_NAME, "Quarzlampe").map_err(|()| "failed to set application name".to_string())?;

        let mainloop = Mainloop::new().ok_or("failed to create mainloop")?;
        let mainloop = Rc::new(RefCell::new(mainloop));

        let context = Context::new_with_proplist(mainloop.borrow().deref(), "quarzlampe-mic", &proplist)
            .ok_or("failed to create context")?;
        let context = Rc::new(RefCell::new(context));

        context
            .borrow_mut()
            .connect(None, FlagSet::NOFLAGS, None)
            .map_err(|e| format!("failed to connect to PulseAudio: {e}"))?;

        loop {
            match mainloop.borrow_mut().iterate(false) {
                IterateResult::Err(e) => return Err(format!("mainloop error while connecting: {e}")),
                IterateResult::Quit(_) => return Err("mainloop quit while connecting".to_string()),
                IterateResult::Success(_) => {}
            }
            match context.borrow().get_state() {
                ContextState::Ready => break,
                ContextState::Failed | ContextState::Terminated => {
                    return Err("PulseAudio context failed or terminated while connecting".to_string());
                }
                _ => {}
            }
        }

        let introspect = context.borrow().introspect();
        Ok(Self { mainloop, context, introspect })
    }

    fn wait<G: ?Sized>(&self, op: Operation<G>) -> Result<(), String> {
        loop {
            match self.mainloop.borrow_mut().iterate(false) {
                IterateResult::Err(e) => return Err(format!("mainloop error: {e}")),
                IterateResult::Quit(_) => return Err("mainloop quit while waiting for an operation".to_string()),
                IterateResult::Success(_) => {}
            }
            match op.get_state() {
                OperationState::Done => return Ok(()),
                OperationState::Running => {}
                OperationState::Cancelled => return Err("PulseAudio operation was cancelled".to_string()),
            }
        }
    }

    fn any_source_output_uncorked(&self) -> Result<bool, String> {
        let any_uncorked = Rc::new(RefCell::new(false));
        let found = Rc::clone(&any_uncorked);
        let op = self.introspect.get_source_output_info_list(move |result| {
            if let ListResult::Item(item) = result
                && !item.corked
            {
                *found.borrow_mut() = true;
            }
        });
        self.wait(op)?;
        Ok(*any_uncorked.borrow())
    }
}

impl Drop for Connection {
    fn drop(&mut self) {
        self.context.borrow_mut().disconnect();
    }
}

#[derive(Default)]
pub struct PulseMicProvider {
    connection: Option<Connection>,
}

impl PulseMicProvider {
    pub fn new() -> Self {
        Self::default()
    }
}

impl MicActivityProvider for PulseMicProvider {
    fn poll(&mut self) -> Result<MicActivity, String> {
        if self.connection.is_none() {
            self.connection = Some(Connection::connect()?);
        }
        let connection = self.connection.as_ref().expect("just ensured Some above");
        match connection.any_source_output_uncorked() {
            Ok(true) => Ok(MicActivity::Active),
            Ok(false) => Ok(MicActivity::Inactive),
            Err(e) => {
                // The connection may be stale - drop it so the *next* poll
                // reconnects instead of failing forever.
                self.connection = None;
                Err(e)
            }
        }
    }
}
