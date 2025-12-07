import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

type EaseType = 'linear' | 'ease' | 'ease-in' | 'ease-out' | 'ease-in-out';

const easeSample = (t: number, ease: EaseType, pow: number) => {
  switch (ease) {
    case 'ease-in':
      return Math.pow(t, pow);
    case 'ease-out':
      return 1 - Math.pow(1 - t, pow);
    case 'ease-in-out':
      return t < 0.5 ? 0.5 * Math.pow(2 * t, pow) : 1 - 0.5 * Math.pow(2 * (1 - t), pow);
    case 'ease':
      // simple smoothstep-ish
      return t * t * (3 - 2 * t);
    default:
      return t;
  }
};

const RampGraph = ({
  duration,
  reverse,
  pow = 2,
  ease = 'linear',
}: {
  duration?: number;
  reverse?: boolean;
  pow?: number;
  ease?: EaseType;
}) => {
  const dur = Math.max(50, Math.min(5000, duration ?? 500));
  const points = Array.from({ length: 50 }, (_, i) => {
    const t = i / 49;
    const easedBase = easeSample(t, ease, pow);
    const eased = reverse ? 1 - easedBase : easedBase;
    const x = 5 + t * 110;
    const y = 60 - eased * 48;
    return `${x},${y}`;
  }).join(' ');
  const path = `M${points}`;
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  return (
    <div className="relative rounded-lg border border-border bg-panel overflow-hidden">
      <div className="relative w-full" style={{ paddingTop: '58.33%' }}>
        <svg viewBox="0 0 120 70" className="absolute inset-0 h-full w-full">
        <defs>
          <linearGradient id={gradientId} x1="0%" x2="100%" y1="0%" y2="0%">
            <stop offset="0%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.15" />
            <stop offset="100%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.5" />
          </linearGradient>
        </defs>
        <rect x="0" y="0" width="120" height="70" fill={`url(#${gradientId})`} fillOpacity="0.08" />
        <path d={path} stroke={`url(#${gradientId})`} strokeWidth="4" fill="none" strokeLinecap="round" />
        <line x1="8" y1="8" x2="8" y2="62" stroke="#1f2937" strokeWidth="1.5" />
        <line x1="8" y1="58" x2="112" y2="58" stroke="#1f2937" strokeWidth="1.5" />
        </svg>
      </div>
      <div className="absolute inset-x-0 bottom-0 p-3 flex flex-col text-xs text-muted">
        <span className="text-sm font-semibold text-text drop-shadow">{dur} ms</span>
        <span className="drop-shadow">{reverse ? 'Fade out' : 'Fade in'} · {ease} · γ={pow.toFixed(1)}</span>
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
  const [rampOnPow, setRampOnPow] = useState(2);
  const [rampOffPow, setRampOffPow] = useState(2);

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
    if (dir === 'on') {
      setRampOnEase(ease);
      sendCmd(`ramp ease on ${ease} ${rampOnPow}`).catch((e) => console.warn(e));
    } else {
      setRampOffEase(ease);
      sendCmd(`ramp ease off ${ease} ${rampOffPow}`).catch((e) => console.warn(e));
    }
  };

  const handleRampPow = (dir: 'on' | 'off', pow: number) => {
    if (dir === 'on') {
      setRampOnPow(pow);
      if (!Number.isNaN(pow)) sendCmd(`ramp ease on ${rampOnEase} ${pow}`);
    } else {
      setRampOffPow(pow);
      if (!Number.isNaN(pow)) sendCmd(`ramp ease off ${rampOffEase} ${pow}`);
    }
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
            <div className="space-y-3">
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
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ = ${rampOnPow.toFixed(1)}`}
                inputProps={{
                  min: 0.1,
                  max: 10,
                  step: 0.1,
                  value: rampOnPow,
                }}
                onInputChange={(v) => handleRampPow('on', v)}
              />
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
              </div>
              <RampGraph duration={rampOn} pow={rampOnPow} ease={rampOnEase as EaseType} />
            </div>
          </Card>
          <Card className="p-3">
            <CardTitle className="text-base text-text"><Trans k="label.rampOff">Ramp Off</Trans></CardTitle>
            <div className="space-y-3">
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
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ = ${rampOffPow.toFixed(1)}`}
                inputProps={{
                  min: 0.1,
                  max: 10,
                  step: 0.1,
                  value: rampOffPow,
                }}
                onInputChange={(v) => handleRampPow('off', v)}
              />
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
              </div>
              <RampGraph duration={rampOff} reverse pow={rampOffPow} ease={rampOffEase as EaseType} />
            </div>
          </Card>
        </div>
      </CardContent>
    </Card>
  );
}
