import { Link } from '@tanstack/react-router';
import { Gauge, Hand, Lightbulb, Music, Radar, Sun, ToggleLeft, Users } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { IdleOffCard } from '@/cards/IdleOffCard';
import { GestureMappingCard } from '@/cards/GestureMappingCard';
import { QuickModesCard } from '@/cards/QuickModesCard';
import { CustomPatternCard } from '@/cards/CustomPatternCard';
import { useConnection } from '@/context/connection';
import { useStatusGroupFetch } from '@/hooks/useStatusGroupFetch';
import { Trans, useI18n } from '@/i18n';

export function AdvancedOverview() {
  const { status } = useConnection();
  const { t } = useI18n();

  // Gesture mappings/quick modes (MISC|), idle-off timer (TIMERS|), and
  // GestureMappingCard's own mesh-node-count target list (MESHSTATUS|) all
  // used to ride along on the general status payload, refreshed here via a
  // full refreshStatus() (= "status raw") just to get them - replaced with
  // their own targeted on-demand fetches once those fields moved off the
  // core line (see memory: status protocol redesign's 2026-11-28 follow-up).
  useStatusGroupFetch('misc');
  useStatusGroupFetch('timers');
  useStatusGroupFetch('mesh');

  const tiles = [
    { to: '/advanced/led' as const, icon: Lightbulb, labelKey: 'nav.led', labelFallback: 'LED', descKey: 'desc.tile.led', descFallback: 'Gamma curve, min/max brightness', show: true },
    { to: '/advanced/poti' as const, icon: Gauge, labelKey: 'nav.poti', labelFallback: 'Poti', descKey: 'desc.tile.poti', descFallback: 'Knob calibration & gestures', show: status.hasPoti !== false },
    {
      to: '/advanced/taster' as const,
      icon: ToggleLeft,
      labelKey: 'nav.taster',
      labelFallback: 'Taster',
      descKey: 'desc.tile.taster',
      descFallback: 'Buttons & switches',
      show: status.hasPush !== false || (status.buttons?.length ?? 0) > 0,
    },
    { to: '/advanced/anwesenheit' as const, icon: Users, labelKey: 'nav.anwesenheit', labelFallback: 'Anwesenheit', descKey: 'desc.tile.anwesenheit', descFallback: 'BLE presence detection', show: status.hasPresenceBle !== false },
    { to: '/advanced/musik' as const, icon: Music, labelKey: 'nav.musik', labelFallback: 'Musik', descKey: 'desc.tile.musik', descFallback: 'Sound-reactive brightness', show: status.hasMusic !== false },
    { to: '/advanced/radar' as const, icon: Radar, labelKey: 'nav.radar', labelFallback: 'Radar', descKey: 'desc.tile.radar', descFallback: 'Presence & distance sensing', show: status.hasRadar !== false },
    { to: '/advanced/licht' as const, icon: Sun, labelKey: 'nav.licht', labelFallback: 'Licht', descKey: 'desc.tile.licht', descFallback: 'Ambient light sensor', show: status.hasLight !== false },
    {
      to: '/advanced/touch' as const,
      icon: Hand,
      labelKey: 'nav.touch',
      labelFallback: 'Touch',
      descKey: 'desc.tile.touch',
      descFallback: 'Touch surface input',
      show: status.hasTouch !== false && status.touchState !== undefined && status.touchState !== 'N/A',
    },
  ].filter((tile) => tile.show);

  return (
    <div className="space-y-4">
      <IdleOffCard />
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
                  className="card flex flex-col items-center gap-1.5 p-4 text-center transition hover:border-accent hover:text-accent"
                >
                  <tile.icon className="h-6 w-6" />
                  <span className="text-sm font-medium">{t(tile.labelKey, tile.labelFallback)}</span>
                  <span className="text-xs text-muted">{t(tile.descKey, tile.descFallback)}</span>
                </Link>
              ))}
            </div>
          </CardContent>
        </Card>
      )}
      <GestureMappingCard />
      <QuickModesCard />
      <CustomPatternCard />
    </div>
  );
}
