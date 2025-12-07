import { Shield } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans } from '@/i18n';

type Props = {
  presenceText?: string;
  onPresenceToggle: (on: boolean) => void;
  onPresenceMe: () => void;
  onPresenceClear: () => void;
};

export function PresenceCard({ presenceText, onPresenceToggle, onPresenceMe, onPresenceClear }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.presence">Presence</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onPresenceToggle(e.target.checked)} /> Presence
          </label>
          <Button onClick={onPresenceMe}>
            <Shield className="mr-1 h-4 w-4" /> Set me
          </Button>
          <Button onClick={onPresenceClear}>Clear</Button>
        </div>
        <p className="text-sm text-muted">Status: {presenceText ?? '---'}</p>
      </CardContent>
    </Card>
  );
}
