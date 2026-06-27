import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { useSyncedValue } from '@/hooks/useSyncedValue';
import { Trans } from '@/i18n';

export function PushButtonCard() {
  const { status, sendCmd } = useConnection();
  const [enabled, setEnabled] = useState(true);
  const [debounce, editDebounce] = useSyncedValue(status.pushDebounceMs, {
    tolerance: 1,
    send: (v) => sendCmd(`push debounce ${Math.round(v)}`).catch((e) => console.warn(e)),
  });
  const [doubleMs, editDoubleMs] = useSyncedValue(status.pushDoubleMs, {
    tolerance: 1,
    send: (v) => sendCmd(`push double ${Math.round(v)}`).catch((e) => console.warn(e)),
  });
  const [holdMs, editHoldMs] = useSyncedValue(status.pushHoldMs, {
    tolerance: 1,
    send: (v) => sendCmd(`push hold ${Math.round(v)}`).catch((e) => console.warn(e)),
  });
  const [stepMs, editStepMs] = useSyncedValue(status.pushStepMs, {
    tolerance: 1,
    send: (v) => sendCmd(`push step_ms ${Math.round(v)}`).catch((e) => console.warn(e)),
  });
  const [step, editStep] = useSyncedValue(status.pushStep, {
    tolerance: 0.003,
    send: (v) => sendCmd(`push step ${v.toFixed(3)}`).catch((e) => console.warn(e)),
  });

  useEffect(() => {
    if (typeof status.pushEnabled === 'boolean') setEnabled(status.pushEnabled);
  }, [status.pushEnabled]);

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
            value: debounce ?? 25,
          }}
          onInputChange={(val) => {
            if (!Number.isNaN(val)) editDebounce(val);
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
            value: doubleMs ?? 450,
          }}
          onInputChange={(val) => {
            if (!Number.isNaN(val)) editDoubleMs(val);
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
            value: holdMs ?? 700,
          }}
          onInputChange={(val) => {
            if (!Number.isNaN(val)) editHoldMs(val);
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
            value: stepMs ?? 180,
          }}
          onInputChange={(val) => {
            if (!Number.isNaN(val)) editStepMs(val);
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
            value: (step ?? 0.05) * 100,
          }}
          onInputChange={(val) => {
            const pct = val / 100;
            if (!Number.isNaN(pct)) editStep(pct);
          }}
          disabled={!enabled}
        />
      </CardContent>
    </Card>
  );
}
