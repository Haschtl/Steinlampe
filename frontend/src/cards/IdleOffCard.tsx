import { useEffect, useState } from 'react';
import { Moon } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';

export function IdleOffCard() {
  const { status, sendCmd } = useConnection();
  const { t } = useI18n();
  const [idleMinutes, setIdleMinutes] = useState(0);

  useEffect(() => {
    if (typeof status.idleMinutes === 'number') setIdleMinutes(status.idleMinutes);
  }, [status.idleMinutes]);

  const handleIdle = () => {
    const minutes = Math.max(0, idleMinutes);
    setIdleMinutes(minutes);
    sendCmd(`idleoff ${minutes}`).catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <Moon className="h-4 w-4" />
          <Trans k="title.idleOff">Idle off</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <p className="text-sm text-muted">{t('desc.idleOff', '')}</p>
        <div>
          <Label><Trans k="label.idle">Idle off (min)</Trans></Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" suffix="min" />
            <Button onClick={handleIdle}>Set</Button>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
