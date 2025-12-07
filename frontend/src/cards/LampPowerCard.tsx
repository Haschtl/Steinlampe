import { useEffect, useState } from 'react';
import { Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LampPowerCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { status, sendCmd } = useConnection();
  const [brightness, setBrightness] = useState(70);
  const [lampOn, setLampOn] = useState(false);
  const canSync = !!status.switchState && !!status.lampState && status.switchState !== status.lampState;

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

  useEffect(() => {
    setLampOn(status.lampState === 'ON');
  }, [status.lampState]);

  const handleBrightness = (value: number) => {
    const clamped = Math.min(100, Math.max(1, Math.round(value)));
    setBrightness(clamped);
    sendCmd(`bri ${clamped}`).catch((e) => console.warn(e));
  };

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.lamp">Lamp</Trans></CardTitle>
        <div className="flex items-center gap-2">
          <Label className="m-0"><Trans k="label.profile">Profile</Trans></Label>
          <select className="input" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
            <option value="1">1</option>
            <option value="2">2</option>
            <option value="3">3</option>
          </select>
          <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}><Trans k="btn.load">Load</Trans></Button>
          <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}><Trans k="btn.save">Save</Trans></Button>
        </div>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex flex-wrap items-center gap-3">
          <span className="chip-muted">Switch: {status.switchState ?? '--'}</span>
          <span className="chip-muted">Touch: {status.touchState ?? '--'}</span>
          <Button size="sm" onClick={() => sendCmd('sync')} disabled={!canSync}>
            <RefreshCw className="mr-1 h-4 w-4" /> Sync
          </Button>
          <Button size="sm" variant="primary" onClick={() => handleLampToggle(!lampOn)}>
            {lampOn ? (
              <>
                <Pause className="mr-1 h-4 w-4" /> Off
              </>
            ) : (
              <>
                <Flashlight className="mr-1 h-4 w-4" /> On
              </>
            )}
          </Button>
        </div>
        <div className="space-y-2">
          <div className="flex items-center justify-between">
            <span className="text-sm text-muted"><Trans k="label.brightness">Brightness</Trans></span>
            <Input
              type="number"
              min={1}
              max={100}
              value={brightness}
              onChange={(e) => handleBrightness(Number(e.target.value))}
              onBlur={(e) => handleBrightness(Number(e.target.value))}
              className="w-24 text-right"
              suffix="%"
            />
          </div>
          <input
            type="range"
            min="1"
            max="100"
            value={brightness}
            onChange={(e) => handleBrightness(Number(e.target.value))}
            className="w-full accent-accent"
          />
        </div>
      </CardContent>
    </Card>
  );
}
