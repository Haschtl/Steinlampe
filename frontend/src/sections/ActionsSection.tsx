import { MorseCard } from '@/cards/MorseCard';
import { NotifyCard } from '@/cards/NotifyCard';
import { WakeSleepCard } from '@/cards/WakeSleepCard';

export function ActionsSection() {
  return (
    <div className="space-y-4">
      <WakeSleepCard />
      <MorseCard />
      <NotifyCard />
    </div>
  );
}
