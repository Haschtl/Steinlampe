import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Link2 } from 'lucide-react';
import { toast } from 'react-toastify';
import { Trans, useI18n } from '@/i18n';

const bleGuids = [
  { label: 'Service', value: 'd94d86d7-1eaf-47a4-9d1e-7a90bf34e66b' },
  { label: 'Command Char', value: '4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7' },
  { label: 'Status Char', value: 'c5ad78b6-9b77-4a96-9a42-8e6e9a40c123' },
];

const commands = [
  { cmd: 'on / off / toggle', desc: 'Switch lamp power' },
  { cmd: 'mode N / next / prev', desc: 'Select pattern' },
  { cmd: 'auto on/off', desc: 'Enable auto-cycle' },
  { cmd: 'quick <csv|default>', desc: 'Set quick-list' },
  { cmd: 'bri X / bri min/max X', desc: 'Brightness and limits' },
  { cmd: 'ramp <ms> / ramp on/off <ms>', desc: 'Ramp durations' },
  { cmd: 'ramp ease on|off <type> [pow]', desc: 'Ramp easing curve' },
  { cmd: 'pat scale X / pat fade on|off / pat fade amt X', desc: 'Pattern speed & fade' },
  { cmd: 'idleoff <min>', desc: 'Idle auto-off timer' },
  { cmd: 'wake [soft] [mode=n bri=x] <s>', desc: 'Wake fade' },
  { cmd: 'sleep <min> / sleep stop', desc: 'Sleep fade' },
  { cmd: 'sos [stop]', desc: 'SOS alert' },
  { cmd: 'notify ... / morse <text>', desc: 'Blink/morse' },
  { cmd: 'demo [s] / demo off', desc: 'Cycle quick-list with dwell' },
  { cmd: 'touch tune on off / touchdim on/off / touch hold ms', desc: 'Touch config' },
  { cmd: 'clap on/off / clap thr / cool / train', desc: 'Clap/audio control' },
  { cmd: 'presence_ble on/off/set/add/del/clear/thr/grace/auto on/auto off', desc: 'BLE phone-proximity presence auto on/off' },
  { cmd: 'radar on/off/dim/motion/offdist', desc: 'mmWave radar presence: touchless dimming, motion auto-on, distance auto-off' },
  { cmd: 'light on/off/calib/gain/clamp', desc: 'Ambient light (if built)' },
  { cmd: 'music sens / music smooth / music auto on|off|thr', desc: 'Music params (use patterns Music Direct/Beat)' },
  { cmd: 'poti on/off/alpha/delta/off/sample', desc: 'Brightness knob (if built)' },
  { cmd: 'push on/off/debounce/double/hold/step_ms/step', desc: 'Push button (if built)' },
  { cmd: 'custom v1,v2.. / custom step ms', desc: 'Custom pattern' },
  { cmd: 'profile save/load/show', desc: 'Scene profiles (pattern/brightness/ramp, no calibration)' },
  { cmd: 'cfg export / cfg import ...', desc: 'Backup/restore settings' },
  { cmd: 'status / status json', desc: 'Print status snapshot' },
  { cmd: 'factory / help', desc: 'Reset or list commands' },
];

const midiMapping = [
  { msg: 'CC 7', action: 'Set brightness 0–100%' },
  { msg: 'CC 20', action: 'Set pattern/mode 1–8 (scaled)' },
  { msg: 'Note 59 (B3)', action: 'Toggle lamp' },
  { msg: 'Note 60 (C4)', action: 'Previous pattern' },
  { msg: 'Note 62 (D4)', action: 'Next pattern' },
  { msg: 'Notes 70–77 (F#4..C5)', action: 'Select pattern/mode 1–8' },
];

export function HelpSection() {
  const { t } = useI18n();
  const midi = midiMapping;

  const copyConnectLink = async () => {
    const id = localStorage.getItem('ql-last-device-id');
    if (!id) {
      toast.info(t('help.connectLinkNoDevice', 'Connect to a lamp once to create a link.'));
      return;
    }
    const url = `${window.location.origin}${window.location.pathname}?bleId=${encodeURIComponent(id)}`;
    try {
      await navigator.clipboard.writeText(url);
      toast.success(t('help.connectLinkCopied', 'Connect link copied to clipboard.'));
    } catch {
      // Clipboard API is unavailable in insecure contexts; surface the URL so it can be copied manually.
      window.prompt(t('help.copyConnectLink', 'Copy connect link'), url);
    }
  };

  return (
    <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
      <Card>
        <CardHeader>
          <CardTitle>
            <Trans k="help.inputs">Inputs & Controls</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-3 text-sm text-muted">
          <div className="rounded-lg border border-border/60 bg-panel/70 p-3">
            <p className="text-text font-semibold">
              <Trans k="help.input.switch">Switch</Trans>
            </p>
            <p>
              <Trans k="help.input.switchDesc">
                Toggle ON/OFF. A short OFF→ON pulse advances Quick Modes. Use the sync button to match app ↔ hardware.
              </Trans>
            </p>
          </div>
          <div className="rounded-lg border border-border/60 bg-panel/70 p-3">
            <p className="text-text font-semibold">
              <Trans k="help.input.poti">Poti</Trans>
            </p>
            <p>
              <Trans k="help.input.potiDesc">
                Sets brightness directly; below the off-threshold the lamp turns off. Invert/calibrate in Hardware settings.
              </Trans>
            </p>
          </div>
          <div className="rounded-lg border border-border/60 bg-panel/70 p-3">
            <p className="text-text font-semibold">
              <Trans k="help.input.touch">Touch</Trans>
            </p>
            <p>
              <Trans k="help.input.touchDesc">
                Hold to dim up/down (auto reverses at min/max). Double-tap is detected for feedback; touch-dim can be toggled.
              </Trans>
            </p>
          </div>
          <div className="rounded-lg border border-border/60 bg-panel/70 p-3">
            <p className="text-text font-semibold">
              <Trans k="help.input.push">Push Button</Trans>
            </p>
            <p>
              <Trans k="help.input.pushDesc">
                Single click toggles the lamp; double click steps Quick Modes; hold to ramp brightness in steps.
              </Trans>
            </p>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>
            <Trans k="help.bleGuids">BLE GUIDs</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-2">
          <table className="w-full border-collapse text-sm">
            <tbody>
              {bleGuids.map((g) => (
                <tr key={g.label} className="border-b border-border">
                  <td className="py-1 pr-2 text-muted">{g.label}</td>
                  <td className="py-1 font-mono text-accent">{g.value}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <div className="rounded-xl border border-border/60 bg-panel/70 p-4 text-sm shadow-inner">
            <div className="flex items-center justify-between gap-3">
              <div>
                <p className="text-xs uppercase tracking-[0.08em] text-muted">
                  <Trans k="help.bleMidiOptional">BLE-MIDI (optional)</Trans>
                </p>
                <p className="text-text font-semibold">
                  <Trans k="help.receiveOnly">Receive-only</Trans>
                </p>
              </div>
              <span className="rounded-full bg-accent/10 px-3 py-1 text-xs font-semibold text-accent">RX</span>
            </div>
            <dl className="mt-3 grid gap-3 sm:grid-cols-2">
              <div className="rounded-lg border border-border/60 bg-bg/60 p-3">
                <dt className="text-xs uppercase tracking-[0.06em] text-muted">
                  <Trans k="help.serviceUuid">Service UUID</Trans>
                </dt>
                <dd className="mt-1 font-mono text-accent break-all">03B80E5A-EDE8-4B33-A751-6CE34EC4C700</dd>
              </div>
              <div className="rounded-lg border border-border/60 bg-bg/60 p-3">
                <dt className="text-xs uppercase tracking-[0.06em] text-muted">
                  <Trans k="help.charUuid">Characteristic UUID</Trans>
                </dt>
                <dd className="mt-1 font-mono text-accent break-all">7772E5DB-3868-4112-A1A9-F2669D106BF3</dd>
              </div>
            </dl>
            <p className="mt-3 text-xs text-muted">
              <Trans k="help.bleMidiNote">Standard BLE-MIDI UUIDs; enable with the optional BLE-MIDI flag. Write/WriteNR RX-only.</Trans>
            </p>
          </div>
          <div className="rounded-xl border border-border/60 bg-panel/70 p-4 text-sm shadow-inner">
            <p className="text-text font-semibold">
              <Trans k="help.connectLink">Direct connect link</Trans>
            </p>
            <p className="mt-1 text-xs text-muted">
              <Trans k="help.connectLinkDesc">
                Creates a link with this lamp's id (?bleId=…). Opening it auto-connects to this lamp. Works only in this browser, where the lamp was paired before.
              </Trans>
            </p>
            <Button type="button" variant="ghost" size="sm" className="mt-3" onClick={copyConnectLink}>
              <Link2 className="mr-2 h-4 w-4" />
              <Trans k="help.copyConnectLink">Copy connect link</Trans>
            </Button>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>
            <Trans k="help.commandsLinks">Command Reference</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <table className="w-full table-fixed border-collapse text-sm">
            <tbody>
              {commands.map((c) => (
                <tr key={c.cmd} className="border-b border-border align-top">
                  <td className="w-40 py-1 pr-2 font-mono text-accent break-words whitespace-normal">{c.cmd}</td>
                  <td className="py-1 text-muted break-words whitespace-normal">{c.desc}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>
            <Trans k="help.midiMap">MIDI Mapping (info)</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-2">
          {midi && midi.length > 0 ? (
            <table className="w-full table-fixed border-collapse text-sm">
              <tbody>
                {midi.map((m) => (
                  <tr key={m.msg} className="border-b border-border/60 align-top">
                    <td className="w-36 py-1 pr-2 font-mono text-accent break-words whitespace-normal">{m.msg}</td>
                    <td className="py-1 text-muted break-words whitespace-normal">{m.action}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          ) : (
            <p className="text-sm text-muted">No MIDI mapping available.</p>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Links</CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex flex-wrap gap-2">
            <a className="pill" href="https://play.google.com/store/apps/details?id=net.dinglisch.android.taskerm" target="_blank" rel="noopener noreferrer">
              Tasker (Play Store)
            </a>
            <a className="pill" href="https://github.com/Haschtl/Tasker-Ble-Writer/actions" target="_blank" rel="noopener noreferrer">
              Tasker-BLE-Writer Actions
            </a>
            <a className="pill" href="https://github.com/Haschtl/Steinlampe/actions/workflows/ci.yml" target="_blank" rel="noopener noreferrer">
              Firmware Build (Actions)
            </a>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>
            <Trans k="help.desktopApp">Desktop App (Beta)</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-3 text-sm">
          <p className="text-muted">
            <Trans k="help.desktopAppDesc">
              A tray/menu-bar app that controls all your lamps from one place and can automate them (e.g. turn one on
              while your microphone is active). Still early and unsigned - expect a SmartScreen/Gatekeeper warning.
            </Trans>
          </p>
          <p className="text-xs text-muted">
            <Trans k="help.desktopAppPlatforms">Linux: built and tested against real hardware. Windows/macOS: builds, but unverified.</Trans>
          </p>
          <div className="flex flex-wrap gap-2">
            <a className="pill" href="https://github.com/Haschtl/Steinlampe/releases/tag/desktop-latest" target="_blank" rel="noopener noreferrer">
              <Trans k="help.desktopAppDownload">Download latest build</Trans>
            </a>
            <a className="pill" href="https://github.com/Haschtl/Steinlampe/actions/workflows/desktop-build.yml" target="_blank" rel="noopener noreferrer">
              Desktop App Build (Actions)
            </a>
          </div>
        </CardContent>
      </Card>
      <Card className="md:col-span-2 xl:col-span-1">
        <CardHeader>
          <CardTitle>
            <Trans k="help.secureBoot">Secure Boot / Reset</Trans>
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-2 text-sm">
          <p>
            <Trans k="help.secureBootDesc">
              During the first second after power-up the lamp stays dark. Toggle the hardware switch twice in that window to factory-reset and start at 20% brightness.
            </Trans>
          </p>
          <p className="text-muted">
            <Trans k="help.secureBootNote">
              No filters/patterns run until the window closes; use this gesture if the device is wedged or misconfigured.
            </Trans>
          </p>
          <p className="text-muted">
            <Trans k="help.btPairingHint">
              Classic BT-Serial pairing: connect from the host, then confirm within ~20s by toggling the switch or moving the potentiometer. The device will be trusted afterwards.
            </Trans>
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
