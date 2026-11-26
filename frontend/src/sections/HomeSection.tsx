import { useState } from 'react';
import { LampPowerCard } from '@/cards/LampPowerCard';
import { ProfileSlotsCard } from '@/cards/ProfileSlotsCard';
import { RampCard } from '@/cards/RampCard';

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');
  return (
    <div className="space-y-4">
      <ProfileSlotsCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <LampPowerCard />
      <RampCard />
    </div>
  );
}
