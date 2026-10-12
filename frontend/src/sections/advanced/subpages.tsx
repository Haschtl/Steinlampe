import { PotiCard } from '@/cards/PotiCard';
import { PushButtonCard } from '@/cards/PushButtonCard';
import { PresenceBleCard } from '@/cards/PresenceBleCard';
import { MusicCard } from '@/cards/MusicCard';
import { RadarCard } from '@/cards/RadarCard';
import { LightCard } from '@/cards/LightCard';
import { TouchCard } from '@/cards/TouchCard';
import { useConnection } from '@/context/connection';
import { HardwareSubpage } from './HardwareSubpage';

export function PotiPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasPoti !== false}>
      <PotiCard />
    </HardwareSubpage>
  );
}

export function TasterPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasPush !== false}>
      <PushButtonCard />
    </HardwareSubpage>
  );
}

export function AnwesenheitPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasPresenceBle !== false}>
      <PresenceBleCard />
    </HardwareSubpage>
  );
}

export function MusikPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasMusic !== false}>
      <MusicCard />
    </HardwareSubpage>
  );
}

export function RadarPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasRadar !== false}>
      <RadarCard />
    </HardwareSubpage>
  );
}

export function LichtPage() {
  const { status } = useConnection();
  return (
    <HardwareSubpage available={status.hasLight !== false}>
      <LightCard />
    </HardwareSubpage>
  );
}

export function TouchPage() {
  const { status } = useConnection();
  const available = status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A';
  return (
    <HardwareSubpage available={available}>
      <TouchCard />
    </HardwareSubpage>
  );
}
