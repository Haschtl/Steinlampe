import { useState } from 'react';
import { LampPowerCard } from '@/cards/LampPowerCard';
import { ModesCard } from '@/cards/ModesCard';
import { RampCard } from '@/cards/RampCard';

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <LampPowerCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <ModesCard />
      <RampCard />
    </div>
  );
}
