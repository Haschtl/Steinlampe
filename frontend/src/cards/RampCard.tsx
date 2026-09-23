import { useEffect, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { Trans } from '@/i18n';

type EaseType =
  | 'linear'
  | 'ease'
  | 'ease-in'
  | 'ease-out'
  | 'ease-in-out'
  | 'flash'
  | 'wave'
  | 'blink'
  | 'sine'
  | 'circ'
  | 'expo'
  | 'back'
  | 'elastic'
  | 'bounce'
  | 'stepped'
  | 'flicker';

const EASE_OPTIONS: EaseType[] = [
  'linear',
  'ease',
  'ease-in',
  'ease-out',
  'ease-in-out',
  'flash',
  'wave',
  'blink',
  'sine',
  'circ',
  'expo',
  'back',
  'elastic',
  'bounce',
  'stepped',
  'flicker',
];

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
    case 'flash':
      return Math.min(1, Math.pow(t, pow > 0 ? 1 / pow : 1));
    case 'wave': {
      if (t < 0.45) {
        const u = t / 0.45;
        const s = u * u * (3 - 2 * u);
        return s;
      }
      if (t < 0.75) {
        const u = (t - 0.45) / 0.3;
        const s = u * u * (3 - 2 * u);
        return 1 - 0.5 * s;
      }
      const u = (t - 0.75) / 0.25;
      const s = u * u * (3 - 2 * u);
      return 0.5 + 0.5 * s;
    }
    case 'blink': {
      const smooth = (u: number) => {
        const s = Math.max(0, Math.min(1, u));
        return s * s * (3 - 2 * s);
      };
      if (t < 0.1) return smooth(t / 0.1);
      if (t < 0.2) return 1 - smooth((t - 0.1) / 0.1);
      if (t < 0.3) return smooth((t - 0.2) / 0.1);
      if (t < 0.4) return 1 - smooth((t - 0.3) / 0.1);
      const u = (t - 0.4) / 0.6;
      const p = pow > 0.1 ? pow : 2;
      return Math.pow(Math.max(0, Math.min(1, u)), 1 / p);
    }
    case 'sine':
      return 0.5 - 0.5 * Math.cos(Math.PI * t);
    case 'circ': {
      if (t < 0.5) {
        const x = 2 * t;
        return 0.5 * (1 - Math.sqrt(1 - x * x));
      }
      const x = 2 * t - 2;
      return 0.5 * (Math.sqrt(1 - x * x) + 1);
    }
    case 'expo': {
      if (t <= 0) return 0;
      if (t >= 1) return 1;
      const k = pow > 0.1 ? pow * 2 : 20;
      if (t < 0.5) return 0.5 * Math.pow(2, k * t - k * 0.5);
      return 1 - 0.5 * Math.pow(2, -k * t + k * 0.5);
    }
    case 'back': {
      const c1 = pow > 0.1 ? pow : 1.70158;
      const c2 = c1 * 1.525;
      if (t < 0.5) {
        const x = 2 * t;
        return (x * x * ((c2 + 1) * x - c2)) * 0.5;
      }
      const x = 2 * t - 2;
      return (x * x * ((c2 + 1) * x + c2) + 2) * 0.5;
    }
    case 'elastic': {
      const n = pow > 0.1 ? pow : 3;
      const amp = 0.16;
      return t + amp * Math.sin(2 * Math.PI * n * t) * (1 - t);
    }
    case 'bounce': {
      const n1 = 7.5625;
      const d1 = 2.75;
      let x = t;
      if (x < 1 / d1) return n1 * x * x;
      if (x < 2 / d1) {
        x -= 1.5 / d1;
        return n1 * x * x + 0.75;
      }
      if (x < 2.5 / d1) {
        x -= 2.25 / d1;
        return n1 * x * x + 0.9375;
      }
      x -= 2.625 / d1;
      return n1 * x * x + 0.984375;
    }
    case 'stepped': {
      const n = Math.max(2, pow > 1.5 ? Math.floor(pow) : 8);
      let idx = Math.floor(t * n);
      if (idx >= n) idx = n - 1;
      return idx / (n - 1);
    }
    case 'flicker': {
      // Illustrative only - the device seeds this randomly per activation.
      const amp = pow > 0.1 ? Math.min(1, pow / 10) : 0.25;
      const n1 = Math.sin(t * 71.7 + 1.3) * 0.5;
      const n2 = Math.sin(t * 133.1 + 4.7) * 0.5;
      const flicker = (n1 * 0.7 + n2 * 0.3) * amp * (1 - t);
      return t + flicker * t;
    }
    default:
      return t;
  }
};

const buildRampPaths = (duration: number, ease: EaseType, pow: number, reverse?: boolean) => {
  const dur = Math.max(50, Math.min(10000, duration || 400));
  const w = 240;
  const h = 140;
  const pad = 20;
  const usableW = w - pad * 2;
  const usableH = h - pad * 2;
  const pts = Array.from({ length: 120 }, (_, i) => {
    const t = i / 119;
    const easedBase = easeSample(t, ease, pow);
    const eased = reverse ? 1 - easedBase : easedBase;
    const x = pad + t * usableW;
    const y = h - pad - eased * usableH;
    return { x, y };
  });
  const path = `M ${pts.map((p) => `${p.x.toFixed(2)} ${p.y.toFixed(2)}`).join(' L ')}`;
  const area = `${path} L ${pad + usableW} ${h - pad} L ${pad} ${h - pad} Z`;
  return { path, area, dur, w, h, pad, usableW, usableH };
};

const RampGraph = ({ duration, reverse, pow = 2, ease = 'linear' }: { duration?: number; reverse?: boolean; pow?: number; ease?: EaseType }) => {
  const { path, area, dur, w, h, pad, usableW, usableH } = useMemo(
    () => buildRampPaths(duration ?? 0, ease, pow, reverse),
    [duration, ease, pow, reverse],
  );
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  const gridId = reverse ? 'rampGridOff' : 'rampGridOn';
  const accent = reverse ? '#f97316' : '#5be6ff';
  return (
    <div className="relative overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner">
      <div className="relative w-full" style={{ paddingTop: '62%' }}>
        <svg viewBox={`0 0 ${w} ${h}`} className="absolute inset-0 h-full w-full">
          <defs>
            <linearGradient id={gradientId} x1="0%" x2="100%" y1="0%" y2="0%">
              <stop offset="0%" stopColor={accent} stopOpacity="0.35" />
              <stop offset="100%" stopColor={accent} stopOpacity="0.8" />
            </linearGradient>
            <linearGradient id="bgFade" x1="0%" x2="0%" y1="0%" y2="100%">
              <stop offset="0%" stopColor={accent} stopOpacity="0.08" />
              <stop offset="100%" stopColor={accent} stopOpacity="0.02" />
            </linearGradient>
            <pattern id={gridId} width="24" height="24" patternUnits="userSpaceOnUse">
              <path d="M24 0H0V24" fill="none" stroke="rgba(255,255,255,0.05)" strokeWidth="1" />
            </pattern>
          </defs>
          <rect x="0" y="0" width={w} height={h} fill={`url(#${gridId})`} />
          <rect x={pad} y={pad} width={usableW} height={usableH} rx="10" fill="url(#bgFade)" opacity="0.08" />
          <motion.path
            d={area}
            fill={`url(#${gradientId})`}
            opacity="0.3"
            initial={false}
            animate={{ d: area }}
            transition={{ type: 'spring', stiffness: 120, damping: 22 }}
          />
          <motion.path
            d={path}
            fill="none"
            stroke={`url(#${gradientId})`}
            strokeWidth="5"
            strokeLinecap="round"
            initial={false}
            animate={{ d: path }}
            transition={{ type: 'spring', stiffness: 160, damping: 28 }}
          />
          <line x1={pad} y1={h - pad} x2={pad + usableW} y2={h - pad} stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
          <line x1={pad} y1={pad} x2={pad} y2={h - pad} stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
          <text x={pad} y={h - 4} className="text-muted" fill="currentColor" fontSize="10">
            0 ms
          </text>
          <text x={pad + usableW - 36} y={h - 4} className="text-muted" fill="currentColor" fontSize="10">
            {dur} ms
          </text>
          <text x={pad + 6} y={pad + 12} className="text-text" fill="currentColor" fontSize="11">
            {reverse ? 'Off' : 'On'} · {ease} · γ={pow.toFixed(1)}
          </text>
        </svg>
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
  const [rampOnPow, editRampOnPow] = useSyncedValue(status.rampOnPow, {
    tolerance: 0.05,
    send: (v) => sendCmd(`ramp ease on ${rampOnEase} ${v.toFixed(1)}`).catch((e) => console.warn(e)),
  });
  const [rampOffPow, editRampOffPow] = useSyncedValue(status.rampOffPow, {
    tolerance: 0.05,
    send: (v) => sendCmd(`ramp ease off ${rampOffEase} ${v.toFixed(1)}`).catch((e) => console.warn(e)),
  });
  const [rampAmbient, editRampAmbient] = useSyncedValue(status.rampAmbient, {
    tolerance: 0.02,
    send: (v) => sendCmd(`ramp ambient ${v.toFixed(2)}`).catch((e) => console.warn(e)),
  });
  const onPow = rampOnPow ?? 2;
  const offPow = rampOffPow ?? 2;

  useEffect(() => {
    if (typeof status.rampOnMs === 'number') setRampOn(status.rampOnMs);
    if (typeof status.rampOffMs === 'number') setRampOff(status.rampOffMs);
    if (status.rampOnEase) setRampOnEase(status.rampOnEase as EaseType);
    if (status.rampOffEase) setRampOffEase(status.rampOffEase as EaseType);
  }, [status.rampOnMs, status.rampOffMs, status.rampOnEase, status.rampOffEase]);

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
      sendCmd(`ramp ease on ${ease} ${onPow.toFixed(1)}`).catch((e) => console.warn(e));
    } else {
      setRampOffEase(ease);
      sendCmd(`ramp ease off ${ease} ${offPow.toFixed(1)}`).catch((e) => console.warn(e));
    }
  };

  const handleRampPow = (dir: 'on' | 'off', pow: number) => {
    if (Number.isNaN(pow)) return;
    if (dir === 'on') editRampOnPow(pow);
    else editRampOffPow(pow);
  };

  const handleRampAmbient = (val: number) => {
    if (!Number.isNaN(val)) editRampAmbient(val);
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
                <Select value={rampOnEase} onValueChange={(v) => handleRampEase('on', v)}>
                  <SelectTrigger className="w-40">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {EASE_OPTIONS.map((v) => (
                      <SelectItem key={v} value={v}>
                        {v}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ = ${onPow.toFixed(1)}`}
                inputProps={{
                  min: 0.1,
                  max: 10,
                  step: 0.1,
                  value: onPow,
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
                  className="w-32"
                  suffix="ms"
                />
              </div>
              <RampGraph duration={rampOn} pow={onPow} ease={rampOnEase as EaseType} />
            </div>
          </Card>
          <Card className="p-3">
            <CardTitle className="text-base text-text"><Trans k="label.rampOff">Ramp Off</Trans></CardTitle>
            <div className="space-y-3">
              <div className="flex items-center gap-2">
                <Label className="m-0"><Trans k="label.mode">Mode</Trans></Label>
                <Select value={rampOffEase} onValueChange={(v) => handleRampEase('off', v)}>
                  <SelectTrigger className="w-40">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {EASE_OPTIONS.map((v) => (
                      <SelectItem key={v} value={v}>
                        {v}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ = ${offPow.toFixed(1)}`}
                inputProps={{
                  min: 0.1,
                  max: 10,
                  step: 0.1,
                  value: offPow,
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
                  className="w-32"
                  suffix="ms"
                />
              </div>
              <RampGraph duration={rampOff} reverse pow={offPow} ease={rampOffEase as EaseType} />
            </div>
          </Card>
        </div>
        <Card className="p-3">
          <CardTitle className="text-base text-text"><Trans k="label.rampAmbient">Ambient ramps</Trans></CardTitle>
          <div className="space-y-3">
            <SliderRow
              label={<Trans k="label.rampAmbient">Ambient ramps</Trans>}
              description={<Trans k="desc.rampAmbient">Dark rooms stretch ramps by this factor (0 disables)</Trans>}
              valueLabel={`x${(1 + (rampAmbient ?? 0)).toFixed(2)} in dark`}
              inputProps={{
                min: 0,
                max: 5,
                step: 0.05,
                value: rampAmbient ?? 0,
              }}
              onInputChange={handleRampAmbient}
            />
          </div>
        </Card>
      </CardContent>
    </Card>
  );
}
