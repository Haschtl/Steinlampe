import { Shield } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';
import { useState } from 'react';

export function PresenceCard() {
  const { status, sendCmd } = useConnection();
  const [addr, setAddr] = useState('');
  const {t}=useI18n()

  return (
    <Card>
      <CardHeader className="flex items-center justify-between gap-2">
          <CardTitle><Trans k="title.presence">Presence</Trans></CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.presence?.toUpperCase().includes('ON') ?? false}
              onChange={(e) => sendCmd(`presence ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="title.presence">Presence</Trans>
          </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <div className="flex items-center gap-2">
            <Input
              placeholder={t('label.presenceAddr', 'Presence MAC')}
              value={addr}
              onChange={(e) => setAddr(e.target.value)}
              className="w-48"
            />
            <Button onClick={() => sendCmd(`presence set ${addr || 'me'}`)}>
              <Shield className="mr-1 h-4 w-4" /> <Trans k="btn.save">Set Me</Trans>
            </Button>
            <Button onClick={() => sendCmd('presence clear')}><Trans k="btn.clear">Clear</Trans></Button>
          </div>
          <Button variant="ghost" onClick={() => navigator.bluetooth?.requestDevice({ acceptAllDevices: true }).catch(() => undefined)}>
            <Trans k="btn.scan">Scan</Trans>
          </Button>
        </div>
        <p className="text-sm text-muted">Status: {status.presence ?? '---'}</p>
        <p className="text-xs text-muted">Switch: {status.switchState ?? '--'}</p>
      </CardContent>
    </Card>
  );
}
