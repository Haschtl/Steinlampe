import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function TouchCard() {
  const { sendCmd } = useConnection();

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle><Trans k="title.touch">Touch</Trans></CardTitle>
        <label className="pill cursor-pointer">
          <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`touchdim ${e.target.checked ? 'on' : 'off'}`)} /> TouchDim
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <Button onClick={() => sendCmd('calibrate touch')}><Trans k="btn.calibrate">Calibrate</Trans></Button>
          <Button onClick={() => sendCmd('touch')}><Trans k="btn.debug">Debug</Trans></Button>
          <Button onClick={() => sendCmd('touch thr 0.5')}><Trans k="label.clapThr">Clap thr</Trans></Button>
          <Button onClick={() => sendCmd('touch cool 800')}><Trans k="label.cool">Cooldown</Trans></Button>
        </div>
      </CardContent>
    </Card>
  );
}
