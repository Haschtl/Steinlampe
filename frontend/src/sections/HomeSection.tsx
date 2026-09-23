import { LampPowerCard } from '@/cards/LampPowerCard';
import { ModesCard } from '@/cards/ModesCard';
import { ProfileSlotsCard } from '@/cards/ProfileSlotsCard';
import { RampCard } from '@/cards/RampCard';

type HomeSectionProps = {
  profileSlot: string;
  setProfileSlot: (value: string) => void;
};

export function HomeSection({ profileSlot, setProfileSlot }: HomeSectionProps) {
  return (
    <div className="space-y-4">
      <ProfileSlotsCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <LampPowerCard />
      <ModesCard />
      <RampCard />
    </div>
  );
}
