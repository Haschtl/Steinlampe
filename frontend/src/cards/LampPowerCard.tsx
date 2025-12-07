import { useEffect, useState } from 'react';
import { motion } from 'framer-motion';
import { Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { PatternPalette } from '@/components/PatternPalette';
import { useConnection } from '@/context/connection';
import { patternLabel } from '@/data/patterns';
import { Trans } from '@/i18n';
import { getPatternBrightness } from '@/lib/patternSim';

export function LampPowerCard() {
  const { status, sendCmd } = useConnection();
  const [paletteOpen, setPaletteOpen] = useState(false);
  const [brightness, setBrightness] = useState(70);
  const [lampOn, setLampOn] = useState(false);
  const canSync = !!status.switchState && !!status.lampState && status.switchState !== status.lampState;
  const [tick, setTick] = useState(0);

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

  useEffect(() => {
    setLampOn(status.lampState === 'ON');
  }, [status.lampState]);

  const patternText = lampOn && status.currentPattern
    ? `${patternLabel(status.currentPattern, status.patternName)}`
    : '';

  useEffect(() => {
    let raf: number;
    const loop = (t: number) => {
      setTick(t);
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  const energy = lampOn && status.currentPattern ? getPatternBrightness(status.currentPattern, tick % 12000) : 0;

  const handleBrightness = (value: number) => {
    const clamped = Math.min(100, Math.max(1, Math.round(value)));
    setBrightness(clamped);
    sendCmd(`bri ${clamped}`).catch((e) => console.warn(e));
  };

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.lamp">Lamp</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex flex-wrap items-center gap-3">
          <span className="chip-muted inline-flex items-center gap-2">
            Switch: {status.switchState ?? "--"}
            <Button size="sm" variant="ghost" onClick={() => sendCmd("sync")} disabled={!canSync} className="h-6 px-2 text-xs">
              <RefreshCw className="mr-1 h-3 w-3" />
              <span className="hidden sm:inline">Sync</span>
            </Button>
          </span>
          <span className="chip-muted">Touch: {status.touchState ?? "--"}</span>
        </div>

        <motion.button
          type="button"
          onClick={() => handleLampToggle(!lampOn)}
          className="relative flex w-full items-center justify-between overflow-hidden rounded-2xl border border-accent/50 px-6 py-6 text-left text-lg font-semibold shadow-[0_15px_40px_rgba(0,0,0,0.35)] transition-transform hover:scale-[1.01] focus:outline-none focus:ring-2 focus:ring-accent focus:ring-offset-2"
          animate={{
            scale: lampOn ? 1.01 : 1,
            boxShadow: lampOn
              ? "0 0 30px rgba(255,200,120,0.35), 0 15px 50px rgba(0,0,0,0.35)"
              : "0 10px 25px rgba(0,0,0,0.3)",
          }}
        >
          <motion.div
            className="absolute inset-0 opacity-80"
            initial={false}
            style={{
              background: lampOn
                ? "linear-gradient(135deg, rgba(255,214,134,0.4), rgba(255,140,90,0.3))"
                : "linear-gradient(120deg, rgba(40,50,70,0.7), rgba(20,24,36,0.8))",
              opacity: lampOn ? energy : 0.9,
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
              {lampOn ? <Trans k="btn.on">On</Trans> : <Trans k="btn.off">Off</Trans>}
            </span>
            {lampOn && status.currentPattern && (
              <button
                type="button"
                onClick={(e) => {e.stopPropagation();setPaletteOpen(true)}}
                className="rounded-full bg-amber-300/25 px-3 py-1 text-sm text-amber-50 shadow-inner underline decoration-amber-100/70 decoration-dotted"
              >
                {patternText ? patternText : `Mode ${status.currentPattern}`}
              </button>
            )}
          </div>
        </motion.button>
        <SliderRow
          label={<Trans k="label.brightness">Brightness</Trans>}
          description={<Trans k="desc.brightness">Set output level</Trans>}
          inputProps={{
            min: 1,
            max: 100,
            value: brightness,
          }}
          onInputChange={(val) => handleBrightness(val)}
        />
        <PatternPalette open={paletteOpen} setOpen={setPaletteOpen} />
      </CardContent>
    </Card>
  );
}
