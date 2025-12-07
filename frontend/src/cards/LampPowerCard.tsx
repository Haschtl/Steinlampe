import { useEffect, useState } from 'react';
import { Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LampPowerCard() {
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
        <SliderRow
          label={<Trans k="label.brightness">Brightness</Trans>}
          description={<Trans k="desc.brightness">Set output level</Trans>}
          inputProps={{
            min: 1,
            max: 100,
            value: brightness,
          }}
          onInputChange={(val) => handleBrightness(val)}
        />
      </CardContent>
    </Card>
  );
}
