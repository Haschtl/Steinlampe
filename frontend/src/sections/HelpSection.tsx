import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans } from '@/i18n';

type HelpProps = {
  bleGuids: { label: string; value: string }[];
  commands: { cmd: string; desc: string }[];
  midi?: { msg: string; action: string }[];
};

export function HelpSection({ bleGuids, commands, midi }: HelpProps) {
  return (
    <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
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
          <CardTitle>
            <Trans k="help.commandsLinks">Links</Trans>
          </CardTitle>
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
    </div>
  );
}
