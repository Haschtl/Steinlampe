import { useEffect, useState } from 'react';
import { Sun } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { RangeSliderRow } from '@/components/ui/range-slider-row';
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
  const [rawMin, setRawMin] = useState<number | undefined>();
  const [rawMax, setRawMax] = useState<number | undefined>();

  useEffect(() => {
    if (typeof status.lightEnabled === 'boolean') setEnabled(status.lightEnabled);
    if (typeof status.lightGain === 'number') setGain(status.lightGain);
    if (typeof status.lightAlpha === 'number') setAlpha(status.lightAlpha);
    if (typeof status.lightClampMin === 'number') setClampMin(status.lightClampMin);
    if (typeof status.lightClampMax === 'number') setClampMax(status.lightClampMax);
    if (typeof status.lightRaw === 'number') setRaw(status.lightRaw);
    if (typeof status.lightRawMin === 'number') setRawMin(status.lightRawMin);
    if (typeof status.lightRawMax === 'number') setRawMax(status.lightRawMax);
  }, [status.lightEnabled, status.lightGain, status.lightAlpha, status.lightClampMin, status.lightClampMax, status.lightRaw, status.lightRawMin, status.lightRawMax]);

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
      <CardContent className="space-y-4">
        <div className="grid gap-3">
          <div className="flex flex-wrap items-center gap-2">
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
            </div>
            <div className="flex items-center gap-2">
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
            </div>
            <div className="flex flex-wrap gap-2">
              <Button size="sm" onClick={() => sendCmd('light calib min')}>
                <Trans k="btn.calibrateMin">Calib min</Trans>
              </Button>
              <Button size="sm" onClick={() => sendCmd('light calib max')}>
                <Trans k="btn.calibrateMax">Calib max</Trans>
              </Button>
            </div>
          </div>
          <p className="text-xs text-muted leading-snug">
            <Trans k="desc.lightCalib">
              Tip: First press “Calib min” while covering the sensor (dark), then “Calib max” under bright light so slow sensors get a clean span.
            </Trans>
          </p>
          <div className="flex flex-wrap items-center gap-3 text-xs text-foreground/80">
            <span className="chip-muted">
              <strong>Raw:</strong> {raw !== undefined ? raw.toFixed(2) : '—'}
            </span>
            <span className="chip-muted">
              <strong>Min:</strong> {rawMin !== undefined ? rawMin.toFixed(2) : '—'}
            </span>
            <span className="chip-muted">
              <strong>Max:</strong> {rawMax !== undefined ? rawMax.toFixed(2) : '—'}
            </span>
          </div>
        </div>
        <RangeSliderRow
          label={<Trans k="label.clamp">Dim-Limits</Trans>}
          description={<Trans k="desc.clamp">Min/Max Zielhelligkeit aus Lichtsensor</Trans>}
          min={0}
          max={1.5}
          step={0.01}
          values={[clampMin, clampMax]}
          onChange={([mn, mx]) => {
            setClampMin(mn);
            setClampMax(mx);
            sendCmd(`light clamp ${mn} ${mx}`);
          }}
        />
      </CardContent>
    </Card>
  );
}
