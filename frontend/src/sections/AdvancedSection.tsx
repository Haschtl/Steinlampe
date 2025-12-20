import { PresenceCard } from '@/cards/PresenceCard';
import { TouchCard } from '@/cards/TouchCard';
import { LightCard } from '@/cards/LightCard';
import { MusicCard } from '@/cards/MusicCard';
import { LedConfigCard } from '@/cards/LedConfigCard';
import { PotiCard } from '@/cards/PotiCard';
import { PushButtonCard } from '@/cards/PushButtonCard';
import { useConnection } from '@/context/connection';

export function AdvancedSection() {
  const { status } = useConnection();
  const showPresence = status.hasPresence !== false;
  const showLight = status.hasLight !== false;
  const showMusic = status.hasMusic !== false;
  const showPoti = status.hasPoti !== false;
  const showPush = status.hasPush !== false;
  const showTouch = status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A';

  return (
    <div className="space-y-4">
      <LedConfigCard />
      {showPoti && <PotiCard />}
      {showPush && <PushButtonCard />}
      {showPresence && <PresenceCard />}
      {showTouch && <TouchCard />}
      {showLight && <LightCard />}
      {showMusic && <MusicCard />}
    </div>
  );
}
