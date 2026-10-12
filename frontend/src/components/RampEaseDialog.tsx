import { useMemo } from 'react';
import { Check } from 'lucide-react';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { buildRampPaths, EASE_OPTIONS, EaseType } from '@/lib/rampEase';

const TILE_SIZE = { w: 160, h: 96, pad: 14 };

function EaseTile({
  ease,
  pow,
  reverse,
  selected,
  onClick,
}: {
  ease: EaseType;
  pow: number;
  reverse?: boolean;
  selected: boolean;
  onClick: () => void;
}) {
  const { path, area, w, h, pad, usableW, usableH } = useMemo(
    () => buildRampPaths(400, ease, pow, reverse, TILE_SIZE, 60),
    [ease, pow, reverse],
  );
  const accent = reverse ? '#f97316' : '#5be6ff';
  const gradientId = `rampTileGrad-${ease}-${reverse ? 'off' : 'on'}`;

  return (
    <button
      type="button"
      onClick={onClick}
      className={`relative overflow-hidden rounded-xl border p-2 text-left transition-transform hover:-translate-y-0.5 focus:outline-none focus:ring-2 focus:ring-accent ${
        selected ? 'border-accent bg-accent/10' : 'border-border/70 bg-panel/60'
      }`}
    >
      <svg viewBox={`0 0 ${w} ${h}`} className="h-20 w-full">
        <defs>
          <linearGradient id={gradientId} x1="0%" x2="100%" y1="0%" y2="0%">
            <stop offset="0%" stopColor={accent} stopOpacity="0.35" />
            <stop offset="100%" stopColor={accent} stopOpacity="0.85" />
          </linearGradient>
        </defs>
        <rect x={pad} y={pad} width={usableW} height={usableH} rx="6" fill="rgba(255,255,255,0.03)" />
        <path d={area} fill={`url(#${gradientId})`} opacity="0.25" />
        <path d={path} fill="none" stroke={`url(#${gradientId})`} strokeWidth="3.5" strokeLinecap="round" />
        <line x1={pad} y1={h - pad} x2={w - pad} y2={h - pad} stroke="rgba(255,255,255,0.12)" strokeWidth="1" />
      </svg>
      <div className="mt-1 flex items-center justify-between gap-1">
        <span className="truncate text-xs font-medium text-text">{ease}</span>
        {selected && <Check className="h-3.5 w-3.5 shrink-0 text-accent" />}
      </div>
    </button>
  );
}

export function RampEaseDialog({
  open,
  setOpen,
  value,
  pow,
  reverse,
  onSelect,
  title,
}: {
  open: boolean;
  setOpen: (v: boolean) => void;
  value: string;
  pow: number;
  reverse?: boolean;
  onSelect: (ease: EaseType) => void;
  title: string;
}) {
  return (
    <Dialog open={open} onOpenChange={setOpen}>
      <DialogContent className="max-w-3xl">
        <DialogHeader>
          <DialogTitle>{title}</DialogTitle>
        </DialogHeader>
        <div className="max-h-[70vh] overflow-y-auto">
          <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 md:grid-cols-4">
            {EASE_OPTIONS.map((ease) => (
              <EaseTile
                key={ease}
                ease={ease}
                pow={pow}
                reverse={reverse}
                selected={ease === value}
                onClick={() => {
                  onSelect(ease);
                  setOpen(false);
                }}
              />
            ))}
          </div>
        </div>
      </DialogContent>
    </Dialog>
  );
}
