import { useEffect, useMemo, useRef, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import {
  Activity,
  ArrowLeftCircle,
  ArrowRightCircle,
  ArrowUpDown,
  Blend,
  Flashlight,
  Gauge,
  Palette,
  Pause,
  Radar,
  RefreshCw,
  SlidersHorizontal,
} from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Collapsible } from '@/components/ui/collapsible';
import { SliderRow } from '@/components/ui/slider-row';
import { RangeSliderRow } from '@/components/ui/range-slider-row';
import { PatternPalette } from '@/components/PatternPalette';
import { PatternPreviewGraph } from '@/components/PatternPreviewGraph';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { patternLabel, patternLabels } from '@/data/patterns';
import { Trans, useI18n } from '@/i18n';
import { applyPatternAdjustments, getPatternBrightness } from '@/lib/patternSim';

type OverlayKey = 'speed' | 'margin' | 'fade';

const UPDATE_INTERVAL = 5;
export function LampPowerCard() {
  const { t } = useI18n();
  const { status, sendCmd, sendCmdReliable } = useConnection();
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

  // Pattern controls (formerly ModesCard, merged into this card so Home shows one
  // "Lampe" card instead of two).
  const [pattern, setPattern] = useState(1);
  const [activeOverlay, setActiveOverlay] = useState<OverlayKey | null>(null);
  const [patternSpeed, editPatternSpeed] = useSyncedValue(status.patternSpeed, {
    tolerance: 0.05,
    send: (v) => sendCmd(`pat scale ${v.toFixed(2)}`).catch((e) => console.warn(e)),
  });
  const [patternFade, editPatternFade] = useSyncedValue(status.patternFade, {
    tolerance: 0.05,
    send: (v) => {
      if (v <= 0) {
        sendCmd('pat fade off').catch((e) => console.warn(e));
        return;
      }
      // write-with-response and awaited: see the identical fix/reasoning in
      // ProfileSlotsCard.tsx (memory: profile-show-no-response-investigation) -
      // two writeValueWithoutResponse calls fired back to back can have the
      // first silently dropped on Android with zero error anywhere, so "on"
      // must be confirmed delivered before "amt" goes out.
      sendCmdReliable('pat fade on')
        .then(() => sendCmdReliable(`pat fade amt ${v.toFixed(2)}`))
        .catch((e) => console.warn(e));
    },
  });
  const [margin, setMargin] = useState<[number, number]>([0, 1]);
  const [invert, setInvert] = useState(false);
  const [reactive, setReactive] = useState(false);

  useEffect(() => {
    if (status.currentPattern) setPattern(status.currentPattern);
  }, [status.currentPattern]);

  useEffect(() => {
    if (typeof status.patternInvert === 'boolean') setInvert(status.patternInvert);
  }, [status.patternInvert]);

  useEffect(() => {
    if (typeof status.patternReactive === 'boolean') setReactive(status.patternReactive);
  }, [status.patternReactive]);

  useEffect(() => {
    const lo = typeof status.patternMarginLow === 'number' ? status.patternMarginLow : undefined;
    const hi = typeof status.patternMarginHigh === 'number' ? status.patternMarginHigh : undefined;
    if (lo !== undefined || hi !== undefined) {
      setMargin([lo ?? margin[0], hi ?? margin[1]]);
    }
  }, [status.patternMarginLow, status.patternMarginHigh]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
  }, [status.patternCount]);

  const handlePatternChange = (val: number) => {
    setPattern(val);
    // mode switch itself goes write-with-response (the thing that actually
    // matters here) - the follow-up status echo stays best-effort, same
    // reasoning as the fade fix above.
    sendCmdReliable(`mode ${val}`)
      .then(() => sendCmd('status'))
      .catch((e) => console.warn(e));
  };

  const handlePatternSpeed = (val: number) => {
    editPatternSpeed(Math.max(0.1, Math.min(5, val)));
  };

  const handleMargin = (vals: [number, number]) => {
    setMargin(vals);
    sendCmd(`pat margin ${vals[0].toFixed(2)} ${vals[1].toFixed(2)}`).catch((e) => console.warn(e));
  };

  const handleInvert = (val: boolean) => {
    setInvert(val);
    sendCmd(`pat invert ${val ? "on" : "off"}`).catch((e) => console.warn(e));
  };

  const handleReactive = (val: boolean) => {
    setReactive(val);
    sendCmd(`pat reactive ${val ? "on" : "off"}`).catch((e) => console.warn(e));
  };

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

  const speed = patternSpeed ?? 1;
  const fade = patternFade && patternFade > 0 ? patternFade : 0;
  const simMs = Math.max(0, tick - patternAnchor);
  const rawEnergy = lampOn && status.currentPattern ? getPatternBrightness(status.currentPattern, simMs * speed) : 0;
  const marginLow = margin[0];
  const marginHigh = margin[1];
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
      <CardContent className="space-y-4">
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

        {/* One unified frame: the preview graph is this whole frame's background (not a
            boxed-in sub-element), with the pattern switcher and the param palette floating
            on top of it. A fixed min-height (not width-driven aspect ratio) keeps the right-edge
            palette's 4 stacked buttons from overflowing the frame on narrow/mobile widths. */}
        <div className="relative" onClick={() => activeOverlay && setActiveOverlay(null)}>
          <div className="relative min-h-[320px] overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner">
            <PatternPreviewGraph
              pattern={pattern}
              speed={patternSpeed ?? 1}
              marginLow={margin[0]}
              marginHigh={margin[1]}
              invert={invert}
              fade={patternFade ?? 0}
            />
            <div className="pointer-events-none absolute inset-x-0 top-0 flex items-center gap-2 p-2">
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  const prevIdx = pattern > 1 ? pattern - 1 : patternOptions.length;
                  handlePatternChange(prevIdx);
                }}
                className="pointer-events-auto rounded-full border border-border/60 bg-black/30 p-2 text-muted backdrop-blur-sm transition hover:border-accent/60 hover:text-text"
              >
                <ArrowLeftCircle className="h-4 w-4" />
              </button>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  setPaletteOpen(true);
                }}
                title="Open pattern palette"
                className="pointer-events-auto flex min-w-0 flex-1 items-center justify-between gap-2 rounded-xl border border-border/60 bg-black/30 px-3 py-2 text-left backdrop-blur-sm transition hover:border-accent/60"
              >
                <div className="flex min-w-0 flex-col items-start leading-tight">
                  <span className="text-xs text-muted">Pattern {pattern}</span>
                  <span className="truncate text-sm font-semibold text-text drop-shadow">
                    {patternLabel(pattern, status.patternName)}
                  </span>
                </div>
                <Palette className="h-4 w-4 shrink-0 text-muted" />
              </button>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation();
                  const nextIdx = pattern < patternOptions.length ? pattern + 1 : 1;
                  handlePatternChange(nextIdx);
                }}
                className="pointer-events-auto rounded-full border border-border/60 bg-black/30 p-2 text-muted backdrop-blur-sm transition hover:border-accent/60 hover:text-text"
              >
                <ArrowRightCircle className="h-4 w-4" />
              </button>
            </div>
            {/* Overlay palette: Speed/Margin/Fade carry a value and open their editor as a
                popup anchored to this corner on click; Invert has none, so it just toggles.
                Offset below the switcher row above so the two never overlap. */}
            <div className="absolute right-2 top-16 flex flex-col gap-1.5" onClick={(e) => e.stopPropagation()}>
              {(
                [
                  { key: 'speed' as const, icon: Gauge, label: t('label.speed', 'Speed'), value: `${(patternSpeed ?? 1).toFixed(1)}x` },
                  {
                    key: 'margin' as const,
                    icon: SlidersHorizontal,
                    label: t('label.patternMargin', 'Pattern Margin'),
                    value: `${Math.round(margin[0] * 100)}-${Math.round(margin[1] * 100)}%`,
                  },
                  {
                    key: 'fade' as const,
                    icon: Blend,
                    label: t('label.fadeMul', 'Pattern Fade'),
                    value: (patternFade ?? 0) > 0 ? `${(patternFade ?? 0).toFixed(1)}x` : t('label.off', 'Off'),
                  },
                ]
              ).map((item) => (
                <button
                  key={item.key}
                  type="button"
                  title={item.label}
                  onClick={() => setActiveOverlay((cur) => (cur === item.key ? null : item.key))}
                  className={`flex flex-col items-center gap-0.5 rounded-xl border px-2 py-1.5 text-[10px] leading-none backdrop-blur-sm transition ${
                    activeOverlay === item.key
                      ? 'border-accent bg-accent/20 text-accent'
                      : 'border-border/60 bg-black/30 text-muted hover:border-accent/60 hover:text-text'
                  }`}
                >
                  <item.icon className="h-4 w-4" />
                  <span className="font-mono">{item.value}</span>
                </button>
              ))}
              <button
                type="button"
                title={t('label.patternInvert', 'Invert Pattern')}
                onClick={() => handleInvert(!invert)}
                className={`flex items-center justify-center rounded-xl border px-2 py-2 backdrop-blur-sm transition ${
                  invert
                    ? 'border-accent bg-accent/20 text-accent'
                    : 'border-border/60 bg-black/30 text-muted hover:border-accent/60 hover:text-text'
                }`}
              >
                <ArrowUpDown className="h-4 w-4" />
              </button>
            </div>
          </div>
          {/* Rendered outside the frame's overflow-hidden above so the popup can't get clipped. */}
          <AnimatePresence>
            {activeOverlay && (
              <motion.div
                key={activeOverlay}
                initial={{ opacity: 0, x: -16, y: 16 }}
                animate={{ opacity: 1, x: 0, y: 0 }}
                exit={{ opacity: 0, x: -16, y: 16 }}
                transition={{ duration: 0.18 }}
                onClick={(e) => e.stopPropagation()}
                className="absolute bottom-2 left-2 z-20 w-[min(280px,calc(100%-1rem))] rounded-xl border border-border bg-panel/95 p-3 shadow-xl backdrop-blur"
              >
                {activeOverlay === 'speed' && (
                  <SliderRow
                    label={<Trans k="label.speed">Speed</Trans>}
                    description={<Trans k="desc.patternSpeed">Pattern speed multiplier</Trans>}
                    inputProps={{ min: 0.1, max: 5, step: 0.1, value: patternSpeed ?? 1 }}
                    onInputChange={(val) => handlePatternSpeed(val)}
                  />
                )}
                {activeOverlay === 'margin' && (
                  <RangeSliderRow
                    label={<Trans k="label.patternMargin">Pattern Margin</Trans>}
                    description={<Trans k="desc.patternMargin">Clamp pattern output between two levels</Trans>}
                    min={0}
                    max={1}
                    step={0.01}
                    values={margin}
                    onChange={handleMargin}
                  />
                )}
                {activeOverlay === 'fade' && (
                  <SliderRow
                    label={<Trans k="label.fadeMul">Pattern Fade</Trans>}
                    description={<Trans k="desc.patternFade">Smooth transitions between patterns</Trans>}
                    valueLabel={(patternFade ?? 0) > 0 ? `${(patternFade ?? 0).toFixed(1)}x` : 'Off'}
                    inputProps={{ min: 0, max: 2, step: 0.01, value: patternFade ?? 0 }}
                    onInputChange={(val) => editPatternFade(Math.max(0, val))}
                  />
                )}
              </motion.div>
            )}
          </AnimatePresence>
        </div>
        <Collapsible title={<Trans k="label.moreOptions">More options</Trans>}>
          <div className="flex flex-wrap gap-2">
            <label className="pill cursor-pointer">
              <input
                type="checkbox"
                className="accent-accent"
                onChange={(e) =>
                  sendCmd(`auto ${e.target.checked ? "on" : "off"}`)
                }
              />{" "}
              <span className="inline-flex items-center gap-1">
                <Activity className="h-4 w-4" /> AutoCycle
              </span>
            </label>
            <label className="pill cursor-pointer">
              <input
                type="checkbox"
                className="accent-accent"
                checked={reactive}
                onChange={(e) => handleReactive(e.target.checked)}
              />{" "}
              <span className="inline-flex items-center gap-1">
                <Radar className="h-4 w-4" />{" "}
                <Trans k="label.patternReactive">Sensor-reactive</Trans>
              </span>
            </label>
          </div>
        </Collapsible>
        <PatternPalette open={paletteOpen} setOpen={setPaletteOpen} />
      </CardContent>
    </Card>
  );
}
