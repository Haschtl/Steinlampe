import { useEffect, useMemo, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useBle } from './hooks/useBle';

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
  const { status, log, liveLog, setLiveLog, connect, disconnect, refreshStatus, sendCmd } = useBle();
  const [brightness, setBrightness] = useState(70);
  const [cap, setCap] = useState(100);
  const [pattern, setPattern] = useState(1);
  const [notifySeq, setNotifySeq] = useState('80 40 80 120');
  const [notifyFade, setNotifyFade] = useState(0);
  const [notifyRepeat, setNotifyRepeat] = useState(1);
  const [wakeDuration, setWakeDuration] = useState(180);
  const [wakeMode, setWakeMode] = useState('');
  const [wakeBri, setWakeBri] = useState('');
  const [wakeSoft, setWakeSoft] = useState(false);
  const [sleepMinutes, setSleepMinutes] = useState(15);
  const [idleMinutes, setIdleMinutes] = useState(0);
  const [profileSlot, setProfileSlot] = useState('1');
  const [commandInput, setCommandInput] = useState('');

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

  useEffect(() => {
    if (typeof status.cap === 'number') setCap(Math.round(status.cap));
  }, [status.cap]);

  useEffect(() => {
    if (status.currentPattern) setPattern(status.currentPattern);
  }, [status.currentPattern]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || 30;
    return Array.from({ length: count }, (_, i) => i + 1);
  }, [status.patternCount]);

  const handleBrightness = (value: number) => {
    const clamped = Math.min(100, Math.max(1, Math.round(value)));
    setBrightness(clamped);
    sendCmd(`bri ${clamped}`).catch((e) => console.warn(e));
  };

  const handleCap = () => {
    const clamped = Math.min(100, Math.max(1, Math.round(cap)));
    setCap(clamped);
    sendCmd(`bri cap ${clamped}`).catch((e) => console.warn(e));
  };

  const handlePatternChange = (val: number) => {
    setPattern(val);
    sendCmd(`mode ${val}`).catch((e) => console.warn(e));
  };

  const buildNotifyCmd = (seq: string, fade?: number, repeat = 1) => {
    const parts = seq
      .split(/\s+/)
      .map((n) => parseInt(n, 10))
      .filter((n) => !Number.isNaN(n) && n > 0);
    if (!parts.length) return '';
    const expanded: number[] = [];
    for (let i = 0; i < repeat; i += 1) expanded.push(...parts);
    let cmd = `notify ${expanded.join(' ')}`;
    if (fade && fade > 0) cmd += ` fade=${fade}`;
    return cmd;
  };

  const handleNotify = (seq: string, fade?: number, repeat = 1) => {
    const cmd = buildNotifyCmd(seq, fade, repeat);
    if (!cmd) return;
    sendCmd(cmd).catch((e) => console.warn(e));
  };

  const handleWake = () => {
    const parts = ['wake'];
    if (wakeSoft) parts.push('soft');
    if (wakeMode.trim()) parts.push(`mode=${wakeMode.trim()}`);
    if (wakeBri.trim()) parts.push(`bri=${wakeBri.trim()}`);
    parts.push(String(Math.max(1, wakeDuration || 1)));
    sendCmd(parts.join(' ')).catch((e) => console.warn(e));
  };

  const iconHref = `${import.meta.env.BASE_URL}icon-lamp.svg`;

  return (
    <div className="min-h-screen bg-bg text-text">
      <header className="sticky top-0 z-10 bg-gradient-to-br from-[#111a2d] via-[#0b0f1a] to-[#0b0f1a] px-4 py-3 shadow-lg shadow-black/40">
        <div className="mx-auto flex max-w-6xl items-center gap-3">
          <img src={iconHref} alt="Lamp Icon" className="h-10 w-10 rounded-lg border border-border bg-[#0b0f1a] p-1.5" />
          <div className="flex flex-1 items-center gap-3">
            <h1 className="text-xl font-semibold tracking-wide">Quarzlampe Control</h1>
            <div className="flex flex-wrap gap-2">
              <Button variant="primary" onClick={connect} disabled={status.connecting}>
                {status.connecting ? 'Connecting…' : 'Connect'}
              </Button>
              <Button disabled>Connect Serial</Button>
              {status.connected && (
                <Button onClick={disconnect}>
                  Disconnect
                </Button>
              )}
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" defaultChecked /> Auto-reconnect
              </label>
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <span className="pill text-accent2">Status: {status.connected ? `Connected to ${status.deviceName}` : 'Not connected'}</span>
            <span className="pill">Last status: {status.lastStatusAt ? new Date(status.lastStatusAt).toLocaleTimeString() : '--'}</span>
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
                <span className="chip-muted">Switch: {status.switchState ?? '--'}</span>
                <span className="chip-muted">Touch: {status.touchState ?? '--'}</span>
                <Button size="sm" onClick={() => sendCmd('sync')}>
                  Sync
                </Button>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <div className="space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-sm text-muted">Brightness</span>
                    <Input
                      type="number"
                      min={1}
                      max={100}
                      value={brightness}
                      onChange={(e) => setBrightness(Number(e.target.value))}
                      onBlur={(e) => handleBrightness(Number(e.target.value))}
                      className="w-24 text-right"
                    />
                  </div>
                  <input
                    type="range"
                    min="1"
                    max="100"
                    value={brightness}
                    onChange={(e) => setBrightness(Number(e.target.value))}
                    onMouseUp={(e) => handleBrightness(Number((e.target as HTMLInputElement).value))}
                    onTouchEnd={(e) => handleBrightness(Number((e.target as HTMLInputElement).value))}
                    className="w-full accent-accent"
                  />
                  <div className="flex gap-2">
                    <Button onClick={() => sendCmd('prev')}>Prev</Button>
                    <select
                      className="input"
                      value={pattern}
                      onChange={(e) => handlePatternChange(parseInt(e.target.value, 10))}
                    >
                      {patternOptions.map((num) => (
                        <option key={num} value={num}>
                          {num === status.currentPattern ? `Pattern ${num} (active)` : `Pattern ${num}`}
                        </option>
                      ))}
                    </select>
                    <Button onClick={() => sendCmd('next')}>Next</Button>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    <label className="pill cursor-pointer">
                      <input
                        type="checkbox"
                        className="accent-accent"
                        onChange={(e) => sendCmd(`auto ${e.target.checked ? 'on' : 'off'}`)}
                      />{' '}
                      AutoCycle
                    </label>
                    <label className="pill cursor-pointer">
                      <input type="checkbox" className="accent-accent" disabled /> Pattern Fade
                    </label>
                  </div>
                </div>
                <div className="space-y-2">
                  <Label>Profile</Label>
                  <div className="flex gap-2">
                    <select className="input" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
                      <option value="1">1</option>
                      <option value="2">2</option>
                      <option value="3">3</option>
                    </select>
                    <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load</Button>
                    <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}>Save</Button>
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
                  <Input
                    placeholder="80 40 80 120"
                    value={notifySeq}
                    onChange={(e) => setNotifySeq(e.target.value)}
                  />
                  <div className="flex items-center gap-3">
                    <Label className="m-0 text-muted">Fade</Label>
                    <Input
                      type="number"
                      min={0}
                      max={500}
                      step={10}
                      value={notifyFade}
                      onChange={(e) => setNotifyFade(Number(e.target.value))}
                      className="w-24"
                    />
                    <Label className="m-0 text-muted">Repeat</Label>
                    <Input
                      type="number"
                      min={1}
                      max={10}
                      step={1}
                      value={notifyRepeat}
                      onChange={(e) => setNotifyRepeat(Number(e.target.value))}
                      className="w-20"
                    />
                  </div>
                </div>
                <div className="flex items-end gap-2">
                  <Button variant="primary" onClick={() => handleNotify(notifySeq, notifyFade, notifyRepeat)}>
                    Notify
                  </Button>
                  <Button variant="danger" onClick={() => sendCmd('notify stop')}>
                    Stop
                  </Button>
                </div>
              </div>
              <div className="flex flex-wrap gap-2">
                {[
                  { label: 'Short blink', seq: '80 40 80 120', fade: 0 },
                  { label: 'Soft alert', seq: '200 100', fade: 100 },
                  { label: 'Triple pulse', seq: '120 80 120 80 120 200', fade: 40 },
                  { label: 'Doorbell', seq: '200 80 200 400', fade: 30 },
                  { label: 'Long Fade Alert', seq: '500 300', fade: 120 },
                  { label: 'Double 60', seq: '60 60 60 200', fade: 0 },
                  { label: 'SOS', seq: '400 400 400 800 800 800 400 400 400 1200', fade: 0 },
                ].map((p) => (
                  <Button
                    key={p.label}
                    onClick={() => {
                      setNotifySeq(p.seq);
                      setNotifyFade(p.fade ?? 0);
                      setNotifyRepeat(1);
                      handleNotify(p.seq, p.fade, 1);
                    }}
                  >
                    {p.label}
                  </Button>
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
                  <Input type="number" min={1} value={wakeDuration} onChange={(e) => setWakeDuration(Number(e.target.value))} />
                </div>
                <div>
                  <Label>Mode</Label>
                  <select className="input" value={wakeMode} onChange={(e) => setWakeMode(e.target.value)}>
                    <option value="">Unverändert</option>
                    {patternOptions.slice(0, 10).map((m) => (
                      <option key={m} value={m}>
                        {m}
                      </option>
                    ))}
                  </select>
                </div>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <div>
                  <Label>Brightness (%)</Label>
                  <Input type="number" placeholder="Bri %" value={wakeBri} onChange={(e) => setWakeBri(e.target.value)} />
                </div>
                <div className="flex items-center gap-2">
                  <label className="pill cursor-pointer">
                    <input type="checkbox" className="accent-accent" checked={wakeSoft} onChange={(e) => setWakeSoft(e.target.checked)} /> Soft
                  </label>
                  <Button variant="primary" onClick={handleWake}>
                    Wake
                  </Button>
                  <Button variant="danger" onClick={() => sendCmd('wake stop')}>
                    Stop
                  </Button>
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
                  <Input type="number" min={1} value={sleepMinutes} onChange={(e) => setSleepMinutes(Number(e.target.value))} />
                </div>
                <Button variant="primary" onClick={() => sendCmd(`sleep ${Math.max(1, sleepMinutes || 1)}`)}>
                  Sleep
                </Button>
                <Button variant="danger" onClick={() => sendCmd('sleep stop')}>
                  Stop
                </Button>
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
                  <Input type="number" min={1} max={100} value={cap} onChange={(e) => setCap(Number(e.target.value))} className="w-28" />
                  <Button onClick={handleCap}>Set</Button>
                </div>
              </div>
              <div>
                <Label>Idle off (min)</Label>
                <div className="flex items-center gap-2">
                  <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" />
                  <Button onClick={() => sendCmd(`idleoff ${Math.max(0, idleMinutes)}`)}>Set</Button>
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
                  <input
                    type="checkbox"
                    className="accent-accent"
                    checked={liveLog}
                    onChange={(e) => setLiveLog(e.target.checked)}
                  />{' '}
                  Live log
                </label>
                <Button onClick={refreshStatus}>Reload status</Button>
                <Button onClick={() => sendCmd('cfg export')}>cfg export</Button>
              </div>
              <div className="min-h-[160px] rounded-lg border border-border bg-[#0b0f1a] p-3 font-mono text-sm text-accent">
                {log.length === 0 && <p className="text-muted">Waiting for connection…</p>}
                {log.slice(-80).map((l) => (
                  <p key={l.ts} className="text-accent">
                    [{new Date(l.ts).toLocaleTimeString()}] {l.line}
                  </p>
                ))}
              </div>
              <div className="flex gap-2">
                <Input
                  placeholder="type command"
                  value={commandInput}
                  onChange={(e) => setCommandInput(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') {
                      const val = commandInput.trim();
                      if (val) {
                        sendCmd(val);
                        setCommandInput('');
                      }
                    }
                  }}
                />
                <Button
                  variant="primary"
                  onClick={() => {
                    const val = commandInput.trim();
                    if (val) {
                      sendCmd(val);
                      setCommandInput('');
                    }
                  }}
                >
                  Send
                </Button>
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
