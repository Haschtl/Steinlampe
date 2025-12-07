import { PresenceCard } from '@/cards/PresenceCard';
import { TouchCard } from '@/cards/TouchCard';
import { LightCard } from '@/cards/LightCard';
import { MusicCard } from '@/cards/MusicCard';

export function AdvancedSection() {
  return (
    <div className="space-y-4">
      <PresenceCard />
      <TouchCard />
      <LightCard />
      <MusicCard />
    </div>
  );
}
