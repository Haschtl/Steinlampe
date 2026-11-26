import { useEffect, useMemo, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { Activity, ArrowLeftCircle, ArrowRightCircle, ArrowUpDown, Blend, Gauge, Palette, Radar, SlidersHorizontal } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { RangeSliderRow } from '@/components/ui/range-slider-row';
import { Collapsible } from '@/components/ui/collapsible';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { patternLabel, patternLabels } from '@/data/patterns';
import { Trans, useI18n } from '@/i18n';
import { PatternPalette } from '@/components/PatternPalette';
import { PatternPreviewGraph } from '@/components/PatternPreviewGraph';

type OverlayKey = 'speed' | 'margin' | 'fade';

export function ModesCard() {
  const { t } = useI18n();
  const { status, sendCmd } = useConnection();
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
      sendCmd('pat fade on').catch((e) => console.warn(e));
      sendCmd(`pat fade amt ${v.toFixed(2)}`).catch((e) => console.warn(e));
    },
  });
  const [paletteOpen, setPaletteOpen] = useState(false);
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
    sendCmd(`mode ${val}`)
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

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.pattern">Pattern</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
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
