import { ReactNode, useMemo, useEffect, useState } from 'react';
import { motion } from 'framer-motion';
import { Palette, Pencil } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { LiveOutputGraph } from '@/components/LiveOutputGraph';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { Trans } from '@/i18n';
import { RampEaseDialog } from '@/components/RampEaseDialog';
import { buildRampPaths, EaseType } from '@/lib/rampEase';

const RampGraph = ({ duration, reverse, pow = 2, ease = 'linear' }: {
  duration?: number;
  reverse?: boolean;
  pow?: number;
  ease?: EaseType;
}) => {
  const { path, area, dur, w, h, pad, usableW, usableH } = useMemo(
    () => buildRampPaths(duration ?? 0, ease, pow, reverse),
    [duration, ease, pow, reverse],
  );
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  const gridId = reverse ? 'rampGridOff' : 'rampGridOn';
  const accent = reverse ? '#f97316' : '#5be6ff';

  return (
    <svg viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none" className="absolute inset-0 h-full w-full">
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
    </svg>
  );
};

// The plot is the card's own background (filling the whole frame, not boxed in separately);
// title/duration/ease float over it in frosted glass chips, and the whole tile is a button
// that opens the editor as a popup (no inline expand section).
const RampTile = ({
  title,
  duration,
  reverse,
  pow,
  ease,
  onOpen,
}: {
  title: ReactNode;
  duration?: number;
  reverse?: boolean;
  pow: number;
  ease: EaseType;
  onOpen: () => void;
}) => (
  <motion.button
    type="button"
    onClick={onOpen}
    whileHover={{ scale: 1.015 }}
    whileTap={{ scale: 0.99 }}
    transition={{ type: 'spring', stiffness: 300, damping: 24 }}
    className="group relative block min-h-[150px] w-full overflow-hidden rounded-2xl border border-border/70 bg-panel/70 text-left shadow-[0_10px_30px_rgba(0,0,0,0.25)] focus:outline-none focus:ring-2 focus:ring-accent"
  >
    <RampGraph duration={duration} reverse={reverse} pow={pow} ease={ease} />
    <div
      className="pointer-events-none absolute inset-0 opacity-0 transition-opacity duration-300 group-hover:opacity-100"
      style={{ background: `radial-gradient(60% 50% at 50% 100%, ${reverse ? 'rgba(249,115,22,0.18)' : 'rgba(91,230,255,0.18)'}, transparent 70%)` }}
    />
    <div className="pointer-events-none absolute inset-x-0 top-0 bg-gradient-to-b from-black/35 to-transparent p-2.5">
      <div className="flex items-center justify-between gap-2">
        <span className="rounded-full border border-white/10 bg-black/30 px-2.5 py-1 text-sm font-semibold text-text backdrop-blur-sm">
          {title}
        </span>
        <span className="rounded-full border border-white/10 bg-black/30 p-1.5 text-muted/90 backdrop-blur-sm transition group-hover:text-accent">
          <Pencil className="h-3.5 w-3.5" />
        </span>
      </div>
    </div>
    <div className="pointer-events-none absolute inset-x-2.5 bottom-2 flex items-center justify-between text-[11px]">
      <span className="rounded-full border border-white/10 bg-black/30 px-2 py-0.5 text-muted backdrop-blur-sm">
        {ease} · γ={pow.toFixed(1)}
      </span>
      <span className="rounded-full border border-white/10 bg-black/30 px-2 py-0.5 text-muted backdrop-blur-sm">
        {duration ?? 0} ms
      </span>
    </div>
  </motion.button>
);

export function RampCard() {
  const { status, sendCmd } = useConnection();
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [rampOnEase, setRampOnEase] = useState('linear');
  const [rampOffEase, setRampOffEase] = useState('linear');
  const [onPaletteOpen, setOnPaletteOpen] = useState(false);
  const [offPaletteOpen, setOffPaletteOpen] = useState(false);
  const [onEditorOpen, setOnEditorOpen] = useState(false);
  const [offEditorOpen, setOffEditorOpen] = useState(false);
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
  // Ambient ramps only mean anything on a lamp with an actual light sensor -
  // same "don't claim a feature before we actually know it's there, but
  // hide it outright once the firmware says it isn't" pattern already used
  // for poti/push/music elsewhere (see LampPowerCard).
  const hasLight = status.hasLight !== false && status.lightEnabled !== undefined;

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
      <CardContent>
        <LiveOutputGraph />
        <div className="grid gap-3 md:grid-cols-2">
          <RampTile
            title={<Trans k="label.rampOn">Ramp On</Trans>}
            duration={rampOn}
            pow={onPow}
            ease={rampOnEase as EaseType}
            onOpen={() => setOnEditorOpen(true)}
          />
          <RampTile
            title={<Trans k="label.rampOff">Ramp Off</Trans>}
            duration={rampOff}
            reverse
            pow={offPow}
            ease={rampOffEase as EaseType}
            onOpen={() => setOffEditorOpen(true)}
          />
        </div>

        <Dialog open={onEditorOpen} onOpenChange={setOnEditorOpen}>
          <DialogContent>
            <DialogHeader>
              <DialogTitle><Trans k="label.rampOn">Ramp On</Trans></DialogTitle>
            </DialogHeader>
            <div className="space-y-3">
              <div className="relative overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner" style={{ paddingTop: '62%' }}>
                <RampGraph duration={rampOn} pow={onPow} ease={rampOnEase as EaseType} />
              </div>
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ=${onPow.toFixed(1)}`}
                inputProps={{ min: 0.1, max: 10, step: 0.1, value: onPow }}
                onInputChange={(val) => handleRampPow('on', val)}
              />
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
            </div>
          </DialogContent>
        </Dialog>

        <Dialog open={offEditorOpen} onOpenChange={setOffEditorOpen}>
          <DialogContent>
            <DialogHeader>
              <DialogTitle><Trans k="label.rampOff">Ramp Off</Trans></DialogTitle>
            </DialogHeader>
            <div className="space-y-3">
              <div className="relative overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner" style={{ paddingTop: '62%' }}>
                <RampGraph duration={rampOff} reverse pow={offPow} ease={rampOffEase as EaseType} />
              </div>
              <SliderRow
                label={<Trans k="label.pow">Power</Trans>}
                valueLabel={`γ=${offPow.toFixed(1)}`}
                inputProps={{ min: 0.1, max: 10, step: 0.1, value: offPow }}
                onInputChange={(val) => handleRampPow('off', val)}
              />
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
            </div>
          </DialogContent>
        </Dialog>
        {hasLight && (
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
        )}
      </CardContent>
    </Card>
  );
}
