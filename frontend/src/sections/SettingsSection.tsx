import { useEffect, useState } from 'react';
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
    </div>
  );
}
