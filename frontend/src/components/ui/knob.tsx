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
      <div className={`relative ${disabled ? 'opacity-50' : ''}`} style={{ width: 92, height: 92 }}>
        {/* Outer shimmer */}
        <div
          className="pointer-events-none absolute -inset-4 rounded-full blur-2xl"
          style={{
            background:
              'radial-gradient(circle at 35% 30%, rgba(var(--accent-rgb,91,230,255),0.35), transparent 55%), ' +
              'radial-gradient(circle at 70% 70%, rgba(255,200,160,0.18), transparent 60%)',
            opacity: 0.9,
          }}
        />
        {/* Glass ring */}
        <div
          className="pointer-events-none absolute inset-[4px] rounded-full"
          style={{
            background:
              'linear-gradient(145deg, rgba(12,18,32,0.95), rgba(7,11,20,0.9)), ' +
              'radial-gradient(circle at 30% 30%, rgba(255,255,255,0.12), transparent 60%)',
            boxShadow:
              'inset 0 10px 18px rgba(0,0,0,0.45), inset 0 -6px 10px rgba(0,0,0,0.5), 0 0 18px rgba(var(--accent-rgb,91,230,255),0.28)',
            border: '1px solid rgba(255,255,255,0.08)',
          }}
        />
        {/* Progress arc */}
        <div
          className="pointer-events-none absolute inset-[10px] rounded-full"
          style={{
            background: `conic-gradient(from ${startDeg + 90}deg, rgb(var(--accent-rgb,91,230,255)) ${
              norm * sweepDeg
            }deg, rgba(16,24,38,0.85) ${norm * sweepDeg}deg)`,
            WebkitMaskImage:
              'radial-gradient(circle at center, transparent 50%, black 56%), radial-gradient(circle at center, black 100%)',
            maskImage:
              'radial-gradient(circle at center, transparent 50%, black 56%), radial-gradient(circle at center, black 100%)',
            boxShadow: 'inset 0 0 16px rgba(0,0,0,0.8), 0 0 14px rgba(var(--accent-rgb,91,230,255),0.22)',
          }}
        />
        {/* Inner disc */}
        <div
          className="pointer-events-none absolute inset-[18px] rounded-full"
          style={{
            background:
              'radial-gradient(circle at 32% 32%, rgba(255,255,255,0.18), transparent 55%), ' +
              'linear-gradient(160deg, #0f172a, #0a1322)',
            boxShadow: 'inset 0 3px 6px rgba(255,255,255,0.08), inset 0 -4px 10px rgba(0,0,0,0.55)',
          }}
        />
        {/* Pointer overlay */}
        <div
          className="pointer-events-none absolute inset-[16px] flex items-center justify-center"
          style={{ transform: `rotate(${pointerDeg}deg)` }}
        >
          <div className="h-1.5 w-9 rounded-full bg-[rgba(91,230,255,0.98)] shadow-[0_0_20px_rgba(91,230,255,0.75)]" />
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
              'radial-gradient(circle at 20% 20%, rgba(255,255,255,0.08), transparent 90%), ' +
              'radial-gradient(circle at 70% 70%, rgba(var(--accent-rgb,91,230,255),0.14), transparent 92%), ' +
              'linear-gradient(120deg, rgba(18,27,46,0.55), rgba(10,16,30,0.79))',
            boxShadow:
              'inset 0 10px 16px rgba(0,0,0,0.4), inset 0 -6px 10px rgba(0,0,0,0.45), 0 0 14px rgba(var(--accent-rgb,91,230,255),0.2)',
            border: '1px solid rgba(255,255,255,0.05)',
            cursor: disabled ? 'not-allowed' : 'pointer',
            pointerEvents: disabled ? 'none' : 'auto',
            color: 'inherit',
            position: 'relative',
            zIndex: 1,
          }}
          {...kb}
        />
      </div>
      <input
        type="number"
        className="input w-16 text-center text-xs"
        step={step || 0.01}
        min={min}
        max={max}
        value={fmt(internalVal)}
        onChange={(e) => {
          if (disabled) return;
          const v = parseFloat(e.target.value);
          if (!Number.isFinite(v)) return;
          handleChange(v);
        }}
        disabled={disabled}
      />
    </div>
  );
}
