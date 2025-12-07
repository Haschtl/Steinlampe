import { useEffect, useMemo, useState } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle, ClipboardPaste, Copy, Flashlight, Pause, RefreshCw, Send } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

export type PatternOption = { idx: number; label: string };

type LampCardProps = {
  profileSlot: string;
  setProfileSlot: (v: string) => void;
};

type ProfilesProps = {
  profileSlot: string;
  setProfileSlot: (v: string) => void;
};

export function LampCard({ profileSlot, setProfileSlot }: LampCardProps) {
  const { status, sendCmd } = useConnection();
  const [brightness, setBrightness] = useState(70);
  const [pattern, setPattern] = useState(1);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [lampOn, setLampOn] = useState(false);

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

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

  const handlePatternChange = (val: number) => {
    setPattern(val);
    sendCmd(`mode ${val}`).catch((e) => console.warn(e));
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

  const handleRampOn = (val: number) => {
    setRampOn(val);
    if (!Number.isNaN(val)) sendCmd(`ramp on ${val}`);
  };

  const handleRampOff = (val: number) => {
    setRampOff(val);
    if (!Number.isNaN(val)) sendCmd(`ramp off ${val}`);
  };

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.lamp">Lamp &amp; Ramps</Trans></CardTitle>
        <div className="flex items-center gap-2">
          <Label className="m-0"><Trans k="label.profile">Profile</Trans></Label>
          <select className="input" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
            <option value="1">1</option>
            <option value="2">2</option>
            <option value="3">3</option>
          </select>
          <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}><Trans k="btn.load">Load</Trans></Button>
          <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}><Trans k="btn.save">Save</Trans></Button>
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
            {lampOn ? (
              <>
                <Pause className="mr-1 h-4 w-4" /> Off
              </>
            ) : (
              <>
                <Flashlight className="mr-1 h-4 w-4" /> On
              </>
            )}
          </Button>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <span className="text-sm text-muted"><Trans k="label.brightness">Brightness</Trans></span>
              <Input
                type="number"
                min={1}
                max={100}
                value={brightness}
                onChange={(e) => handleBrightness(Number(e.target.value))}
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
              onChange={(e) => handleBrightness(Number(e.target.value))}
              className="w-full accent-accent"
            />
            <div className="flex gap-2">
              <Button onClick={() => sendCmd('prev')}>
                <ArrowLeftCircle className="mr-1 h-4 w-4" /> Prev
              </Button>
              <select className="input" value={pattern} onChange={(e) => handlePatternChange(parseInt(e.target.value, 10))}>
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
                <Label className="m-0 text-muted"><Trans k="label.speed">Speed</Trans></Label>
                <Input
                  type="number"
                  min={0.1}
                  max={5}
                  step={0.1}
                  value={patternSpeed}
                  onChange={(e) => handlePatternSpeed(Number(e.target.value))}
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
                  onChange={(e) => handlePatternSpeed(Number(e.target.value))}
                  className="accent-accent"
                />
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0 text-muted"><Trans k="label.fade">Fade</Trans></Label>
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
                <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`auto ${e.target.checked ? 'on' : 'off'}`)} />{' '}
                <span className="inline-flex items-center gap-1">
                  <Activity className="h-4 w-4" /> AutoCycle
                </span>
              </label>
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" disabled /> Pattern Fade
              </label>
            </div>
          </div>
          <div className="space-y-2 text-sm text-muted">
            <p>Profiles moved to header.</p>
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
                  onChange={(e) => handleRampOn(Number(e.target.value))}
                  className="w-28"
                  suffix="ms"
                />
                <div className="h-2 w-full rounded bg-border">
                  <div className="h-2 rounded bg-accent" style={{ width: `${Math.min(100, Math.max(5, ((rampOn || 0) / 5000) * 100))}%` }} />
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
                  onChange={(e) => handleRampOff(Number(e.target.value))}
                  className="w-28"
                  suffix="ms"
                />
                <div className="h-2 w-full rounded bg-border">
                  <div className="h-2 rounded bg-accent2" style={{ width: `${Math.min(100, Math.max(5, ((rampOff || 0) / 5000) * 100))}%` }} />
                </div>
              </div>
            </div>
          </Card>
        </div>
      </CardContent>
    </Card>
  );
}

export function QuickCustomSection() {
  const { status, refreshStatus, sendCmd } = useConnection();
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);
  const [customCsv, setCustomCsv] = useState('');
  const [customStep, setCustomStep] = useState(400);

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

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => (prev.includes(idx) ? prev.filter((n) => n !== idx) : [...prev, idx].sort((a, b) => a - b)));
  };

  const saveQuickSelection = () => {
    if (quickSelection.length === 0) return;
    sendCmd(`quick ${quickSelection.join(',')}`).catch((e) => console.warn(e));
  };

  const applyCustom = () => {
    if (customCsv.trim()) sendCmd(`custom ${customCsv.replace(/\s+/g, '')}`);
    if (customStep) sendCmd(`custom step ${customStep}`);
  };

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader className="items-start">
          <CardTitle><Trans k="title.quick">Quick Tap Modes</Trans></CardTitle>
          <Input placeholder="Search pattern" value={quickSearch} onChange={(e) => setQuickSearch(e.target.value)} className="w-40" />
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex flex-wrap gap-2 max-h-48 overflow-y-auto">
            {patternOptions
              .filter((p) => p.label.toLowerCase().includes(quickSearch.toLowerCase()))
              .map((p) => (
                <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                  <input type="checkbox" className="accent-accent" checked={quickSelection.includes(p.idx)} onChange={() => toggleQuickSelection(p.idx)} /> {p.label}
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
          <CardTitle><Trans k="title.custom">Custom Pattern</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <Label><Trans k="label.values">Values (0..1 CSV)</Trans></Label>
          <textarea className="input w-full" rows={3} placeholder="0.2,0.8,1.0,0.4" value={customCsv} onChange={(e) => setCustomCsv(e.target.value)} />
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
            <Button onClick={() => applyCustom()}>Set step</Button>
          </div>
          <div className="flex gap-2">
            <Button variant="primary" onClick={applyCustom}>
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
  );
}

export function ProfilesSection({ profileSlot, setProfileSlot }: ProfilesProps) {
  const { sendCmd } = useConnection();
  const [cfgText, setCfgText] = useState('');

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.import">Import / Export</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2">
            <Button onClick={() => sendCmd('cfg export')}>
              <Copy className="mr-1 h-4 w-4" /> cfg export
            </Button>
            <Input placeholder="cfg import key=val ..." value={cfgText} onChange={(e) => setCfgText(e.target.value)} />
            <Button onClick={() => cfgText.trim() && sendCmd(cfgText)}>
              <ClipboardPaste className="mr-1 h-4 w-4" /> Import
            </Button>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.profiles">Profiles</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2 flex-wrap items-center">
            <Label className="m-0">Slot</Label>
            <select className="input w-20" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
              <option value="1">1</option>
              <option value="2">2</option>
              <option value="3">3</option>
            </select>
            <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}>Save Profile {profileSlot}</Button>
            <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load Profile {profileSlot}</Button>
            <Button onClick={() => sendCmd('cfg export')}>Backup Settings</Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <LampCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <QuickCustomSection />
      <ProfilesSection profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
    </div>
  );
}
