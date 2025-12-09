import { ReactNode, useEffect, useMemo, useRef } from 'react';
import KnobDefault, { Knob as KnobNamed } from 'react-rotary-knob';

type KnobProps = {
  label?: ReactNode;
  min?: number;
  max?: number;
  step?: number;
  value: number;
  onChange: (v: number) => void;
  disabled?: boolean;
};

/**
 * Rotary knob control built on react-rotary-knob.
 * Uses a themed skin so it matches the Quarzlampe visuals.
 */
export function Knob({ label, min = 0, max = 1, step = 0.01, value, onChange, disabled }: KnobProps) {
  // Support both default and named export across library versions.
  const KnobBase: any = (KnobNamed as any) || (KnobDefault as any);
  const lastEmit = useRef(0);
  const queued = useRef<number | null>(null);
  const tHandle = useRef<number | null>(null);

  useEffect(
    () => () => {
      if (tHandle.current !== null) {
        clearTimeout(tHandle.current);
        tHandle.current = null;
      }
    },
    [],
  );
  const fmt = useMemo(() => {
    if (step >= 1) return (v: number) => v.toFixed(0);
    if (step >= 0.1) return (v: number) => v.toFixed(1);
    return (v: number) => v.toFixed(2);
  }, [step]);

  const norm = useMemo(() => {
    const span = max - min || 1;
    return Math.max(0, Math.min(1, (value - min) / span));
  }, [value, min, max]);
  const sweepDeg = 270; // leave a gap for a quartz-like notch
  const startDeg = -225;
  const pointerDeg = startDeg + norm * sweepDeg;

  return (
    <div className="flex flex-col items-center gap-2 text-center" style={{marginTop:0}}>
      {label && <span className="text-xs font-medium text-muted">{label}</span>}
      <div
        className={`relative ${disabled ? "opacity-50" : ""}`}
        style={{ width: 88, height: 88 }}
      >
        {/* Outer glow */}
        <div
          className="pointer-events-none absolute -inset-2 rounded-full blur-2xl"
          style={{
            background:
              "radial-gradient(circle at 40% 35%, rgba(var(--accent-rgb,91,230,255),0.22), transparent 60%), " +
              "radial-gradient(circle at 70% 65%, rgba(255,190,140,0.08), transparent 65%)",
          }}
        />
        {/* Main body */}
        <div
          className="absolute inset-0 rounded-full"
          style={{
            background:
              "radial-gradient(circle at 30% 30%, rgba(255,255,255,0.08), transparent 55%), " +
              "radial-gradient(circle at 65% 70%, rgba(var(--accent-rgb,91,230,255),0.18), transparent 60%), " +
              "linear-gradient(140deg, rgba(10,15,28,0.96), rgba(6,10,18,0.86))",
            boxShadow:
              "inset 0 6px 12px rgba(0,0,0,0.35), inset 0 -4px 8px rgba(0,0,0,0.35), 0 0 18px rgba(var(--accent-rgb,91,230,255),0.24)",
            border: "1px solid rgba(255,255,255,0.06)",
          }}
        />
        {/* Track + progress */}
        <div
          className="absolute inset-[8px] rounded-full"
          style={{
            background: `conic-gradient(from ${
              startDeg + 90
            }deg, rgba(var(--accent-rgb,91,230,255),0.9) ${
              norm * sweepDeg
            }deg, rgba(255,255,255,0.08) ${norm * sweepDeg}deg)`,
            WebkitMaskImage:
              "radial-gradient(circle at center, transparent 62%, black 64%), radial-gradient(circle at center, black 100%)",
            maskImage:
              "radial-gradient(circle at center, transparent 62%, black 64%), radial-gradient(circle at center, black 100%)",
            boxShadow: "inset 0 0 10px rgba(0,0,0,0.5)",
          }}
        />
        {/* Center cap */}
        <div
          className="absolute inset-[16px] rounded-full"
          style={{
            background:
              "radial-gradient(circle at 35% 30%, rgba(255,255,255,0.12), transparent 55%), linear-gradient(145deg, #0c1221, #0a1525)",
            boxShadow:
              "inset 0 2px 4px rgba(255,255,255,0.05), inset 0 -2px 6px rgba(0,0,0,0.42)",
          }}
        />
        {/* Pointer */}
        <div
          className="absolute inset-[14px] flex items-center justify-center"
          style={{ transform: `rotate(${pointerDeg}deg)` }}
        >
          <div className="h-1.5 w-8 rounded-full bg-[rgba(91,230,255,0.9)] shadow-[0_0_14px_rgba(91,230,255,0.65)]" />
        </div>
        {/* Invisible interactive layer handled by the library */}
        <KnobBase
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(v: number) => {
            if (disabled) return;
            const clamped = Math.min(max, Math.max(min, v));
            const now = performance.now();
            if (now - lastEmit.current > 70) {
              lastEmit.current = now;
              onChange(clamped);
            } else {
              queued.current = clamped;
              if (tHandle.current === null) {
                tHandle.current = window.setTimeout(() => {
                  if (queued.current !== null) {
                    onChange(queued.current);
                    lastEmit.current = performance.now();
                    queued.current = null;
                  }
                  if (tHandle.current !== null) {
                    clearTimeout(tHandle.current);
                    tHandle.current = null;
                  }
                }, 90);
              }
            }
          }}
          unlockDistance={0}
          preciseMode={false}
          className="w-full h-full"
          style={{ width: "100%", height: "100%", opacity: 0 }}
        />
      </div>
      <span className="text-xs text-muted">{fmt(value)}</span>
    </div>
  );
}
