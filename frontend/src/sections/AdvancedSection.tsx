import { PresenceCard } from '@/cards/PresenceCard';
import { TouchCard } from '@/cards/TouchCard';
import { LightCard } from '@/cards/LightCard';
import { MusicCard } from '@/cards/MusicCard';
import { LedConfigCard } from "@/cards/LedConfigCard";

export function AdvancedSection() {
  return (
    <div className="space-y-4">
       <LedConfigCard />
      <PresenceCard />     
      <TouchCard />
      <LightCard />
      <MusicCard />
    </div>
  );
}
