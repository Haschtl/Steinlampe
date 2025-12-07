import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';

export function HelpSection({ bleGuids, commands }: { bleGuids: { label: string; value: string }[]; commands: { cmd: string; desc: string }[] }) {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle>BLE GUIDs</CardTitle>
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
          <p className="text-sm text-muted">
            BLE-MIDI (optional): Service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, Char 7772E5DB-3868-4112-A1A9-F2669D106BF3 (RX-only).
          </p>
        </CardContent>
      </Card>
      <Card>
        <CardHeader>
          <CardTitle>Command Reference &amp; Links</CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <table className="w-full border-collapse text-sm">
            <tbody>
              {commands.map((c) => (
                <tr key={c.cmd} className="border-b border-border">
                  <td className="py-1 pr-2 font-mono text-accent">{c.cmd}</td>
                  <td className="py-1 text-muted">{c.desc}</td>
                </tr>
              ))}
            </tbody>
          </table>
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
