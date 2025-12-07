import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Trans } from '@/i18n';

type Props = {
  cap: number;
  setCap: (v: number) => void;
  idleMinutes: number;
  setIdleMinutes: (v: number) => void;
  pwmCurve: number;
  setPwmCurve: (v: number) => void;
  handleCap: () => void;
  handleIdle: () => void;
  handlePwm: (v: number) => void;
};

export function SettingsCard({ cap, setCap, idleMinutes, setIdleMinutes, pwmCurve, setPwmCurve, handleCap, handleIdle, handlePwm }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.settings">Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div>
          <Label><Trans k="label.cap">Brightness cap (%)</Trans></Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={1} max={100} value={cap} onChange={(e) => setCap(Number(e.target.value))} className="w-28" suffix="%" />
            <Button onClick={handleCap}>Set</Button>
          </div>
        </div>
        <div>
          <Label><Trans k="label.idle">Idle off (min)</Trans></Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" suffix="min" />
            <Button onClick={handleIdle}>Set</Button>
          </div>
        </div>
        <div>
          <Label><Trans k="label.pwm">PWM Curve</Trans></Label>
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
              description="Gamma to linearize LED brightness (0.5–4)"
            />
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
