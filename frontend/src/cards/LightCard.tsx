import { Sun } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LightCard() {
  const { sendCmd } = useConnection();

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.light">Light</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`light ${e.target.checked ? 'on' : 'off'}`)} /> Light
          </label>
        </div>
        <div className="flex items-center gap-2">
          <Sun className="h-4 w-4 text-muted" />
          <Label className="m-0">Light gain</Label>
          <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => sendCmd(`light gain ${e.target.value}`)} className="w-24" suffix="x" />
          <Button onClick={() => sendCmd('light calib')}>Calibrate</Button>
        </div>
      </CardContent>
    </Card>
  );
}
