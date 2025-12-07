import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function SettingsCard() {
  const { status, sendCmd } = useConnection();
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
        <CardTitle><Trans k="title.settings">Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
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
