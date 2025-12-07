import { useEffect, useState } from 'react';
import { LightCard } from '@/cards/LightCard';
import { MusicCard } from '@/cards/MusicCard';
import { PresenceCard } from '@/cards/PresenceCard';
import { TouchCard } from '@/cards/TouchCard';
import { SettingsCard } from '@/cards/SettingsCard';
import { UISettingsCard } from '@/cards/UISettingsCard';
import { QuickCustomCard } from '@/cards/QuickCustomCard';
import { ProfilesCard } from '@/cards/ProfilesCard';

export function SettingsSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <UISettingsCard />
      <SettingsCard />
      <QuickCustomCard />
      <ProfilesCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <PresenceCard />
      <TouchCard />
      <LightCard />
      <MusicCard />
    </div>
  );
}
