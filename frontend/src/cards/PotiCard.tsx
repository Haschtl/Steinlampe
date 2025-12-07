import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function PotiCard() {
  const { status, sendCmd } = useConnection();
  const [enabled, setEnabled] = useState(true);
  const [alpha, setAlpha] = useState(0.25);
  const [delta, setDelta] = useState(0.025);
  const [off, setOff] = useState(0.02);
  const [sample, setSample] = useState(80);

  useEffect(() => {
    if (typeof status.potiEnabled === 'boolean') setEnabled(status.potiEnabled);
    if (typeof status.potiAlpha === 'number') setAlpha(status.potiAlpha);
    if (typeof status.potiDelta === 'number') setDelta(status.potiDelta);
    if (typeof status.potiOff === 'number') setOff(status.potiOff);
    if (typeof status.potiSample === 'number') setSample(status.potiSample);
  }, [status.potiAlpha, status.potiDelta, status.potiEnabled, status.potiOff, status.potiSample]);

  const apply = (cmd: string) => sendCmd(cmd).catch((err) => console.warn(err));

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle>
          <Trans k="title.poti">Potentiometer</Trans>
        </CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={enabled}
            onChange={(e) => {
              const next = e.target.checked;
              setEnabled(next);
              apply(`poti ${next ? 'on' : 'off'}`);
            }}
          />{' '}
          <Trans k="label.enabled">Enabled</Trans>
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <SliderRow
          label={<Trans k="label.alpha">Alpha</Trans>}
          description={<Trans k="desc.potiAlpha">Filter strength</Trans>}
          inputProps={{
            min: 0.01,
            max: 1,
            step: 0.01,
            value: alpha,
          }}
          onInputChange={(val) => {
            setAlpha(val);
            if (!Number.isNaN(val)) apply(`poti alpha ${val.toFixed(2)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.delta">Delta</Trans>}
          description={<Trans k="desc.potiDelta">Min. change before updating</Trans>}
          inputProps={{
            min: 0.001,
            max: 0.3,
            step: 0.001,
            value: delta,
          }}
          onInputChange={(val) => {
            setDelta(val);
            if (!Number.isNaN(val)) apply(`poti delta ${val.toFixed(3)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.offThresh">Off threshold</Trans>}
          description={<Trans k="desc.potiOff">Below this the lamp switches off</Trans>}
          inputProps={{
            min: 0,
            max: 0.4,
            step: 0.005,
            value: off,
          }}
          onInputChange={(val) => {
            setOff(val);
            if (!Number.isNaN(val)) apply(`poti off ${val.toFixed(3)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.sampleMs">Sample (ms)</Trans>}
          description={<Trans k="desc.potiSample">Read interval</Trans>}
          inputProps={{
            min: 10,
            max: 2000,
            step: 10,
            value: sample,
          }}
          onInputChange={(val) => {
            setSample(val);
            if (!Number.isNaN(val)) apply(`poti sample ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
      </CardContent>
    </Card>
  );
}
