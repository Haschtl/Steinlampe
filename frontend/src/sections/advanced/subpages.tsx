import { PotiCard } from '@/cards/PotiCard';
import { PushButtonCard } from '@/cards/PushButtonCard';
import { ButtonsOverviewCard } from '@/cards/ButtonsOverviewCard';
import { PresenceBleCard } from '@/cards/PresenceBleCard';
import { MusicCard } from '@/cards/MusicCard';
import { RadarCard } from '@/cards/RadarCard';
import { LightCard } from '@/cards/LightCard';
import { TouchCard } from '@/cards/TouchCard';
import { LedConfigCard } from '@/cards/LedConfigCard';
import { useConnection } from '@/context/connection';
import { useStatusGroupFetch } from '@/hooks/useStatusGroupFetch';
import { HardwareSubpage } from './HardwareSubpage';

export function PotiPage() {
  const { status } = useConnection();
  useStatusGroupFetch('poti');
  useStatusGroupFetch('misc'); // PotiCard also reads gestCsv (poti-targeted gesture rows)
  return (
    <HardwareSubpage available={status.hasPoti !== false}>
      <PotiCard />
    </HardwareSubpage>
  );
}

export function TasterPage() {
  const { status } = useConnection();
  useStatusGroupFetch('push');
  const hasAnyButtons = (status.buttons?.length ?? 0) > 0;
  const hasMomentary = status.buttons?.some((b) => b.type === 'momentary') ?? false;
  return (
    <HardwareSubpage available={status.hasPush !== false || hasAnyButtons}>
      <ButtonsOverviewCard />
      {(status.hasPush !== false || hasMomentary) && <PushButtonCard />}
    </HardwareSubpage>
  );
}

export function AnwesenheitPage() {
  const { status } = useConnection();
  useStatusGroupFetch('presence');
  return (
    <HardwareSubpage available={status.hasPresenceBle !== false}>
      <PresenceBleCard />
    </HardwareSubpage>
  );
}

export function MusikPage() {
  const { status } = useConnection();
  useStatusGroupFetch('music');
  return (
    <HardwareSubpage available={status.hasMusic !== false}>
      <MusicCard />
    </HardwareSubpage>
  );
}

export function RadarPage() {
  const { status } = useConnection();
  useStatusGroupFetch('radar');
  return (
    <HardwareSubpage available={status.hasRadar !== false}>
      <RadarCard />
    </HardwareSubpage>
  );
}

export function LichtPage() {
  const { status } = useConnection();
  useStatusGroupFetch('light');
  return (
    <HardwareSubpage available={status.hasLight !== false}>
      <LightCard />
    </HardwareSubpage>
  );
}

export function TouchPage() {
  const { status } = useConnection();
  useStatusGroupFetch('misc'); // touch_dim_step lives in MISC| now, see useStatusGroupFetch's own doc comment
  const available = status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A';
  return (
    <HardwareSubpage available={available}>
      <TouchCard />
    </HardwareSubpage>
  );
}

export function LedPage() {
  useStatusGroupFetch('io');
  return (
    <HardwareSubpage available>
      <LedConfigCard />
    </HardwareSubpage>
  );
}

