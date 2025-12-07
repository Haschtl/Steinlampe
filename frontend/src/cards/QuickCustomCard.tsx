import { useEffect, useMemo, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

export function QuickCustomCard() {
  const { status, refreshStatus, sendCmd } = useConnection();
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);
  const [customCsv, setCustomCsv] = useState('');
  const [customStep, setCustomStep] = useState(400);

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

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader className="items-start">
          <CardTitle><Trans k="title.quick">Quick Tap Modes</Trans></CardTitle>
          <Input placeholder="Search pattern" value={quickSearch} onChange={(e) => setQuickSearch(e.target.value)} className="w-40" />
        </CardHeader>
        <CardContent className="space-y-3">
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
              <RefreshCw className="mr-1 h-4 w-4" /> Reload
            </Button>
          </div>
          <p className="text-sm text-muted">Tap the physical switch to cycle through selected quick modes.</p>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.custom">Custom Pattern</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <Label><Trans k="label.values">Values (0..1 CSV)</Trans></Label>
          <textarea className="input w-full" rows={3} placeholder="0.2,0.8,1.0,0.4" value={customCsv} onChange={(e) => setCustomCsv(e.target.value)} />
          <div className="flex items-center gap-2">
            <Label className="m-0">Step (ms)</Label>
            <Input
              type="number"
              min={20}
              max={2000}
              step={20}
              value={customStep}
              onChange={(e) => setCustomStep(Number(e.target.value))}
              className="w-28"
              suffix="ms"
              description="Step duration for each custom value"
            />
            <Button onClick={() => applyCustom()}>Set step</Button>
          </div>
          <div className="flex gap-2">
            <Button variant="primary" onClick={applyCustom}>
              Apply
            </Button>
            <Button onClick={() => setCustomCsv('')}>Clear</Button>
            <Button onClick={() => sendCmd('custom')}>Reload</Button>
          </div>
          <p className="text-sm text-muted">
            Current: len={status.customLen ?? '--'} step={status.customStepMs ?? '--'}ms
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
