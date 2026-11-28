import { MorseCard } from '@/cards/MorseCard';
import { NotifyCard } from '@/cards/NotifyCard';
import { WakeSleepCard } from '@/cards/WakeSleepCard';
import { useStatusGroupFetch } from '@/hooks/useStatusGroupFetch';

export function ActionsSection() {
  // wake_active/sleep_active (WakeSleepCard) and notify_active (Morse/
  // NotifyCard) moved off the general status payload into TIMERS| on
  // 2026-11-28, see memory: status protocol redesign.
  useStatusGroupFetch('timers');
  return (
    <div className="space-y-4">
      <WakeSleepCard />
      <MorseCard />
      <NotifyCard />
    </div>
  );
}
