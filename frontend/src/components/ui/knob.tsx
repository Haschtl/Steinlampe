import { ReactNode, useEffect, useMemo, useRef, useState } from 'react';
import { KnobHeadless, useKnobKeyboardControls } from 'react-knob-headless';

type KnobProps = {
  label?: ReactNode;
  min?: number;
  max?: number;
  step?: number;
  value: number;
  onChange: (v: number) => void;
  disabled?: boolean;
};

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

/**
 * Quartz-styled knob using react-knob-headless for interaction/keyboard handling.
 */
export function Knob({ label, min = 0, max = 1, step = 0.01, value, onChange, disabled }: KnobProps) {
  const span = max - min || 1;
  const [internalVal, setInternalVal] = useState(value);
  const norm = clamp((internalVal - min) / span, 0, 1);
  const sweepDeg = 270;
  const startDeg = -225;
  const pointerDeg = startDeg + norm * sweepDeg;

  const frame = useRef<number | null>(null);
  const pending = useRef<number | null>(null);
  const lastEmit = useRef(0);
  const dragging = useRef(false);

  useEffect(
    () => () => {
      if (frame.current !== null) cancelAnimationFrame(frame.current);
      frame.current = null;
      pending.current = null;
    },
    [],
  );

  // Keep internal state aligned with external value when not dragging.
  useEffect(() => {
    if (!dragging.current) {
      setInternalVal(value);
    }
  }, [value]);

  const fmt = useMemo(() => {
    if (step >= 1) return (v: number) => v.toFixed(0);
    if (step >= 0.1) return (v: number) => v.toFixed(1);
    return (v: number) => v.toFixed(2);
  }, [step]);

  const emitThrottled = (val: number) => {
    const now = performance.now();
    if (now - lastEmit.current > 70) {
      lastEmit.current = now;
      onChange(val);
      pending.current = null;
      return;
    }
    pending.current = val;
    if (frame.current === null) {
      frame.current = requestAnimationFrame(() => {
        frame.current = null;
        if (pending.current !== null) {
          lastEmit.current = performance.now();
          onChange(pending.current);
          pending.current = null;
        }
      });
    }
  };

  const handleChange = (v: number) => {
    const snapped = step > 0 ? clamp(Math.round(v / step) * step, min, max) : clamp(v, min, max);
    setInternalVal(snapped);
    emitThrottled(snapped);
  };

  const kb = useKnobKeyboardControls({
    valueRaw: value,
    valueMin: min,
    valueMax: max,
    step: step > 0 ? step : span / 100,
    stepLarger: step > 0 ? step * 5 : span / 20,
    onValueRawChange: (val) => handleChange(val),
  });

  const ariaLabel =
    typeof label === 'string' ? label : typeof label === 'number' ? String(label) : 'Knob';

  return (
    <div className="flex flex-col items-center gap-2 text-center" style={{marginTop:0}}>
      {label && <span className="text-xs font-medium text-muted">{label}</span>}
      <div className={`relative ${disabled ? 'opacity-50' : ''}`} style={{ width: 88, height: 88 }}>
        {/* Glow */}
        <div
          className="pointer-events-none absolute -inset-3 rounded-full blur-2xl"
          style={{
            background:
              'radial-gradient(circle at 40% 35%, rgba(var(--accent-rgb,91,230,255),0.28), transparent 60%), ' +
              'radial-gradient(circle at 70% 65%, rgba(255,190,140,0.12), transparent 65%)',
          }}
        />
        {/* Progress */}
        <div
          className="pointer-events-none absolute inset-[6px] rounded-full"
          style={{
            background: `conic-gradient(from ${startDeg + 90}deg, rgba(var(--accent-rgb,91,230,255),0.95) ${
              norm * sweepDeg
            }deg, rgba(60,80,110,0.6) ${norm * sweepDeg}deg)`,
            WebkitMaskImage:
              'radial-gradient(circle at center, transparent 54%, black 58%), radial-gradient(circle at center, black 100%)',
            maskImage:
              'radial-gradient(circle at center, transparent 54%, black 58%), radial-gradient(circle at center, black 100%)',
            boxShadow: 'inset 0 0 14px rgba(0,0,0,0.7)',
          }}
        />
        {/* Pointer overlay */}
        <div
          className="pointer-events-none absolute inset-[12px] flex items-center justify-center"
          style={{ transform: `rotate(${pointerDeg}deg)` }}
        >
          <div className="h-1.5 w-8 rounded-full bg-[rgba(91,230,255,0.95)] shadow-[0_0_18px_rgba(91,230,255,0.75)]" />
        </div>

        <KnobHeadless
          aria-label={ariaLabel}
          valueRaw={internalVal}
          valueMin={min}
          valueMax={max}
          valueRawDisplayFn={fmt}
          valueRawRoundFn={(v) => v}
          onValueRawChange={handleChange}
          dragSensitivity={0.006}
          includeIntoTabOrder
          onPointerDown={() => {
            if (disabled) return;
            dragging.current = true;
          }}
          onPointerUp={() => {
            dragging.current = false;
          }}
          onPointerLeave={() => {
            dragging.current = false;
          }}
          style={{
            width: '100%',
            height: '100%',
            touchAction: 'none',
            borderRadius: '9999px',
            background:
              'radial-gradient(circle at 30% 30%, rgba(255,255,255,0.08), transparent 55%), ' +
              'radial-gradient(circle at 65% 70%, rgba(var(--accent-rgb,91,230,255),0.18), transparent 60%), ' +
              'linear-gradient(140deg, rgba(10,15,28,0.96), rgba(6,10,18,0.86))',
            boxShadow:
              'inset 0 6px 12px rgba(0,0,0,0.35), inset 0 -4px 8px rgba(0,0,0,0.35), 0 0 18px rgba(var(--accent-rgb,91,230,255),0.24)',
            border: '1px solid rgba(255,255,255,0.06)',
            cursor: disabled ? 'not-allowed' : 'pointer',
            pointerEvents: disabled ? 'none' : 'auto',
            color: 'inherit',
            position: 'relative',
            zIndex: 1,
          }}
          {...kb}
        />
      </div>
      <span className="text-xs text-muted">{fmt(value)}</span>
    </div>
  );
}
