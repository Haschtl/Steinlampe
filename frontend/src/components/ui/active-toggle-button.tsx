import { Loader2, type LucideIcon } from 'lucide-react';
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
  // True while a write triggered by this button is in flight (write confirmed
  // sent is as precise as this can get without the protocol carrying request/
  // response correlation - see memory: a full status round-trip to confirm
  // the *result* can itself take seconds under the current protocol). Shows a
  // spinner in place of the icon and disables the button, so a slow
  // round-trip at least gets immediate feedback instead of looking like
  // nothing happened - and can't be double-fired by an impatient second tap.
  pending?: boolean;
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
  pending,
  className,
}: Props) {
  const Icon = active ? StopIcon : StartIcon;
  return (
    <button
      type="button"
      disabled={disabled || pending}
      onClick={active ? onStop : onStart}
      className={cn(
        'flex w-full items-center justify-center gap-2 rounded-2xl border px-5 py-4 text-base font-semibold shadow-lg transition focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent focus-visible:ring-offset-2 focus-visible:ring-offset-transparent disabled:cursor-not-allowed disabled:opacity-50',
        active
          ? 'border-red-500/60 bg-red-500/15 text-red-300 hover:bg-red-500/25'
          : 'border-accent/60 bg-accent/10 text-accent hover:bg-accent/20',
        className,
      )}
    >
      {pending ? <Loader2 className="h-5 w-5 animate-spin" /> : <Icon className="h-5 w-5" />}
      {active ? stopLabel : startLabel}
    </button>
  );
}
