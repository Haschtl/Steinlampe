import { useEffect, useRef, useState } from 'react';
import { motion } from 'framer-motion';
import { Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { PatternPalette } from '@/components/PatternPalette';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { patternLabel } from '@/data/patterns';
import { Trans } from '@/i18n';
import { applyPatternAdjustments, getPatternBrightness } from '@/lib/patternSim';

const UPDATE_INTERVAL = 5;
export function LampPowerCard() {
  const { status, sendCmd } = useConnection();
  const [paletteOpen, setPaletteOpen] = useState(false);
  // Optimistic brightness: hold the dragged value until the device's (possibly
  // ramping) feedback converges, so the slider doesn't flicker back mid-ramp.
  // Sends are throttled while dragging; the final value always lands.
  const [brightness, editBrightness] = useSyncedValue(status.brightness, {
    tolerance: 1,
    send: (v) => sendCmd(`bri ${v}`).catch((e) => console.warn(e)),
  });
  const [lampOn, setLampOn] = useState(false);
  const canSync = !!status.switchState && !!status.lampState && status.switchState !== status.lampState;
  const hasSwitch = status.hasSwitch !== false && status.switchState !== undefined && status.switchState !== 'N/A';
  const hasPoti = status.hasPoti !== false && status.potiVal !== undefined;
  const potiPct = typeof status.potiVal === 'number' ? Math.round(status.potiVal * 100) : null;
  const hasTouch = status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A';
  const hasPush = status.hasPush !== false && status.pushEnabled !== undefined && status.pushEnabled !== null;
  const hasMusic = status.hasMusic !== false && status.musicEnabled !== undefined && status.musicEnabled !== null;
  const hasLight = status.hasLight !== false;
  const ambientPct = (() => {
    if (typeof status.lightRaw === 'number' && typeof status.lightRawMin === 'number' && typeof status.lightRawMax === 'number' && status.lightRawMax > status.lightRawMin) {
      const n = (status.lightRaw - status.lightRawMin) / (status.lightRawMax - status.lightRawMin);
      const pct = Math.round(Math.min(1, Math.max(0, n)) * 100);
      return pct;
    }
    return null;
  })();
  const [tick, setTick] = useState(0);
  const [patternAnchor, setPatternAnchor] = useState(0);
  const lightActive = status.lightEnabled && status.hasLight !== false;
  const musicActive = status.musicEnabled && status.hasMusic !== false;
  const touchDimActive = status.touchState === 'TOUCHDIM' || status.touchState === 'DIM';
  const presenceActive = !!(status.presenceBle && status.presenceBle.toUpperCase().startsWith('ON'));

  useEffect(() => {
    setLampOn(status.lampState === 'ON');
  }, [status.lampState]);

  const patternText = lampOn && status.currentPattern
    ? `${patternLabel(status.currentPattern, status.patternName)}`
    : '';

  useEffect(() => {
    let raf: number;
    let last = 0;
    const loop = (t: number) => {
      if (t - last > UPDATE_INTERVAL) {
        setTick(t);
        last = t;
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  useEffect(() => {
    if (typeof status.patternElapsedMs === 'number') {
      // Align UI simulation clock with controller pattern time
      setPatternAnchor(performance.now() - status.patternElapsedMs);
    }
  }, [status.patternElapsedMs, status.currentPattern]);

  const speed = status.patternSpeed ?? 1;
  const fade = status.patternFade && status.patternFade > 0 ? status.patternFade : 0;
  const simMs = Math.max(0, tick - patternAnchor);
  const rawEnergy = lampOn && status.currentPattern ? getPatternBrightness(status.currentPattern, simMs * speed) : 0;
  const invert = status.patternInvert ?? false;
  const marginLow = typeof status.patternMarginLow === 'number' ? status.patternMarginLow : 0;
  const marginHigh = typeof status.patternMarginHigh === 'number' ? status.patternMarginHigh : 1;
  const adjustedEnergy = applyPatternAdjustments(rawEnergy, { invert, marginLow, marginHigh });
  const [energy, setEnergy] = useState(0);

  useEffect(() => {
    if (!fade) {
      setEnergy(adjustedEnergy);
      return;
    }
    const base = 400;
    setEnergy((prev) => {
      const alpha = Math.min(1, 50 / (base * fade));
      return prev + (adjustedEnergy - prev) * alpha;
    });
  }, [adjustedEnergy, fade]);

  const handleBrightness = (value: number) => {
    editBrightness(Math.min(100, Math.max(1, Math.round(value))));
  };

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  // The power switch and the brightness slider are the same control: a horizontal
  // fill that tracks brightness, tapped to toggle power and dragged to set the level.
  // Pointer handlers (not onClick) so a drag can be told apart from a tap - a plain
  // click handler can't see the movement in between down and up.
  const sliderRef = useRef<HTMLButtonElement | null>(null);
  const dragRef = useRef<{ moved: boolean; startX: number } | null>(null);
  const DRAG_THRESHOLD_PX = 4;

  const brightnessFromClientX = (clientX: number) => {
    const el = sliderRef.current;
    if (!el) return brightness ?? 70;
    const rect = el.getBoundingClientRect();
    const pct = (clientX - rect.left) / rect.width;
    return Math.min(100, Math.max(1, Math.round(pct * 100)));
  };

  const handleSliderPointerDown = (e: React.PointerEvent<HTMLButtonElement>) => {
    sliderRef.current?.setPointerCapture(e.pointerId);
    dragRef.current = { moved: false, startX: e.clientX };
  };

  const handleSliderPointerMove = (e: React.PointerEvent<HTMLButtonElement>) => {
    const drag = dragRef.current;
    if (!drag) return;
    if (!drag.moved && Math.abs(e.clientX - drag.startX) > DRAG_THRESHOLD_PX) drag.moved = true;
    if (drag.moved) handleBrightness(brightnessFromClientX(e.clientX));
  };

  const endSliderDrag = (e: React.PointerEvent<HTMLButtonElement>) => {
    const drag = dragRef.current;
    dragRef.current = null;
    if (drag && !drag.moved) handleLampToggle(!lampOn);
  };

  const brightnessPct = Math.round(brightness ?? 70);

  return (
    <Card>
      <CardHeader className="flex-wrap">
        <CardTitle>
          <Trans k="title.lamp">Lamp</Trans>
        </CardTitle>
        <div className="flex flex-1 flex-wrap items-center justify-end gap-1.5">
          {hasSwitch && (
            <span className="chip-muted inline-flex items-center gap-1 text-[11px] px-1.5 py-0.5">
              Switch: {status.switchState ?? "--"}
              <Button size="sm" variant="ghost" onClick={() => sendCmd("sync")} disabled={!canSync} className="h-5 px-1 text-[11px]">
                <RefreshCw className="h-3 w-3" />
              </Button>
            </span>
          )}
          {hasPoti && (
            <span className="chip-muted inline-flex items-center gap-1 text-[11px] px-1.5 py-0.5">
              Poti: {potiPct !== null ? `${potiPct}%` : "--"}
              <Button
                size="sm"
                variant="ghost"
                className="h-5 px-1 text-[11px]"
                onClick={() => sendCmd(`bri ${Math.max(1, Math.min(100, potiPct ?? 0))}`)}
              >
                <RefreshCw className="h-3 w-3" />
              </Button>
            </span>
          )}
          {hasTouch && <span className="chip-muted text-[11px] px-1.5 py-0.5">Touch: {status.touchState ?? "--"}</span>}
          {hasPush && <span className="chip-muted text-[11px] px-1.5 py-0.5">Push: {status.pushEnabled ? "ON" : "OFF"}</span>}
          {lightActive && <span className="chip-muted text-[11px] px-1.5 py-0.5"><Trans k="title.light">Light</Trans></span>}
          {typeof ambientPct === 'number' && <span className="chip-muted text-[11px] px-1.5 py-0.5">Ambient: {ambientPct}%</span>}
          {hasMusic && <span className="chip-muted text-[11px] px-1.5 py-0.5"><Trans k="title.music">Music</Trans></span>}
          {touchDimActive && <span className="chip-muted text-[11px] px-1.5 py-0.5">Touch-Dim</span>}
          {presenceActive && <span className="chip-muted text-[11px] px-1.5 py-0.5"><Trans k="title.presenceBle">Presence (BLE)</Trans></span>}
        </div>
      </CardHeader>
      <CardContent>
        <motion.button
          type="button"
          ref={sliderRef}
          onPointerDown={handleSliderPointerDown}
          onPointerMove={handleSliderPointerMove}
          onPointerUp={endSliderDrag}
          onPointerCancel={endSliderDrag}
          className="relative flex w-full items-center justify-between overflow-hidden rounded-2xl border border-accent/50 px-6 py-6 text-left text-lg font-semibold shadow-[0_15px_40px_rgba(0,0,0,0.35)] focus:outline-none focus:ring-2 focus:ring-accent focus:ring-offset-2"
          style={{ touchAction: 'none' }}
          animate={{
            scale: lampOn ? 1.01 : 1,
            boxShadow: lampOn
              ? "0 0 30px rgba(255,200,120,0.35), 0 15px 50px rgba(0,0,0,0.35)"
              : "0 10px 25px rgba(0,0,0,0.3)",
          }}
        >
          {/* Dark track - the "unfilled" rest of the brightness range. */}
          <div className="absolute inset-0 bg-[linear-gradient(120deg,rgba(40,50,70,0.7),rgba(20,24,36,0.8))]" />
          {/* Brightness fill: grows left-to-right with the level, dim while off so the
              level stays visible (tapping turns the lamp back on at this same level). */}
          <motion.div
            className="absolute inset-y-0 left-0"
            initial={false}
            animate={{ width: `${brightnessPct}%` }}
            transition={dragRef.current ? { duration: 0 } : { type: 'spring', stiffness: 260, damping: 30 }}
            style={{
              background: lampOn
                ? "linear-gradient(135deg, rgba(255,214,134,0.9), rgba(255,140,90,0.75))"
                : "rgba(255,255,255,0.08)",
            }}
          />
          <motion.div
            className="absolute -inset-10 opacity-40"
            initial={{ rotate: 0 }}
            animate={{ rotate: lampOn ? 12 : -8 }}
            transition={{ duration: 1.4, ease: "easeInOut" }}
            style={{
              background:
                lampOn
                  ? "radial-gradient(45% 35% at 50% 50%, rgba(var(--accent-rgb),0.4), transparent 60%)"
                  : "radial-gradient(45% 35% at 50% 50%, rgba(var(--accent-rgb),0.2), transparent 60%)",
              filter: "blur(45px)",
              opacity: lampOn ? energy : 0.35,
            }}
          />
          <div className="relative z-10 flex items-center gap-3 text-shadow">
            {lampOn ? (
              <Pause className="h-6 w-6 text-amber-200 drop-shadow" />
            ) : (
              <Flashlight className="h-6 w-6 text-sky-200 drop-shadow" />
            )}
            <div className="flex flex-col leading-tight">
              <span className="text-base text-muted">
                <Trans k="title.lamp">Lamp</Trans>
              </span>
              <span className="text-2xl tracking-wide">
                {lampOn ? (
                  <Trans k="btn.on">On</Trans>
                ) : (
                  <Trans k="btn.off">Off</Trans>
                )}
              </span>
            </div>
          </div>
          <div className="relative z-10 flex items-center gap-3">
            <span className="rounded-full bg-black/30 px-3 py-1 text-sm text-amber-100 shadow-inner">
              {brightnessPct}%
            </span>
            {lampOn && status.currentPattern && (
              <button
                type="button"
                onPointerDown={(e) => e.stopPropagation()}
                onClick={(e) => { e.stopPropagation(); setPaletteOpen(true); }}
                className="rounded-full bg-amber-300/25 px-3 py-1 text-sm text-amber-50 shadow-inner underline decoration-amber-100/70 decoration-dotted"
              >
                {patternText ? patternText : `Mode ${status.currentPattern}`}
              </button>
            )}
          </div>
        </motion.button>
        <PatternPalette open={paletteOpen} setOpen={setPaletteOpen} />
      </CardContent>
    </Card>
  );
}
