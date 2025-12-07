import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function TouchCard() {
  const { sendCmd } = useConnection();

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.touch">Touch</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`touchdim ${e.target.checked ? 'on' : 'off'}`)} /> TouchDim
          </label>
          <Button onClick={() => sendCmd('calibrate touch')}>Calibrate touch</Button>
          <Button onClick={() => sendCmd('touch')}>Touch debug</Button>
        </div>
      </CardContent>
    </Card>
  );
}
