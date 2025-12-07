import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';

const bleGuids = [
  { label: 'Service', value: 'd94d86d7-1eaf-47a4-9d1e-7a90bf34e66b' },
  { label: 'Command Char', value: '4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7' },
  { label: 'Status Char', value: 'c5ad78b6-9b77-4a96-9a42-8e6e9a40c123' },
];

const commands = [
  { cmd: 'on / off / toggle', desc: 'Switch lamp power' },
  { cmd: 'mode N / next / prev', desc: 'Select pattern' },
  { cmd: 'bri X', desc: 'Brightness 0..100%' },
  { cmd: 'bri cap X', desc: 'Brightness cap (%)' },
  { cmd: 'ramp on/off <ms>', desc: 'Set ramp durations' },
  { cmd: 'wake [soft] <s>', desc: 'Wake fade' },
  { cmd: 'sleep <min>', desc: 'Sleep fade' },
  { cmd: 'notify ...', desc: 'Blink sequence' },
  { cmd: 'morse <text>', desc: 'Morse blink' },
  { cmd: 'profile save/load', desc: 'User profiles' },
];

export default function App() {
  return (
    <div className="min-h-screen bg-bg text-text">
      <header className="sticky top-0 z-10 bg-gradient-to-br from-[#111a2d] via-[#0b0f1a] to-[#0b0f1a] px-4 py-3 shadow-lg shadow-black/40">
        <div className="mx-auto flex max-w-6xl items-center gap-3">
          <img src="/icon-lamp.svg" alt="Lamp Icon" className="h-10 w-10 rounded-lg border border-border bg-[#0b0f1a] p-1.5" />
          <div className="flex flex-1 items-center gap-3">
            <h1 className="text-xl font-semibold tracking-wide">Quarzlampe Control</h1>
            <div className="flex flex-wrap gap-2">
              <Button variant="primary">Connect</Button>
              <Button>Connect Serial</Button>
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" defaultChecked /> Auto-reconnect
              </label>
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <span className="pill text-accent2">Status: Not connected</span>
            <span className="pill">Last status: --</span>
          </div>
        </div>
      </header>

      <main className="mx-auto flex max-w-6xl flex-col gap-4 px-4 py-4">
        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Lamp &amp; Ramps</CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="flex flex-wrap items-center gap-3">
                <span className="chip-muted">Switch: --</span>
                <span className="chip-muted">Touch: --</span>
                <Button size="sm">Sync</Button>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <div className="space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-sm text-muted">Brightness</span>
                    <Input type="number" defaultValue={70} className="w-24 text-right" />
                  </div>
                  <input type="range" min="1" max="100" defaultValue="70" className="w-full accent-accent" />
                  <div className="flex gap-2">
                    <Button>Prev</Button>
                    <select className="input">
                      <option>1 - Konstant</option>
                      <option>2 - Atmung</option>
                    </select>
                    <Button>Next</Button>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    <label className="pill cursor-pointer">
                      <input type="checkbox" className="accent-accent" /> AutoCycle
                    </label>
                    <label className="pill cursor-pointer">
                      <input type="checkbox" className="accent-accent" /> Pattern Fade
                    </label>
                  </div>
                </div>
                <div className="space-y-2">
                  <Label>Profile</Label>
                  <div className="flex gap-2">
                    <select className="input">
                      <option>1</option>
                      <option>2</option>
                    </select>
                    <Button>Load</Button>
                    <Button>Save</Button>
                  </div>
                </div>
              </div>

              <div className="grid gap-3 md:grid-cols-2">
                <Card className="p-3">
                  <CardTitle className="text-base text-text">Ramp On</CardTitle>
                  <div className="space-y-2">
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Ease</Label>
                      <select className="input">
                        <option>linear</option>
                        <option>ease</option>
                        <option>ease-in</option>
                        <option>ease-out</option>
                        <option>ease-in-out</option>
                      </select>
                    </div>
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Power</Label>
                      <Input type="number" min="0.01" max="10" step="0.01" defaultValue={2} className="w-24" />
                    </div>
                  </div>
                </Card>
                <Card className="p-3">
                  <CardTitle className="text-base text-text">Ramp Off</CardTitle>
                  <div className="space-y-2">
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Ease</Label>
                      <select className="input">
                        <option>linear</option>
                        <option>ease</option>
                        <option>ease-in</option>
                        <option>ease-out</option>
                        <option>ease-in-out</option>
                      </select>
                    </div>
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Power</Label>
                      <Input type="number" min="0.01" max="10" step="0.01" defaultValue={2} className="w-24" />
                    </div>
                  </div>
                </Card>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Notify</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="grid gap-2 md:grid-cols-[1fr_auto]">
                <div className="space-y-1">
                  <Label>Sequence (ms)</Label>
                  <Input placeholder="80 40 80 120" />
                </div>
                <div className="flex items-end gap-2">
                  <Button variant="primary">Notify</Button>
                  <Button variant="danger">Stop</Button>
                </div>
              </div>
              <div className="flex flex-wrap gap-2">
                {['Short blink', 'Soft alert', 'Triple pulse', 'Doorbell', 'Long Fade Alert', 'Double 60', 'SOS'].map((p) => (
                  <Button key={p}>{p}</Button>
                ))}
              </div>
            </CardContent>
          </Card>
        </div>

        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Wake</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="grid gap-3 md:grid-cols-2">
                <div>
                  <Label>Duration (s)</Label>
                  <Input type="number" defaultValue={180} />
                </div>
                <div>
                  <Label>Mode</Label>
                  <select className="input">
                    <option>Unverändert</option>
                  </select>
                </div>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <div>
                  <Label>Brightness (%)</Label>
                  <Input type="number" placeholder="Bri %" />
                </div>
                <div className="flex items-center gap-2">
                  <label className="pill cursor-pointer">
                    <input type="checkbox" className="accent-accent" /> Soft
                  </label>
                  <Button variant="primary">Wake</Button>
                  <Button variant="danger">Stop</Button>
                </div>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Sleep</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="grid gap-3 md:grid-cols-[1fr_auto_auto]">
                <div>
                  <Label>Duration (min)</Label>
                  <Input type="number" defaultValue={15} />
                </div>
                <Button variant="primary">Sleep</Button>
                <Button variant="danger">Stop</Button>
              </div>
            </CardContent>
          </Card>
        </div>

        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Settings</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div>
                <Label>Brightness cap (%)</Label>
                <div className="flex items-center gap-2">
                  <Input type="number" min={1} max={100} defaultValue={100} className="w-28" />
                  <Button>Set</Button>
                </div>
              </div>
              <div>
                <Label>Idle off (min)</Label>
                <div className="flex items-center gap-2">
                  <Input type="number" min={0} max={180} defaultValue={0} className="w-28" />
                  <Button>Set</Button>
                </div>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Log</CardTitle>
            </CardHeader>
            <CardContent className="space-y-2">
              <div className="flex items-center gap-2">
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" defaultChecked /> Live log
                </label>
                <Button>Reload status</Button>
                <Button>cfg export</Button>
              </div>
              <div className="min-h-[160px] rounded-lg border border-border bg-[#0b0f1a] p-3 font-mono text-sm text-accent">
                <p className="text-muted">[12:00:00] Waiting for connection…</p>
              </div>
              <div className="flex gap-2">
                <Input placeholder="type command" />
                <Button variant="primary">Send</Button>
              </div>
            </CardContent>
          </Card>
        </div>

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
              <CardTitle>Command Reference</CardTitle>
            </CardHeader>
            <CardContent>
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
            </CardContent>
          </Card>
        </div>
      </main>
    </div>
  );
}
