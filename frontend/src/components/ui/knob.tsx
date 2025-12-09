import { ReactNode, useEffect, useRef, useState } from 'react';

type KnobProps = {
  label?: ReactNode;
  min?: number;
  max?: number;
  step?: number;
  value: number;
  onChange: (v: number) => void;
  disabled?: boolean;
};

export function Knob({ label, min = 0, max = 1, step = 0.01, value, onChange, disabled }: KnobProps) {
  const knobRef = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState(false);
  const span = max - min || 1;
  const norm = Math.min(1, Math.max(0, (value - min) / span));
  const angle = -140 + norm * 280; // -140..140 deg

  const snap = (v: number) => {
    if (step <= 0) return v;
    return Math.round(v / step) * step;
  };

  const updateFromPointer = (clientX: number, clientY: number) => {
    const rect = knobRef.current?.getBoundingClientRect();
    if (!rect) return;
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const dx = clientX - cx;
    const dy = cy - clientY;
    let ang = (Math.atan2(dy, dx) * 180) / Math.PI;
    // constrain to sweep -140..140
    if (ang > 140) ang = 140;
    if (ang < -140) ang = -140;
    const n = (ang + 140) / 280;
    const val = snap(min + n * span);
    onChange(val);
  };

  useEffect(() => {
    if (!dragging) return;
    const onMove = (e: PointerEvent) => {
      e.preventDefault();
      updateFromPointer(e.clientX, e.clientY);
    };
    const onUp = () => setDragging(false);
    window.addEventListener('pointermove', onMove, { passive: false });
    window.addEventListener('pointerup', onUp);
    return () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
    };
  }, [dragging]);

  return (
    <div className="flex flex-col items-center gap-2 text-center">
      {label && <span className="text-xs font-medium text-muted">{label}</span>}
      <div
        ref={knobRef}
        className={`relative h-16 w-16 rounded-full bg-gradient-to-br from-white/10 via-white/4 to-black/20 shadow-inner transition ${disabled ? 'opacity-60' : 'cursor-pointer'}`}
        onPointerDown={(e) => {
          if (disabled) return;
          (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
          setDragging(true);
          updateFromPointer(e.clientX, e.clientY);
        }}
      >
        <div className="absolute inset-2 rounded-full bg-gradient-to-tr from-black/30 via-accent/10 to-white/10 shadow" />
        <div
          className="absolute left-1/2 top-1/2 h-2 w-6 -translate-x-1/2 -translate-y-1/2 origin-left rounded-full bg-accent shadow-[0_0_8px_rgba(var(--accent-rgb),0.5)]"
          style={{ transform: `translate(-50%,-50%) rotate(${angle}deg)` }}
        />
        <div className="absolute inset-0 rounded-full border border-white/10" />
      </div>
      <span className="text-xs text-muted">{value.toFixed(2)}</span>
    </div>
  );
}
