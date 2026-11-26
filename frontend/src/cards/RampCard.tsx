import { useMemo, useEffect, useRef, useState } from 'react';
import { motion } from 'framer-motion';
import { ChevronDown, Palette } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { LiveOutputGraph } from '@/components/LiveOutputGraph';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { Trans } from '@/i18n';
import { RampEaseDialog } from '@/components/RampEaseDialog';
import { buildRampPaths, easeSample, EaseType } from '@/lib/rampEase';

const POW_MIN = 0.1;
const POW_MAX = 10;
// Drag sensitivity for the gamma handle: pixels of vertical drag per unit of pow.
const POW_DRAG_PX_PER_UNIT = 12;

const RampGraph = ({
  duration,
  reverse,
  pow = 2,
  ease = 'linear',
  interactive,
  onPowChange,
}: {
  duration?: number;
  reverse?: boolean;
  pow?: number;
  ease?: EaseType;
  interactive?: boolean;
  onPowChange?: (v: number) => void;
}) => {
  const { path, area, dur, w, h, pad, usableW, usableH } = useMemo(
    () => buildRampPaths(duration ?? 0, ease, pow, reverse),
    [duration, ease, pow, reverse],
  );
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  const gridId = reverse ? 'rampGridOff' : 'rampGridOn';
  const accent = reverse ? '#f97316' : '#5be6ff';

  const handlePos = useMemo(() => {
    const t = 0.5;
    const base = easeSample(t, ease, pow);
    const eased = reverse ? 1 - base : base;
    return { x: pad + t * usableW, y: h - pad - eased * usableH };
  }, [ease, pow, reverse, pad, usableW, h, usableH]);

  const dragRef = useRef<{ startY: number; startPow: number } | null>(null);

  useEffect(() => {
    if (!interactive || !onPowChange) return;
    const onMove = (e: PointerEvent) => {
      if (!dragRef.current) return;
      const deltaPx = dragRef.current.startY - e.clientY;
      const next = dragRef.current.startPow + deltaPx / POW_DRAG_PX_PER_UNIT;
      onPowChange(Math.min(POW_MAX, Math.max(POW_MIN, next)));
    };
    const onUp = () => {
      dragRef.current = null;
    };
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    window.addEventListener('pointercancel', onUp);
    return () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      window.removeEventListener('pointercancel', onUp);
    };
  }, [interactive, onPowChange]);

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
          {interactive && onPowChange && (
            <circle
              cx={handlePos.x}
              cy={handlePos.y}
              r="7"
              fill={accent}
              stroke="white"
              strokeWidth="2"
              style={{ cursor: 'ns-resize', touchAction: 'none' }}
              onPointerDown={(e) => {
                e.preventDefault();
                (e.target as Element).setPointerCapture?.(e.pointerId);
                dragRef.current = { startY: e.clientY, startPow: pow };
              }}
            />
          )}
        </svg>
      </div>
      {interactive && onPowChange && (
        <div className="px-2 pb-1 text-right text-[10px] text-muted">
          <Trans k="label.dragGamma">Drag the dot to shape the curve</Trans>
        </div>
      )}
    </div>
  );
};

export function RampCard() {
  const { status, sendCmd } = useConnection();
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [rampOnEase, setRampOnEase] = useState('linear');
  const [rampOffEase, setRampOffEase] = useState('linear');
  const [onPaletteOpen, setOnPaletteOpen] = useState(false);
  const [offPaletteOpen, setOffPaletteOpen] = useState(false);
  const [onExpanded, setOnExpanded] = useState(false);
  const [offExpanded, setOffExpanded] = useState(false);
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
        <LiveOutputGraph />
        <div className="grid gap-3 md:grid-cols-2">
          <Card className="p-3">
            <button
              type="button"
              className="flex w-full items-center justify-between gap-2 text-left"
              onClick={() => setOnExpanded((v) => !v)}
              aria-expanded={onExpanded}
            >
              <CardTitle className="text-base text-text"><Trans k="label.rampOn">Ramp On</Trans></CardTitle>
              <ChevronDown className={`h-4 w-4 text-muted transition-transform ${onExpanded ? 'rotate-0' : '-rotate-90'}`} />
            </button>
            <div className="space-y-3">
              <RampGraph
                duration={rampOn}
                pow={onPow}
                ease={rampOnEase as EaseType}
                interactive={onExpanded}
                onPowChange={(v) => handleRampPow('on', v)}
              />
              {onExpanded && (
                <>
                  <div className="flex items-center gap-2">
                    <Label className="m-0"><Trans k="label.mode">Mode</Trans></Label>
                    <Button
                      variant="ghost"
                      className="flex-1 min-w-0 justify-between px-3 py-2"
                      onClick={() => setOnPaletteOpen(true)}
                      title="Open ramp mode palette"
                    >
                      <span className="truncate text-sm font-semibold">{rampOnEase}</span>
                      <Palette className="h-4 w-4 text-muted" />
                    </Button>
                  </div>
                  <RampEaseDialog
                    open={onPaletteOpen}
                    setOpen={setOnPaletteOpen}
                    value={rampOnEase}
                    pow={onPow}
                    title="Ramp On - Mode"
                    onSelect={(ease) => handleRampEase('on', ease)}
                  />
                  <div className="flex items-center gap-2">
                    <Label className="m-0"><Trans k="label.pow">Power</Trans></Label>
                    <Input
                      type="number"
                      min={0.1}
                      max={10}
                      step={0.1}
                      value={onPow}
                      onChange={(e) => handleRampPow('on', Number(e.target.value))}
                      className="w-24"
                      suffix="γ"
                    />
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
                </>
              )}
            </div>
          </Card>
          <Card className="p-3">
            <button
              type="button"
              className="flex w-full items-center justify-between gap-2 text-left"
              onClick={() => setOffExpanded((v) => !v)}
              aria-expanded={offExpanded}
            >
              <CardTitle className="text-base text-text"><Trans k="label.rampOff">Ramp Off</Trans></CardTitle>
              <ChevronDown className={`h-4 w-4 text-muted transition-transform ${offExpanded ? 'rotate-0' : '-rotate-90'}`} />
            </button>
            <div className="space-y-3">
              <RampGraph
                duration={rampOff}
                reverse
                pow={offPow}
                ease={rampOffEase as EaseType}
                interactive={offExpanded}
                onPowChange={(v) => handleRampPow('off', v)}
              />
              {offExpanded && (
                <>
                  <div className="flex items-center gap-2">
                    <Label className="m-0"><Trans k="label.mode">Mode</Trans></Label>
                    <Button
                      variant="ghost"
                      className="flex-1 min-w-0 justify-between px-3 py-2"
                      onClick={() => setOffPaletteOpen(true)}
                      title="Open ramp mode palette"
                    >
                      <span className="truncate text-sm font-semibold">{rampOffEase}</span>
                      <Palette className="h-4 w-4 text-muted" />
                    </Button>
                  </div>
                  <RampEaseDialog
                    open={offPaletteOpen}
                    setOpen={setOffPaletteOpen}
                    value={rampOffEase}
                    pow={offPow}
                    reverse
                    title="Ramp Off - Mode"
                    onSelect={(ease) => handleRampEase('off', ease)}
                  />
                  <div className="flex items-center gap-2">
                    <Label className="m-0"><Trans k="label.pow">Power</Trans></Label>
                    <Input
                      type="number"
                      min={0.1}
                      max={10}
                      step={0.1}
                      value={offPow}
                      onChange={(e) => handleRampPow('off', Number(e.target.value))}
                      className="w-24"
                      suffix="γ"
                    />
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
                </>
              )}
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
