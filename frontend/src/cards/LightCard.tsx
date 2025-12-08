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
  const [alpha, setAlpha] = useState(0.1);
  const [clampMin, setClampMin] = useState(0.2);
  const [clampMax, setClampMax] = useState(1.0);
  const [raw, setRaw] = useState<number | undefined>();

  useEffect(() => {
    if (typeof status.lightEnabled === 'boolean') setEnabled(status.lightEnabled);
    if (typeof status.lightGain === 'number') setGain(status.lightGain);
    if (typeof status.lightAlpha === 'number') setAlpha(status.lightAlpha);
    if (typeof status.lightClampMin === 'number') setClampMin(status.lightClampMin);
    if (typeof status.lightClampMax === 'number') setClampMax(status.lightClampMax);
    if (typeof status.lightRaw === 'number') setRaw(status.lightRaw);
  }, [status.lightEnabled, status.lightGain, status.lightAlpha, status.lightClampMin, status.lightClampMax, status.lightRaw]);

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
        <div className="flex flex-col gap-3">
          <div className="flex items-center gap-2">
            <Sun className="h-4 w-4 text-muted" />
            <Label className="m-0"><Trans k="label.gain">Gain</Trans></Label>
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
            <Label className="m-0"><Trans k="label.alpha">Glättung</Trans></Label>
            <Input
              type="number"
              min={0.001}
              max={0.8}
              step={0.01}
              value={alpha}
              onChange={(e) => setAlpha(Number(e.target.value))}
              onBlur={(e) => sendCmd(`light alpha ${e.target.value}`)}
              className="w-24"
            />
            {raw !== undefined && <span className="text-xs text-muted">raw: {raw.toFixed(0)}</span>}
            <Button onClick={() => sendCmd('light calib')}><Trans k="btn.calibrate">Calibrate</Trans></Button>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <Label className="m-0"><Trans k="label.clamp">Dim-Limits</Trans></Label>
            <Input
              type="number"
              min={0}
              max={1.5}
              step={0.01}
              value={clampMin}
              onChange={(e) => setClampMin(Number(e.target.value))}
              onBlur={(e) => {
                const mn = Number(e.target.value);
                sendCmd(`light clamp ${mn} ${clampMax}`);
              }}
              className="w-30"
              suffix="min"
            />
            <Input
              type="number"
              min={0}
              max={1.5}
              step={0.01}
              value={clampMax}
              onChange={(e) => setClampMax(Number(e.target.value))}
              onBlur={(e) => {
                const mx = Number(e.target.value);
                sendCmd(`light clamp ${clampMin} ${mx}`);
              }}
              className="w-30"
              suffix="max"
            />
            <span className="text-sm text-muted"><Trans k="desc.clamp">Min/Max Zielhelligkeit aus Lichtsensor</Trans></span>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
