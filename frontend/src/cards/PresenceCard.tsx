import { Shield } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function PresenceCard() {
  const { status, sendCmd } = useConnection();

  return (
    <Card>
      <CardHeader>
        <div className="flex items-center justify-between gap-2">
          <CardTitle><Trans k="title.presence">Presence</Trans></CardTitle>
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`presence ${e.target.checked ? 'on' : 'off'}`)} /> Presence
          </label>
        </div>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <Button onClick={() => sendCmd('presence set me')}>
            <Shield className="mr-1 h-4 w-4" /> <Trans k="btn.save">Save</Trans>
          </Button>
          <Button onClick={() => sendCmd('presence clear')}><Trans k="btn.clear">Clear</Trans></Button>
          <Button onClick={() => sendCmd('presence')}><Trans k="btn.status">Status</Trans></Button>
          <Button onClick={() => sendCmd('presence scan')}><Trans k="btn.scan">Scan</Trans></Button>
        </div>
        <p className="text-sm text-muted">Status: {status.presence ?? '---'}</p>
        <p className="text-xs text-muted">Switch: {status.switchState ?? '--'} | Touch: {status.touchState ?? '--'}</p>
      </CardContent>
    </Card>
  );
}
