import { useState } from 'react';
import { LampCard } from '@/cards/LampCard';

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <LampCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
    </div>
  );
}
