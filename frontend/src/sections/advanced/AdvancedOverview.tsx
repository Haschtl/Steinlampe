import { Link } from '@tanstack/react-router';
import { Gauge, Hand, Music, Radar, Sun, ToggleLeft, Users } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { LedConfigCard } from '@/cards/LedConfigCard';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';

export function AdvancedOverview() {
  const { status } = useConnection();
  const { t } = useI18n();

  const tiles = [
    { to: '/advanced/poti' as const, icon: Gauge, labelKey: 'nav.poti', labelFallback: 'Poti', show: status.hasPoti !== false },
    { to: '/advanced/taster' as const, icon: ToggleLeft, labelKey: 'nav.taster', labelFallback: 'Taster', show: status.hasPush !== false },
    { to: '/advanced/anwesenheit' as const, icon: Users, labelKey: 'nav.anwesenheit', labelFallback: 'Anwesenheit', show: status.hasPresenceBle !== false },
    { to: '/advanced/musik' as const, icon: Music, labelKey: 'nav.musik', labelFallback: 'Musik', show: status.hasMusic !== false },
    { to: '/advanced/radar' as const, icon: Radar, labelKey: 'nav.radar', labelFallback: 'Radar', show: status.hasRadar !== false },
    { to: '/advanced/licht' as const, icon: Sun, labelKey: 'nav.licht', labelFallback: 'Licht', show: status.hasLight !== false },
    {
      to: '/advanced/touch' as const,
      icon: Hand,
      labelKey: 'nav.touch',
      labelFallback: 'Touch',
      show: status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A',
    },
  ].filter((tile) => tile.show);

  return (
    <div className="space-y-4">
      <LedConfigCard />
      {tiles.length > 0 && (
        <Card>
          <CardHeader>
            <CardTitle>
              <Trans k="title.hardwareFeatures">Hardware features</Trans>
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 md:grid-cols-4">
              {tiles.map((tile) => (
                <Link
                  key={tile.to}
                  to={tile.to}
                  className="card flex flex-col items-center gap-2 p-4 text-center transition hover:border-accent hover:text-accent"
                >
                  <tile.icon className="h-6 w-6" />
                  <span className="text-sm font-medium">{t(tile.labelKey, tile.labelFallback)}</span>
                </Link>
              ))}
            </div>
          </CardContent>
        </Card>
      )}
    </div>
  );
}
