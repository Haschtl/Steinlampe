import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function PushButtonCard() {
  const { status, sendCmd } = useConnection();
  const [enabled, setEnabled] = useState(true);
  const [debounce, setDebounce] = useState(25);
  const [doubleMs, setDoubleMs] = useState(450);
  const [holdMs, setHoldMs] = useState(700);
  const [stepMs, setStepMs] = useState(180);
  const [step, setStep] = useState(0.05);

  useEffect(() => {
    if (typeof status.pushEnabled === 'boolean') setEnabled(status.pushEnabled);
    if (typeof status.pushDebounceMs === 'number') setDebounce(status.pushDebounceMs);
    if (typeof status.pushDoubleMs === 'number') setDoubleMs(status.pushDoubleMs);
    if (typeof status.pushHoldMs === 'number') setHoldMs(status.pushHoldMs);
    if (typeof status.pushStepMs === 'number') setStepMs(status.pushStepMs);
    if (typeof status.pushStep === 'number') setStep(status.pushStep);
  }, [status.pushDebounceMs, status.pushDoubleMs, status.pushEnabled, status.pushHoldMs, status.pushStep, status.pushStepMs]);

  const apply = (cmd: string) => sendCmd(cmd).catch((err) => console.warn(err));

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle>
          <Trans k="title.push">Push Button</Trans>
        </CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={enabled}
            onChange={(e) => {
              const next = e.target.checked;
              setEnabled(next);
              apply(`push ${next ? 'on' : 'off'}`);
            }}
          />{' '}
          <Trans k="label.enabled">Enabled</Trans>
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <SliderRow
          label={<Trans k="label.debounce">Debounce</Trans>}
          description={<Trans k="desc.pushDebounce">Noise filter</Trans>}
          inputProps={{
            min: 5,
            max: 500,
            step: 5,
            value: debounce,
          }}
          onInputChange={(val) => {
            setDebounce(val);
            if (!Number.isNaN(val)) apply(`push debounce ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.double">Double tap</Trans>}
          description={<Trans k="desc.pushDouble">Max gap for double-press</Trans>}
          inputProps={{
            min: 100,
            max: 5000,
            step: 50,
            value: doubleMs,
          }}
          onInputChange={(val) => {
            setDoubleMs(val);
            if (!Number.isNaN(val)) apply(`push double ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.hold">Hold</Trans>}
          description={<Trans k="desc.pushHold">Hold duration for brightness cycling</Trans>}
          inputProps={{
            min: 200,
            max: 6000,
            step: 50,
            value: holdMs,
          }}
          onInputChange={(val) => {
            setHoldMs(val);
            if (!Number.isNaN(val)) apply(`push hold ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.stepMs">Step interval</Trans>}
          description={<Trans k="desc.pushStepMs">Delay between brightness steps</Trans>}
          inputProps={{
            min: 50,
            max: 2000,
            step: 10,
            value: stepMs,
          }}
          onInputChange={(val) => {
            setStepMs(val);
            if (!Number.isNaN(val)) apply(`push step_ms ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.stepPct">Step size</Trans>}
          description={<Trans k="desc.pushStep">Brightness increment per step</Trans>}
          inputProps={{
            min: 0.5,
            max: 50,
            step: 0.5,
            value: step * 100,
          }}
          onInputChange={(val) => {
            const pct = val / 100;
            setStep(pct);
            if (!Number.isNaN(pct)) apply(`push step ${pct.toFixed(3)}`);
          }}
          disabled={!enabled}
        />
      </CardContent>
    </Card>
  );
}
