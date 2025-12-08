import { useEffect, useMemo, useRef, useState } from 'react';
import { Palette } from 'lucide-react';
import { motion } from 'framer-motion';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { useConnection } from '@/context/connection';
import { patternGroups, patternLabel, patternLabels } from '@/data/patterns';
import { getPatternBrightness } from '@/lib/patternSim';

const UPDATE_INTERVAL = 50;
export function PatternPalette({ open, setOpen }: { open: boolean; setOpen: (v: boolean) => void }) {
  const { sendCmd, status } = useConnection();
  const [tick, setTick] = useState(0);
  const energyCache = useRef<Record<string, number>>({});

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

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabel(i + 1),
    }));
  }, [status.patternCount]);

  const patternPreview = (idx: number) => {
    const baseColors = [
      ['#5be6ff', '#0f172a'],
      ['#f59e0b', '#7c2d12'],
      ['#a855f7', '#0f172a'],
      ['#34d399', '#0f172a'],
      ['#f97316', '#7c1d1d'],
      ['#22d3ee', '#0ea5e9'],
      ['#ef4444', '#111827'],
      ['#5be6ff', '#0b1221'],
    ];
    const palette = baseColors[(idx - 1) % baseColors.length];
    return { base: palette[0], base2: palette[1], accent: palette[0] };
  };

  return (
    <Dialog open={open} onOpenChange={setOpen}>
      {/* <DialogTrigger asChild>
        <Button variant="primary" size="sm" className="ml-auto flex items-center gap-2">
          <Palette className="h-4 w-4" />
          <span className="hidden sm:inline">Pattern Palette</span>
        </Button>
      </DialogTrigger> */}
      <DialogContent className="max-w-4xl">
        <DialogHeader>
          <DialogTitle>Pattern Palette</DialogTitle>
        </DialogHeader>
        <div className="max-h-[70vh] space-y-4 overflow-y-auto">
          {patternGroups.map((group) => {
            const items = patternOptions.filter((p) => group.indices.includes(p.idx));
            if (items.length === 0) return null;
            return (
              <div key={group.title} className="space-y-2">
                <div className="text-xs uppercase tracking-wide text-muted">{group.title}</div>
                <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
                  {items.map((p) => {
                    const bg = patternPreview(p.idx);
                    const speed = status.patternSpeed ?? 1;
                    const fade = status.patternFade && status.patternFade > 0 ? status.patternFade : 0;
                    const raw = getPatternBrightness(p.idx, tick * speed);
                    const key = `p${p.idx}`;
                    const prev = energyCache.current[key] ?? raw;
                    const alpha = fade ? Math.min(1, UPDATE_INTERVAL / (400 * fade)) : 1;
                    const energy = prev + (raw - prev) * alpha;
                    energyCache.current[key] = energy;
                    return (
                      <button
                        key={p.idx}
                        type="button"
                        onClick={() => {
                          sendCmd(`mode ${p.idx}`);
                          setOpen(false);
                        }}
                    className="relative overflow-hidden rounded-2xl border border-border/70 p-4 text-left shadow-lg transition-transform hover:-translate-y-1 focus:outline-none focus:ring-2 focus:ring-accent"
                  >
                    <motion.div
                      className="absolute inset-0"
                      style={{ background: `linear-gradient(135deg, ${bg.base}, ${bg.base2})`, opacity: energy }}
                        />
                        <motion.div
                          className="absolute inset-0"
                          style={{ background: `radial-gradient(circle at 30% 30%, ${bg.accent}44, transparent 50%)`, opacity: energy }}
                        />
                        <div className="absolute inset-0 bg-black/20 backdrop-blur-[2px]" />
                        <div className="relative flex items-center justify-between">
                          <div className="h-10 w-10 rounded-full bg-white/10 shadow-inner" />
                        </div>
                        <div className="absolute inset-x-0 bottom-0 bg-black/40 px-3 py-2 backdrop-blur-sm">
                          <div className="text-xs uppercase tracking-wide text-muted">Mode {p.idx}</div>
                          <div className="text-base font-semibold text-text drop-shadow-sm">{p.label}</div>
                        </div>
                      </button>
                    );
                  })}
                </div>
              </div>
            );
          })}
        </div>
      </DialogContent>
    </Dialog>
  );
}
