import { Shield } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans } from '@/i18n';

type Props = {
  presenceText?: string;
  onPresenceToggle: (on: boolean) => void;
  onPresenceMe: () => void;
  onPresenceClear: () => void;
  onTouchCalib: () => void;
  onTouchDebug: () => void;
  onTouchDimToggle: (on: boolean) => void;
};

export function PresenceTouchCard({ presenceText, onPresenceToggle, onPresenceMe, onPresenceClear, onTouchCalib, onTouchDebug, onTouchDimToggle }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.presence">Presence &amp; Touch</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onPresenceToggle(e.target.checked)} /> Presence
          </label>
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onTouchDimToggle(e.target.checked)} /> TouchDim
          </label>
          <Button onClick={onPresenceMe}>
            <Shield className="mr-1 h-4 w-4" /> Set me
          </Button>
          <Button onClick={onPresenceClear}>Clear</Button>
        </div>
        <p className="text-sm text-muted">Status: {presenceText ?? '---'}</p>
        <div className="flex gap-2">
          <Button onClick={onTouchCalib}>Calibrate touch</Button>
          <Button onClick={onTouchDebug}>Touch debug</Button>
        </div>
      </CardContent>
    </Card>
  );
}
