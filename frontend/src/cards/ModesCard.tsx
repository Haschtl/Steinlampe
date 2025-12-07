import { useEffect, useMemo, useState } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { SliderRow } from '@/components/ui/slider-row';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

export function ModesCard() {
  const { status, sendCmd } = useConnection();
  const [pattern, setPattern] = useState(1);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);

  useEffect(() => {
    if (status.currentPattern) setPattern(status.currentPattern);
  }, [status.currentPattern]);

  useEffect(() => {
    if (typeof status.patternSpeed === 'number') setPatternSpeed(status.patternSpeed);
  }, [status.patternSpeed]);

  useEffect(() => {
    if (typeof status.patternFade === 'number') {
      setPatternFade(status.patternFade || 1);
      setFadeEnabled(status.patternFade > 0);
    }
  }, [status.patternFade]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
  }, [status.patternCount]);

  const handlePatternChange = (val: number) => {
    setPattern(val);
    sendCmd(`mode ${val}`).catch((e) => console.warn(e));
  };

  const handlePatternSpeed = (val: number) => {
    const clamped = Math.max(0.1, Math.min(5, val));
    setPatternSpeed(clamped);
    sendCmd(`pat scale ${clamped.toFixed(2)}`).catch((e) => console.warn(e));
  };

  const handlePatternFade = (val: number, enable: boolean) => {
    const nextVal = val || 1;
    setPatternFade(nextVal);
    setFadeEnabled(enable);
    if (!enable) {
      sendCmd('pat fade off').catch((e) => console.warn(e));
      return;
    }
    sendCmd('pat fade on').catch((e) => console.warn(e));
    sendCmd(`pat fade amt ${nextVal.toFixed(2)}`).catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.modes">Modes</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex flex-nowrap items-center gap-2">
          <Button
            size="sm"
            onClick={() => {
              const prevIdx = pattern > 1 ? pattern - 1 : patternOptions.length;
              handlePatternChange(prevIdx);
            }}
          >
            <ArrowLeftCircle className="h-4 w-4" />
          </Button>
          <Select value={String(pattern)} onValueChange={(v) => handlePatternChange(parseInt(v, 10))}>
            <SelectTrigger className="flex-1 min-w-0">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {patternOptions.map((p) => (
                <SelectItem key={p.idx} value={String(p.idx)}>
                  {p.idx === status.currentPattern ? `${p.label} (active)` : p.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <Button
            size="sm"
            onClick={() => {
              const nextIdx = pattern < patternOptions.length ? pattern + 1 : 1;
              handlePatternChange(nextIdx);
            }}
          >
            <ArrowRightCircle className="h-4 w-4" />
          </Button>
        </div>
        <div className="space-y-3">
          <SliderRow
            label={<Trans k="label.speed">Speed</Trans>}
            description={<Trans k="desc.patternSpeed">Pattern speed multiplier</Trans>}
            inputProps={{
              min: 0.1,
              max: 5,
              step: 0.1,
              value: patternSpeed,
            }}
            onInputChange={(val) => handlePatternSpeed(val)}
          />
          <SliderRow
            label={<Trans k="label.fadeMul">Pattern Fade</Trans>}
            description={<Trans k="desc.patternFade">Smooth transitions between patterns</Trans>}
            valueLabel={fadeEnabled ? `${patternFade.toFixed(1)}x` : 'Off'}
            disabled={!fadeEnabled}
            inputProps={{
              min: 0,
              max: 2,
              step: 0.01,
              value: fadeEnabled ? patternFade : 0,
            }}
            onInputChange={(val) => {
              handlePatternFade(val || 1, val > 0);
            }}
          />
        </div>
        <div className="flex flex-wrap gap-2">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`auto ${e.target.checked ? 'on' : 'off'}`)} />{' '}
            <span className="inline-flex items-center gap-1">
              <Activity className="h-4 w-4" /> AutoCycle
            </span>
          </label>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={fadeEnabled}
              onChange={(e) => handlePatternFade(patternFade || 1, e.target.checked)}
            />{' '}
            <span className="inline-flex items-center gap-1">
              <Activity className="h-4 w-4" /> <Trans k="label.fadeMul">Pattern Fade</Trans>
            </span>
          </label>
        </div>
      </CardContent>
    </Card>
  );
}
