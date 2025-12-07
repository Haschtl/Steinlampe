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
        <CardTitle><Trans k="title.presence">Presence</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`presence ${e.target.checked ? 'on' : 'off'}`)} /> Presence
          </label>
          <Button onClick={() => sendCmd('presence set me')}>
            <Shield className="mr-1 h-4 w-4" /> Set me
          </Button>
          <Button onClick={() => sendCmd('presence clear')}>Clear</Button>
          <Button onClick={() => sendCmd('presence')}>Status</Button>
        </div>
        <p className="text-sm text-muted">Status: {status.presence ?? '---'}</p>
        <p className="text-xs text-muted">Switch: {status.switchState ?? '--'} | Touch: {status.touchState ?? '--'}</p>
      </CardContent>
    </Card>
  );
}
