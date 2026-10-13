//! The on-demand console UI: a perfectly ordinary eframe app, spawned by the
//! tray daemon (`quarzlampe-tray`) when the user wants it and left to exit
//! normally when its window is closed - no hide-to-tray trick, no invisible
//! anchor viewport. The daemon is what stays running; this process doesn't
//! need to.

use eframe::egui;

struct ConsoleApp;

impl eframe::App for ConsoleApp {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        egui::Frame::central_panel(&egui::Style::default()).show(ui, |ui| {
            ui.heading("Quarzlampe console");
            ui.label("Spawned by the tray daemon. Closing this window just exits this process.");
            ui.label("Lamp list / controls land here in a later milestone.");
        });
    }
}

fn main() -> eframe::Result<()> {
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([480.0, 300.0])
            .with_title("Quarzlampe"),
        ..Default::default()
    };
    eframe::run_native(
        "Quarzlampe",
        native_options,
        Box::new(|_cc| Ok(Box::new(ConsoleApp))),
    )
}
