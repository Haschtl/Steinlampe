import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
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
        <rect x="0" y="0" width="120" height="70" fill={`url(#${gradientId})`} fillOpacity="0.08" />
        <path d={path} stroke={`url(#${gradientId})`} strokeWidth="4" fill="none" strokeLinecap="round" />
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

export function RampCard() {
  const { status, sendCmd } = useConnection();
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [rampOnEase, setRampOnEase] = useState('linear');
  const [rampOffEase, setRampOffEase] = useState('linear');

  useEffect(() => {
    if (typeof status.rampOnMs === 'number') setRampOn(status.rampOnMs);
    if (typeof status.rampOffMs === 'number') setRampOff(status.rampOffMs);
  }, [status.rampOnMs, status.rampOffMs]);

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

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.ramps">Ramps</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="grid gap-3 md:grid-cols-2">
          <Card className="p-3">
            <CardTitle className="text-base text-text"><Trans k="label.rampOn">Ramp On</Trans></CardTitle>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label className="m-0"><Trans k="label.mode">Mode</Trans></Label>
                <select className="input" value={rampOnEase} onChange={(e) => handleRampEase('on', e.target.value)}>
                  <option value="linear">linear</option>
                  <option value="ease">ease</option>
                  <option value="ease-in">ease-in</option>
                  <option value="ease-out">ease-out</option>
                  <option value="ease-in-out">ease-in-out</option>
                </select>
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0"><Trans k="label.duration">Duration</Trans></Label>
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
            <CardTitle className="text-base text-text"><Trans k="label.rampOff">Ramp Off</Trans></CardTitle>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label className="m-0"><Trans k="label.mode">Mode</Trans></Label>
                <select className="input" value={rampOffEase} onChange={(e) => handleRampEase('off', e.target.value)}>
                  <option value="linear">linear</option>
                  <option value="ease">ease</option>
                  <option value="ease-in">ease-in</option>
                  <option value="ease-out">ease-out</option>
                  <option value="ease-in-out">ease-in-out</option>
                </select>
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0"><Trans k="label.duration">Duration</Trans></Label>
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
