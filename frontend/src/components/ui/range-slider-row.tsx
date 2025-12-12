import { ReactNode, useEffect, useRef, useState } from 'react';
import { Input } from './input';
import { Label } from './label';

type RangeSliderRowProps = {
  label?: ReactNode;
  description?: ReactNode;
  min?: number;
  max?: number;
  step?: number;
  values: [number, number];
  onChange: (vals: [number, number]) => void;
};

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

export function RangeSliderRow({
  label,
  description,
  min = 0,
  max = 1,
  step = 0.01,
  values,
  onChange,
}: RangeSliderRowProps) {
  const [[low, high], setRange] = useState<[number, number]>(values);
  const span = max - min === 0 ? 1 : max - min;
  const pct = (v: number) => `${((v - min) / span) * 100}%`;

  const trackRef = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState<'low' | 'high' | null>(null);

  const applyValue = (val: number, which: 'low' | 'high') => {
    let lo = low;
    let hi = high;
    if (which === 'low') {
      lo = clamp(val, min, hi);
    } else {
      hi = clamp(val, lo, max);
    }
    setRange([lo, hi]);
    onChange([lo, hi]);
  };

  const handlePointer = (clientX: number, which?: 'low' | 'high') => {
    const rect = trackRef.current?.getBoundingClientRect();
    if (!rect) return;
    const ratio = clamp((clientX - rect.left) / rect.width, 0, 1);
    const raw = min + ratio * span;
    const snapped = step > 0 ? Math.round(raw / step) * step : raw;
    const target =
      which ??
      (Math.abs(raw - low) <= Math.abs(raw - high) ? 'low' : 'high');
    applyValue(snapped, target);
  };

  useEffect(() => {
    if (!dragging) return;
    const onMove = (e: PointerEvent) => handlePointer(e.clientX, dragging);
    const onUp = () => setDragging(null);
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    window.addEventListener('pointercancel', onUp);
    return () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      window.removeEventListener('pointercancel', onUp);
    };
  }, [dragging, low, high]);

  const inputProps = {
    type: 'number' as const,
    min,
    max,
    step,
  };

  // Keep local state in sync with incoming values unless the user is dragging.
  useEffect(() => {
    if (!dragging) setRange(values);
  }, [values, dragging]);

  return (
    <div className="space-y-1">
      {(label || description) && (
        <div className="flex items-center justify-between gap-2">
          <div>
            {label && (
              <p className="text-sm font-semibold text-text">
                <Label className="m-0">{label}</Label>
              </p>
            )}
            {description && <p className="text-xs text-muted">{description}</p>}
          </div>
          <div className="flex items-center gap-2">
            <Input
              {...inputProps}
              className="w-24"
              value={Number(low.toFixed(2))}
              onChange={(e) => applyValue(Number(e.target.value), 'low')}
            />
            <span className="text-xs text-muted">–</span>
            <Input
              {...inputProps}
              className="w-24"
              value={Number(high.toFixed(2))}
              onChange={(e) => applyValue(Number(e.target.value), 'high')}
            />
          </div>
        </div>
      )}
      <div
        ref={trackRef}
        className="relative h-8 select-none touch-none"
        onPointerDown={(e) => {
          e.preventDefault();
          (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
          handlePointer(e.clientX);
        }}
      >
        <div className="absolute inset-x-0 top-1/2 h-2 -translate-y-1/2 rounded-full bg-border/40" />
        <div
          className="absolute top-1/2 h-2 -translate-y-1/2 rounded-full bg-accent/50"
          style={{
            left: pct(low),
            width: `calc(${pct(high)} - ${pct(low)})`,
          }}
        />
        {[['low', low] as const, ['high', high] as const].map(([key, val]) => (
          <div
            key={key}
            className="absolute top-1/2 h-4 w-4 -translate-y-1/2 -translate-x-1/2 rounded-full border border-accent/50 bg-accent focus:outline-none transition-colors hover:border-accent hover:bg-accent/20 touch-none"
            style={{ left: pct(val) }}
            onPointerDown={(e) => {
              e.preventDefault();
              e.stopPropagation();
              (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
              setDragging(key);
            }}
          />
        ))}
      </div>
    </div>
  );
}
