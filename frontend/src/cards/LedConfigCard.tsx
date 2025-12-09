import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LedConfigCard() {
  const { status, sendCmd } = useConnection();
  const [cap, setCap] = useState(100);
  const [pwmCurve, setPwmCurve] = useState(1.8);

  useEffect(() => {
    if (typeof status.cap === "number") setCap(Math.round(status.cap));
    if (typeof status.pwmCurve === "number") setPwmCurve(status.pwmCurve);
  }, [status.cap, status.pwmCurve]);

  const handleCap = () => {
    const clamped = Math.min(100, Math.max(1, Math.round(cap)));
    setCap(clamped);
    sendCmd(`bri cap ${clamped}`).catch((e) => console.warn(e));
  };

  const handlePwm = (val: number) => {
    setPwmCurve(val);
    if (!Number.isNaN(val))
      sendCmd(`pwm curve ${val.toFixed(2)}`).catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.ledConfig">Led configuration</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div>
          <Label>
            <Trans k="label.cap">Max. Helligkeit (%)</Trans>
          </Label>
          <div className="flex items-center gap-2">
            <Input
              type="number"
              min={1}
              max={100}
              value={cap}
              onChange={(e) => setCap(Number(e.target.value))}
              className="w-28"
              suffix="%"
            />
            <Button onClick={handleCap}>
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </div>
        </div>
        <div>
          <Label>
            <Trans k="label.pwm">PWM Curve</Trans>
          </Label>
          <div className="flex items-center gap-2">
            <Input
              type="number"
              min={0.5}
              max={4}
              step={0.1}
              value={pwmCurve}
              onChange={(e) => setPwmCurve(Number(e.target.value))}
              onBlur={(e) => handlePwm(Number(e.target.value))}
              className="w-28"
              suffix="γ"
            />
          </div>
          <p className="text-xs text-muted-foreground mt-1">
            <Trans k="desc.pwm">
              Gamma to linearize LED brightness (0.5–4). Pick a simple step pattern (e.g. “Stufen”) and tweak until each step looks equally bright.
            </Trans>
          </p>
        </div>
      </CardContent>
    </Card>
  );
}
