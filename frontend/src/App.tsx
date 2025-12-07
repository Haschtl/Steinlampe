import { useEffect, useMemo, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import {
  Activity,
  ArrowLeftCircle,
  ArrowRightCircle,
  ClipboardPaste,
  Copy,
  Flashlight,
  LogOut,
  Pause,
  RefreshCw,
  Send,
  Shield,
  SlidersHorizontal,
  Zap,
  ZapOff,
  Sun,
  Mic,
} from 'lucide-react';
import { QRCodeSVG } from 'qrcode.react';
import { patternLabels } from './data/patterns';
import { useBle } from './hooks/useBle';
import { LampCard, PatternOption, ProfilesSection, QuickCustomSection } from './sections/HomeSection';
import { SettingsCard, PresenceTouchCard, LightMusicCard } from './sections/SettingsSection';
import { NotifyCard, WakeSleepCard } from './sections/ActionsSection';
import { HelpSection } from './sections/HelpSection';

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
  const {
    status,
    log,
    liveLog,
    setLiveLog,
    autoReconnect,
    setAutoReconnect,
    connect,
    disconnect,
    refreshStatus,
    sendCmd,
  } = useBle();
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
  const [lampOn, setLampOn] = useState(false);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [pwmCurve, setPwmCurve] = useState(1.8);
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);
  const [customCsv, setCustomCsv] = useState('');
  const [customStep, setCustomStep] = useState(400);
  const [cfgExportText, setCfgExportText] = useState('');
  const [profileQrText, setProfileQrText] = useState('');
  const [activeTab, setActiveTab] = useState<'home' | 'settings' | 'actions' | 'help'>('home');
  const [logOpen, setLogOpen] = useState(false);

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

  useEffect(() => {
    if (typeof status.cap === 'number') setCap(Math.round(status.cap));
  }, [status.cap]);

  useEffect(() => {
    if (status.currentPattern) setPattern(status.currentPattern);
  }, [status.currentPattern]);

  useEffect(() => {
    setLampOn(status.lampState === 'ON');
  }, [status.lampState]);

  useEffect(() => {
    if (typeof status.patternSpeed === 'number') setPatternSpeed(status.patternSpeed);
  }, [status.patternSpeed]);

  useEffect(() => {
    if (typeof status.patternFade === 'number') {
      setPatternFade(status.patternFade);
      setFadeEnabled(true);
    }
  }, [status.patternFade]);

  useEffect(() => {
    if (typeof status.rampOnMs === 'number') setRampOn(status.rampOnMs);
    if (typeof status.rampOffMs === 'number') setRampOff(status.rampOffMs);
  }, [status.rampOnMs, status.rampOffMs]);

  useEffect(() => {
    if (status.quickCsv) {
      const nums = status.quickCsv
        .split(',')
        .map((n) => parseInt(n.trim(), 10))
        .filter((n) => !Number.isNaN(n));
      setQuickSelection(nums);
    }
  }, [status.quickCsv]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabels[i] ? `${i + 1} - ${patternLabels[i]}` : `Pattern ${i + 1}`,
    }));
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

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
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

  const handlePatternSpeed = (val: number) => {
    const clamped = Math.max(0.1, Math.min(5, val));
    setPatternSpeed(clamped);
    sendCmd(`pat scale ${clamped.toFixed(2)}`).catch((e) => console.warn(e));
  };

  const handlePatternFade = (val: number, enable: boolean) => {
    setPatternFade(val);
    setFadeEnabled(enable);
    if (!enable) {
      sendCmd('pat fade off').catch((e) => console.warn(e));
    } else {
      sendCmd(`pat fade on ${val.toFixed(2)}`).catch((e) => console.warn(e));
    }
  };

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => {
      if (prev.includes(idx)) {
        return prev.filter((n) => n !== idx);
      }
      return [...prev, idx].sort((a, b) => a - b);
    });
  };

  const saveQuickSelection = () => {
    if (quickSelection.length === 0) return;
    sendCmd(`quick ${quickSelection.join(',')}`).catch((e) => console.warn(e));
  };

  const applyCustom = () => {
    if (customCsv.trim()) sendCmd(`custom ${customCsv.replace(/\s+/g, '')}`);
    sendCmd(`custom step ${customStep}`);
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
                {status.connecting ? 'Connecting…' : <span className="flex items-center gap-1"><Zap className="h-4 w-4" /> Connect</span>}
              </Button>
              <Button disabled>Connect Serial</Button>
              {status.connected && (
                <Button onClick={disconnect}>
                  <LogOut className="mr-1 h-4 w-4" /> Disconnect
                </Button>
              )}
              <label className="pill cursor-pointer">
                <input
                  type="checkbox"
                  className="accent-accent"
                  checked={autoReconnect}
                  onChange={(e) => setAutoReconnect(e.target.checked)}
                />{' '}
                Auto-reconnect
              </label>
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <span className="pill text-accent2">Status: {status.connected ? `Connected to ${status.deviceName}` : 'Not connected'}</span>
            <span className="pill">Last status: {status.lastStatusAt ? new Date(status.lastStatusAt).toLocaleTimeString() : '--'}</span>
            {status.patternName && <span className="pill">Pattern: {status.patternName}</span>}
            {typeof status.patternSpeed === 'number' && <span className="pill">Speed {status.patternSpeed.toFixed(2)}x</span>}
          </div>
        </div>
      </header>

      <main className="mx-auto flex max-w-6xl flex-col gap-4 px-4 py-4">
        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader className="items-start">
              <CardTitle>Quick Tap Modes</CardTitle>
              <Input
                placeholder="Search pattern"
                value={quickSearch}
                onChange={(e) => setQuickSearch(e.target.value)}
                className="w-40"
              />
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="flex flex-wrap gap-2 max-h-48 overflow-y-auto">
                {patternOptions
                  .filter((p) => p.label.toLowerCase().includes(quickSearch.toLowerCase()))
                  .map((p) => (
                    <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                      <input
                        type="checkbox"
                        className="accent-accent"
                        checked={quickSelection.includes(p.idx)}
                        onChange={() => toggleQuickSelection(p.idx)}
                      />{' '}
                      {p.label}
                    </label>
                  ))}
              </div>
              <div className="flex gap-2">
                <Button onClick={saveQuickSelection}>Save quick list</Button>
                <Button onClick={refreshStatus}>
                  <RefreshCw className="mr-1 h-4 w-4" /> Reload
                </Button>
              </div>
              <p className="text-sm text-muted">Tap the physical switch to cycle through selected quick modes.</p>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Custom Pattern</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <Label>Values (0..1 CSV)</Label>
              <textarea
                className="input w-full"
                rows={3}
                placeholder="0.2,0.8,1.0,0.4"
                value={customCsv}
                onChange={(e) => setCustomCsv(e.target.value)}
              />
              <div className="flex items-center gap-2">
                <Label className="m-0">Step (ms)</Label>
                <Input
                  type="number"
                  min={20}
                  max={2000}
                  step={20}
                  value={customStep}
                  onChange={(e) => setCustomStep(Number(e.target.value))}
                  className="w-28"
                  suffix="ms"
                  description="Step duration for each custom value"
                />
                <Button onClick={() => sendCmd(`custom step ${customStep}`)}>Set step</Button>
              </div>
              <div className="flex gap-2">
                <Button
                  variant="primary"
                  onClick={() => {
                    if (customCsv.trim()) sendCmd(`custom ${customCsv.replace(/\s+/g, '')}`);
                  }}
                >
                  Apply
                </Button>
                <Button onClick={() => setCustomCsv('')}>Clear</Button>
                <Button onClick={() => sendCmd('custom')}>Reload</Button>
              </div>
              <p className="text-sm text-muted">
                Current: len={status.customLen ?? '--'} step={status.customStepMs ?? '--'}ms
              </p>
            </CardContent>
          </Card>
        </div>

        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Presence &amp; Touch</CardTitle>
              <div className="flex items-center gap-2">
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`presence ${e.target.checked ? 'on' : 'off'}`)} /> Presence
                </label>
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`touchdim ${e.target.checked ? 'on' : 'off'}`)} /> TouchDim
                </label>
              </div>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="flex gap-2">
                <Button onClick={() => sendCmd('presence set me')}>
                  <Shield className="mr-1 h-4 w-4" /> Set me
                </Button>
                <Button onClick={() => sendCmd('presence clear')}>Clear</Button>
              </div>
              <p className="text-sm text-muted">Status: {status.presence ?? '---'}</p>
              <div className="flex gap-2">
                <Button onClick={() => sendCmd('calibrate touch')}>Calibrate touch</Button>
                <Button onClick={() => sendCmd('touch')}>Touch debug</Button>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Light &amp; Music</CardTitle>
              <div className="flex items-center gap-2">
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`light ${e.target.checked ? 'on' : 'off'}`)} /> Light
                </label>
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`music ${e.target.checked ? 'on' : 'off'}`)} /> Music
                </label>
              </div>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="flex items-center gap-2">
                <Sun className="h-4 w-4 text-muted" />
                <Label className="m-0">Light gain</Label>
                <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => sendCmd(`light gain ${e.target.value}`)} className="w-24" suffix="x" />
                <Button onClick={() => sendCmd('light calib')}>Calibrate</Button>
              </div>
              <div className="flex items-center gap-2">
                <Mic className="h-4 w-4 text-muted" />
                <Label className="m-0">Music gain</Label>
                <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => sendCmd(`music sens ${e.target.value}`)} className="w-24" suffix="x" />
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0">Clap thr</Label>
                <Input type="number" min={0.05} max={1.5} step={0.01} defaultValue={0.35} onBlur={(e) => sendCmd(`clap thr ${e.target.value}`)} className="w-24" suffix="x" />
                <Label className="m-0">Cool</Label>
                <Input type="number" min={200} max={5000} step={50} defaultValue={800} onBlur={(e) => sendCmd(`clap cool ${e.target.value}`)} className="w-24" suffix="ms" />
              </div>
            </CardContent>
          </Card>
        </div>

        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Import / Export</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="flex gap-2">
                <Button onClick={() => sendCmd('cfg export')}>
                  <Copy className="mr-1 h-4 w-4" /> cfg export
                </Button>
                <Input placeholder="cfg import key=val ..." value={cfgExportText} onChange={(e) => setCfgExportText(e.target.value)} />
                <Button onClick={() => cfgExportText.trim() && sendCmd(cfgExportText)}>
                  <ClipboardPaste className="mr-1 h-4 w-4" /> Import
                </Button>
              </div>
              <div className="flex flex-wrap gap-4">
                <div className="flex flex-col items-center gap-2">
                  <Label className="m-0">CFG QR</Label>
                  <QRCodeSVG value={cfgExportText || 'cfg export ...'} width={128} height={128} />
                </div>
                <div className="flex flex-col items-center gap-2">
                  <Label className="m-0">Profile QR</Label>
                  <QRCodeSVG value={profileQrText || 'profile export'} width={128} height={128} />
                  <Input placeholder="profile import text" value={profileQrText} onChange={(e) => setProfileQrText(e.target.value)} />
                </div>
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Profiles &amp; Custom</CardTitle>
            </CardHeader>
            <CardContent className="space-y-3">
              <div className="flex gap-2 flex-wrap">
                <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}>Save Profile {profileSlot}</Button>
                <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load Profile {profileSlot}</Button>
                <Button onClick={() => sendCmd('cfg export')}>Backup Settings</Button>
              </div>
              <div className="flex gap-2">
                <Input placeholder="cfg import ..." onKeyDown={(e) => {
                  if (e.key === 'Enter') {
                    const val = (e.target as HTMLInputElement).value.trim();
                    if (val) sendCmd(val);
                  }
                }} />
                <Button onClick={refreshStatus}>
                  <RefreshCw className="mr-1 h-4 w-4" /> Refresh
                </Button>
              </div>
            </CardContent>
          </Card>
        </div>
        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <CardTitle>Lamp &amp; Ramps</CardTitle>
              <div className="flex items-center gap-2">
                <Label className="m-0">Profile</Label>
                <select className="input" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
                  <option value="1">1</option>
                  <option value="2">2</option>
                  <option value="3">3</option>
                </select>
                <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load</Button>
                <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}>Save</Button>
              </div>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="flex flex-wrap items-center gap-3">
                <span className="chip-muted">Switch: {status.switchState ?? '--'}</span>
                <span className="chip-muted">Touch: {status.touchState ?? '--'}</span>
                <Button size="sm" onClick={() => sendCmd('sync')}>
                  <RefreshCw className="mr-1 h-4 w-4" /> Sync
                </Button>
                <Button size="sm" variant="primary" onClick={() => handleLampToggle(!lampOn)}>
                  {lampOn ? <><Pause className="mr-1 h-4 w-4" /> Off</> : <><Flashlight className="mr-1 h-4 w-4" /> On</>}
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
                      suffix="%"
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
                    <Button onClick={() => sendCmd('prev')}>
                      <ArrowLeftCircle className="mr-1 h-4 w-4" /> Prev
                    </Button>
                    <select
                      className="input"
                      value={pattern}
                      onChange={(e) => handlePatternChange(parseInt(e.target.value, 10))}
                    >
                      {patternOptions.map((p) => (
                        <option key={p.idx} value={p.idx}>
                          {p.idx === status.currentPattern ? `${p.label} (active)` : p.label}
                        </option>
                      ))}
                    </select>
                    <Button onClick={() => sendCmd('next')}>
                      Next <ArrowRightCircle className="ml-1 h-4 w-4" />
                    </Button>
                  </div>
                  <div className="flex flex-wrap gap-3">
                    <div className="flex items-center gap-2">
                    <Label className="m-0 text-muted">Speed</Label>
                    <Input
                      type="number"
                      min={0.1}
                      max={5}
                      step={0.1}
                      value={patternSpeed}
                      onChange={(e) => setPatternSpeed(Number(e.target.value))}
                      onBlur={(e) => handlePatternSpeed(Number(e.target.value))}
                      className="w-24"
                      suffix="x"
                      description="Pattern speed multiplier"
                    />
                      <input
                        type="range"
                        min="0.1"
                        max="5"
                        step="0.1"
                        value={patternSpeed}
                        onChange={(e) => setPatternSpeed(Number(e.target.value))}
                        onMouseUp={(e) => handlePatternSpeed(Number((e.target as HTMLInputElement).value))}
                        onTouchEnd={(e) => handlePatternSpeed(Number((e.target as HTMLInputElement).value))}
                        className="accent-accent"
                      />
                    </div>
                    <div className="flex items-center gap-2">
                      <Label className="m-0 text-muted">Fade</Label>
                      <select
                        className="input"
                        value={fadeEnabled ? patternFade : 'off'}
                        onChange={(e) => {
                          const val = e.target.value;
                          if (val === 'off') handlePatternFade(1, false);
                          else handlePatternFade(parseFloat(val), true);
                        }}
                      >
                        <option value="off">Off</option>
                        <option value="0.5">0.5x</option>
                        <option value="1">1x</option>
                        <option value="2">2x</option>
                      </select>
                    </div>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    <label className="pill cursor-pointer">
                      <input
                        type="checkbox"
                        className="accent-accent"
                        onChange={(e) => sendCmd(`auto ${e.target.checked ? 'on' : 'off'}`)}
                      />{' '}
                      <span className="inline-flex items-center gap-1"><Activity className="h-4 w-4" /> AutoCycle</span>
                    </label>
                    <label className="pill cursor-pointer">
                      <input type="checkbox" className="accent-accent" disabled /> Pattern Fade
                    </label>
                  </div>
                </div>
                <div className="space-y-2">
                  <Label>Profile</Label>
                  <div className="flex gap-2 text-muted text-sm">oben bei Lamp &amp; Ramps</div>
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
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Duration</Label>
                      <Input
                        type="number"
                        min={50}
                        max={10000}
                        step={10}
                        value={rampOn ?? ''}
                        onChange={(e) => setRampOn(Number(e.target.value))}
                        onBlur={(e) => {
                          const v = Number(e.target.value);
                          if (!Number.isNaN(v)) sendCmd(`ramp on ${v}`);
                        }}
                        className="w-28"
                        suffix="ms"
                      />
                      <div className="h-2 w-full rounded bg-border">
                        <div
                          className="h-2 rounded bg-accent"
                          style={{ width: `${Math.min(100, Math.max(5, ((rampOn || 0) / 5000) * 100))}%` }}
                        />
                      </div>
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
                    <div className="flex items-center gap-2">
                      <Label className="m-0">Duration</Label>
                      <Input
                        type="number"
                        min={50}
                        max={10000}
                        step={10}
                        value={rampOff ?? ''}
                        onChange={(e) => setRampOff(Number(e.target.value))}
                        onBlur={(e) => {
                          const v = Number(e.target.value);
                          if (!Number.isNaN(v)) sendCmd(`ramp off ${v}`);
                        }}
                        className="w-28"
                        suffix="ms"
                      />
                      <div className="h-2 w-full rounded bg-border">
                        <div
                          className="h-2 rounded bg-accent2"
                          style={{ width: `${Math.min(100, Math.max(5, ((rampOff || 0) / 5000) * 100))}%` }}
                        />
                      </div>
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
              <div className="flex items-center gap-3">
                <Button variant="primary" onClick={() => sendCmd('sos')}>
                  <Zap className="mr-1 h-4 w-4" /> SOS Start
                </Button>
                <Button variant="danger" onClick={() => sendCmd('sos stop')}>
                  <ZapOff className="mr-1 h-4 w-4" /> SOS Stop
                </Button>
              </div>
              <div className="grid gap-2 md:grid-cols-[1fr_auto]">
                <div className="space-y-1">
                  <Label>Sequence</Label>
                  <Input
                    placeholder="80 40 80 120"
                    value={notifySeq}
                    onChange={(e) => setNotifySeq(e.target.value)}
                    suffix="ms"
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
                      suffix="ms"
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
                    <Send className="mr-1 h-4 w-4" /> Notify
                  </Button>
                  <Button variant="danger" onClick={() => sendCmd('notify stop')}>
                    <ZapOff className="mr-1 h-4 w-4" /> Stop
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
                  <Input type="number" min={1} value={wakeDuration} onChange={(e) => setWakeDuration(Number(e.target.value))} suffix="s" />
                </div>
                <div>
                  <Label>Mode</Label>
                  <select className="input" value={wakeMode} onChange={(e) => setWakeMode(e.target.value)}>
                    <option value="">Unverändert</option>
                    {patternOptions.slice(0, 10).map((p) => (
                      <option key={p.idx} value={p.idx}>
                        {p.label}
                      </option>
                    ))}
                  </select>
                </div>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <div>
                  <Label>Brightness (%)</Label>
                  <Input type="number" placeholder="Bri %" value={wakeBri} onChange={(e) => setWakeBri(e.target.value)} suffix="%" />
                </div>
                <div className="flex items-center gap-2">
                  <label className="pill cursor-pointer">
                    <input type="checkbox" className="accent-accent" checked={wakeSoft} onChange={(e) => setWakeSoft(e.target.checked)} /> Soft
                  </label>
                  <Button variant="primary" onClick={handleWake}>
                    Wake
                  </Button>
                  <Button variant="danger" onClick={() => sendCmd('wake stop')}>
                    <ZapOff className="mr-1 h-4 w-4" /> Stop
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
                  <Input type="number" min={1} value={sleepMinutes} onChange={(e) => setSleepMinutes(Number(e.target.value))} suffix="min" />
                </div>
                <Button variant="primary" onClick={() => sendCmd(`sleep ${Math.max(1, sleepMinutes || 1)}`)}>
                  <Zap className="mr-1 h-4 w-4" /> Sleep
                </Button>
                <Button variant="danger" onClick={() => sendCmd('sleep stop')}>
                  <ZapOff className="mr-1 h-4 w-4" /> Stop
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
                  <Input type="number" min={1} max={100} value={cap} onChange={(e) => setCap(Number(e.target.value))} className="w-28" suffix="%" />
                  <Button onClick={handleCap}>Set</Button>
                </div>
              </div>
              <div>
                <Label>Idle off (min)</Label>
                <div className="flex items-center gap-2">
                  <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" suffix="min" />
                  <Button onClick={() => sendCmd(`idleoff ${Math.max(0, idleMinutes)}`)}>Set</Button>
                </div>
              </div>
              <div>
                <Label>PWM Curve</Label>
                <div className="flex items-center gap-2">
                  <Input
                    type="number"
                    min={0.5}
                    max={4}
                    step={0.1}
                    value={pwmCurve}
                    onChange={(e) => setPwmCurve(Number(e.target.value))}
                    onBlur={(e) => {
                      const v = Number(e.target.value);
                      if (!Number.isNaN(v)) sendCmd(`pwm curve ${v.toFixed(2)}`);
                    }}
                    className="w-28"
                    suffix="γ"
                    description="Gamma to linearize LED brightness (0.5–4)"
                  />
                  <SlidersHorizontal className="h-4 w-4 text-muted" />
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
                <Button onClick={refreshStatus}>
                  <RefreshCw className="mr-1 h-4 w-4" /> Reload status
                </Button>
                <Button onClick={() => sendCmd('cfg export')}>cfg export</Button>
              </div>
              <div className="min-h-[160px] rounded-lg border border-border bg-[#0b0f1a] p-3 font-mono text-sm text-accent space-y-1">
                {log.length === 0 && <p className="text-muted">Waiting for connection…</p>}
                {log.slice(-120).map((l) => (
                  <div key={l.ts} className="flex items-start gap-2 border-b border-border/40 pb-1 last:border-b-0 last:pb-0">
                    <span className="rounded bg-[#10172a] px-2 py-0.5 text-xs text-muted">
                      {new Date(l.ts).toLocaleTimeString([], { hour12: false })}
                    </span>
                    <span className="text-accent">{l.line}</span>
                  </div>
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
                    <Send className="mr-1 h-4 w-4" /> Send
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
              <CardTitle>Command Reference & Links</CardTitle>
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
      </main>
    </div>
  );
}
