import { useEffect, useMemo, useState } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle, Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

export function LampControlsCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { status, sendCmd } = useConnection();
  const [brightness, setBrightness] = useState(70);
  const [pattern, setPattern] = useState(1);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);
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

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.lamp">Lamp</Trans></CardTitle>
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
      </CardContent>
    </Card>
  );
}
