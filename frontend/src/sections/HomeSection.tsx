import { useState } from 'react';
import { LampControlsCard } from '@/cards/LampControlsCard';
import { RampCard } from '@/cards/RampCard';

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <LampControlsCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <RampCard />
    </div>
  );
}
