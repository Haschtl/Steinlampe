import { useEffect, useMemo, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

function PreviewGraph({ values }: { values: number[] }) {
  const points = values.map((v, idx) => {
    const x = (idx / Math.max(1, values.length - 1)) * 100;
    const y = 100 - Math.min(100, Math.max(0, v * 100));
    return `${x},${y}`;
  });
  return (
    <svg viewBox="0 0 100 100" className="h-24 w-full rounded border border-border bg-panel">
      <polyline points={points.join(' ')} fill="none" stroke="#22d3ee" strokeWidth="2" />
      {values.map((v, idx) => {
        const x = (idx / Math.max(1, values.length - 1)) * 100;
        const y = 100 - Math.min(100, Math.max(0, v * 100));
        return <circle key={idx} cx={x} cy={y} r="1.8" fill="#22d3ee" />;
      })}
    </svg>
  );
}

export function QuickCustomCard() {
  const { status, refreshStatus, sendCmd } = useConnection();
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);
  const [customCsv, setCustomCsv] = useState('');
  const [customStep, setCustomStep] = useState(400);
  const [profileSlot, setProfileSlot] = useState('1');

  useEffect(() => {
    if (status.quickCsv) {
      const nums = status.quickCsv
        .split(',')
        .map((n) => parseInt(n.trim(), 10))
        .filter((n) => !Number.isNaN(n));
      setQuickSelection(nums);
    }
  }, [status.quickCsv]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabels[i] ? `${i + 1} - ${patternLabels[i]}` : `Pattern ${i + 1}`,
    }));
  }, [status.patternCount]);

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => (prev.includes(idx) ? prev.filter((n) => n !== idx) : [...prev, idx].sort((a, b) => a - b)));
  };

  const saveQuickSelection = () => {
    if (quickSelection.length === 0) return;
    sendCmd(`quick ${quickSelection.join(',')}`).catch((e) => console.warn(e));
  };

  const applyCustom = () => {
    if (customCsv.trim()) sendCmd(`custom ${customCsv.replace(/\s+/g, '')}`);
    if (customStep) sendCmd(`custom step ${customStep}`);
  };

  const values = customCsv
    .split(',')
    .map((v) => parseFloat(v.trim()))
    .filter((n) => !Number.isNaN(n) && n >= 0 && n <= 1);

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader className="items-start">
          <CardTitle><Trans k="title.quick">Quick Tap Modes</Trans></CardTitle>
          <Input placeholder="Search pattern" value={quickSearch} onChange={(e) => setQuickSearch(e.target.value)} className="w-40" />
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex items-center gap-2">
            <Label className="m-0"><Trans k="label.profile">Profile</Trans></Label>
            <select className="input w-20" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
              <option value="1">1</option>
              <option value="2">2</option>
              <option value="3">3</option>
            </select>
            <Button size="sm" onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load</Button>
          </div>
          <div className="flex flex-wrap gap-2 max-h-48 overflow-y-auto">
            {patternOptions
              .filter((p) => p.label.toLowerCase().includes(quickSearch.toLowerCase()))
              .map((p) => (
                <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                  <input type="checkbox" className="accent-accent" checked={quickSelection.includes(p.idx)} onChange={() => toggleQuickSelection(p.idx)} /> {p.label}
                </label>
              ))}
          </div>
          <div className="flex gap-2">
            <Button onClick={saveQuickSelection}>Save quick list</Button>
            <Button onClick={refreshStatus}>
              <RefreshCw className="mr-1 h-4 w-4" /> <Trans k="btn.reload">Reload</Trans>
            </Button>
          </div>
          <p className="text-sm text-muted"><Trans k="desc.tapSwitch">Tap the physical switch to cycle through selected quick modes.</Trans></p>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.custom">Custom Pattern</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <Label><Trans k="label.values">Values (0..1 CSV)</Trans></Label>
          <textarea className="input w-full" rows={3} placeholder="0.2,0.8,1.0,0.4" value={customCsv} onChange={(e) => setCustomCsv(e.target.value)} />
          <PreviewGraph values={values.length ? values : [0, 1]} />
          <div className="flex items-center gap-2">
            <Label className="m-0"><Trans k="label.step">Step</Trans> (ms)</Label>
            <Input
              type="number"
              min={20}
              max={2000}
              step={20}
              value={customStep}
              onChange={(e) => setCustomStep(Number(e.target.value))}
              className="w-28"
              suffix="ms"
              description={<Trans k="desc.customStep">Step duration for each custom value</Trans>}
            />
            <Button onClick={() => applyCustom()}><Trans k="btn.apply">Apply</Trans></Button>
          </div>
          <div className="flex gap-2">
            <Button variant="primary" onClick={applyCustom}>
              <Trans k="btn.apply">Apply</Trans>
            </Button>
            <Button onClick={() => setCustomCsv('')}><Trans k="btn.clear">Clear</Trans></Button>
            <Button onClick={() => sendCmd('custom')}><Trans k="btn.reload">Reload</Trans></Button>
          </div>
          <p className="text-sm text-muted">
            Current: len={status.customLen ?? '--'} step={status.customStepMs ?? '--'}ms
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
