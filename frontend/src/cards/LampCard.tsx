import { useEffect, useMemo, useState } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle, Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

const RampGraph = ({ duration, reverse }: { duration?: number; reverse?: boolean }) => {
  const dur = Math.max(50, Math.min(5000, duration ?? 500));
  const path = reverse
    ? 'M8 10 C 42 12 74 50 112 58'
    : 'M8 58 C 42 50 74 12 112 10';
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  return (
    <div className="flex items-center gap-3 rounded-lg border border-border bg-[#0c1221] px-2 py-2">
      <svg viewBox="0 0 120 70" className="h-14 w-24">
        <defs>
          <linearGradient id={gradientId} x1="0%" x2="100%" y1="0%" y2="0%">
            <stop offset="0%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.2" />
            <stop offset="100%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.6" />
          </linearGradient>
        </defs>
        <rect x="0" y="0" width="120" height="70" fill="url(#{gradientId})" fillOpacity="0.08" />
        <path d={path} stroke={`url(#{gradientId})`} strokeWidth="4" fill="none" strokeLinecap="round" />
        <line x1="8" y1="8" x2="8" y2="62" stroke="#1f2937" strokeWidth="1.5" />
        <line x1="8" y1="58" x2="112" y2="58" stroke="#1f2937" strokeWidth="1.5" />
      </svg>
      <div className="flex flex-col text-xs text-muted">
        <span className="text-sm font-semibold text-text">{dur} ms</span>
        <span>{reverse ? 'Fade out' : 'Fade in'}</span>
      </div>
    </div>
  );
};

export function LampCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { status, sendCmd } = useConnection();
  const [brightness, setBrightness] = useState(70);
  const [pattern, setPattern] = useState(1);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [rampOnEase, setRampOnEase] = useState('linear');
  const [rampOffEase, setRampOffEase] = useState('linear');
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

  const handleRampEase = (dir: 'on' | 'off', ease: string) => {
    if (dir === 'on') setRampOnEase(ease);
    else setRampOffEase(ease);
    sendCmd(`ramp ${dir} ease ${ease}`).catch((e) => console.warn(e));
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
              <div className="flex items-center gap-3">
                <Label className="m-0 text-muted"><Trans k="label.fade">Fade</Trans></Label>
                <input
                  type="range"
                  min="0"
                  max="2"
                  step="0.1"
                  value={fadeEnabled ? patternFade : 0}
                  onChange={(e) => {
                    const val = parseFloat(e.target.value);
                    handlePatternFade(val || 1, val > 0);
                  }}
                  className="accent-accent"
                />
                <span className="chip-muted">{fadeEnabled ? `${patternFade.toFixed(1)}x` : 'Off'}</span>
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
                <Label className="m-0">Mode</Label>
                <select className="input" value={rampOnEase} onChange={(e) => handleRampEase('on', e.target.value)}>
                  <option value="linear">linear</option>
                  <option value="ease">ease</option>
                  <option value="ease-in">ease-in</option>
                  <option value="ease-out">ease-out</option>
                  <option value="ease-in-out">ease-in-out</option>
                </select>
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
                <RampGraph duration={rampOn} />
              </div>
            </div>
          </Card>
          <Card className="p-3">
            <CardTitle className="text-base text-text">Ramp Off</CardTitle>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label className="m-0">Mode</Label>
                <select className="input" value={rampOffEase} onChange={(e) => handleRampEase('off', e.target.value)}>
                  <option value="linear">linear</option>
                  <option value="ease">ease</option>
                  <option value="ease-in">ease-in</option>
                  <option value="ease-out">ease-out</option>
                  <option value="ease-in-out">ease-in-out</option>
                </select>
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
                <RampGraph duration={rampOff} reverse />
              </div>
            </div>
          </Card>
        </div>
      </CardContent>
    </Card>
  );
}
