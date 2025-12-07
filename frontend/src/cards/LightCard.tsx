import { useEffect, useState } from 'react';
import { Sun } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LightCard() {
  const { status, sendCmd } = useConnection();
  const [enabled, setEnabled] = useState(true);
  const [gain, setGain] = useState(1);

  useEffect(() => {
    if (typeof status.lightEnabled === 'boolean') setEnabled(status.lightEnabled);
    if (typeof status.lightGain === 'number') setGain(status.lightGain);
  }, [status.lightEnabled, status.lightGain]);

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle>
          <Trans k="title.light">Light</Trans>
        </CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={enabled}
            onChange={(e) => {
              const next = e.target.checked;
              setEnabled(next);
              sendCmd(`light ${next ? 'on' : 'off'}`);
            }}
          />{' '}
          Light
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-2">
          <Sun className="h-4 w-4 text-muted" />
          <Label className="m-0">Light gain</Label>
          <Input
            type="number"
            min={0.1}
            max={5}
            step={0.1}
            value={gain}
            onChange={(e) => setGain(Number(e.target.value))}
            onBlur={(e) => sendCmd(`light gain ${e.target.value}`)}
            className="w-24"
            suffix="x"
          />
          <Button onClick={() => sendCmd('light calib')}>Calibrate</Button>
        </div>
      </CardContent>
    </Card>
  );
}
