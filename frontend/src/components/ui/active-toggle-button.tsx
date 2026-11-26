import type { LucideIcon } from 'lucide-react';
import type { ReactNode } from 'react';
import { cn } from '@/lib/utils';

type Props = {
  active: boolean;
  onStart: () => void;
  onStop: () => void;
  startLabel: ReactNode;
  stopLabel: ReactNode;
  startIcon: LucideIcon;
  stopIcon: LucideIcon;
  disabled?: boolean;
  className?: string;
};

/**
 * A single button that's always the prominent element of its row, swapping
 * label/icon/color between a call-to-action (inactive) and a stop action
 * (active) instead of showing both as separate buttons side by side.
 */
export function ActiveToggleButton({
  active,
  onStart,
  onStop,
  startLabel,
  stopLabel,
  startIcon: StartIcon,
  stopIcon: StopIcon,
  disabled,
  className,
}: Props) {
  const Icon = active ? StopIcon : StartIcon;
  return (
    <button
      type="button"
      disabled={disabled}
      onClick={active ? onStop : onStart}
      className={cn(
        'flex w-full items-center justify-center gap-2 rounded-2xl border px-5 py-4 text-base font-semibold shadow-lg transition focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent focus-visible:ring-offset-2 focus-visible:ring-offset-transparent disabled:cursor-not-allowed disabled:opacity-50',
        active
          ? 'border-red-500/60 bg-red-500/15 text-red-300 hover:bg-red-500/25'
          : 'border-accent/60 bg-accent/10 text-accent hover:bg-accent/20',
        className,
      )}
    >
      <Icon className="h-5 w-5" />
      {active ? stopLabel : startLabel}
    </button>
  );
}
