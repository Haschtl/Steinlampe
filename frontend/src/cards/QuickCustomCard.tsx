import { useEffect, useMemo, useState } from 'react';
import { RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';
import { Trans, useI18n } from '@/i18n';

function PreviewGraph({ values, stepMs }: { values: number[]; stepMs?: number }) {
  const cleaned = (values.length ? values : [0, 1]).map((v) => Math.min(1, Math.max(0, v)));
  if (cleaned.length === 1) cleaned.push(cleaned[0]);
  const pts = cleaned.map((v, idx) => {
    const x = (idx / Math.max(1, cleaned.length - 1)) * 200;
    const y = 100 - v * 100;
    return { x, y };
  });
  const line = pts.map((p) => `${p.x},${p.y}`).join(' ');
  const area = `M0,100 L${line} L200,100 Z`;
  const totalMs = stepMs && cleaned.length ? stepMs * cleaned.length : undefined;
  const min = Math.min(...cleaned);
  const max = Math.max(...cleaned);
  return (
    <div className="space-y-2 w-full">
      <div className="relative rounded-lg border border-border bg-panel/70 p-2 shadow-inner w-full">
        <svg viewBox="0 0 200 100" className="h-40 w-full">
          <defs>
            <linearGradient id="customFill" x1="0%" x2="0%" y1="0%" y2="100%">
              <stop offset="0%" stopColor="var(--accent-color)" stopOpacity="0.35" />
              <stop offset="100%" stopColor="var(--accent-color)" stopOpacity="0.05" />
            </linearGradient>
          </defs>
          {[0, 25, 50, 75, 100].map((y) => (
            <line key={y} x1="0" x2="200" y1={y} y2={y} stroke="rgba(255,255,255,0.08)" strokeWidth="0.4" />
          ))}
          {[0, 50, 100, 150, 200].map((x) => (
            <line key={`x${x}`} x1={x} x2={x} y1="0" y2="100" stroke="rgba(255,255,255,0.05)" strokeWidth="0.4" />
          ))}
          <path d={area} fill="url(#customFill)" stroke="none" />
          <polyline points={line} fill="none" stroke="var(--accent-color)" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
          {pts.map((p, idx) => (
            <circle key={idx} cx={p.x} cy={p.y} r="1.8" fill="var(--accent-color)" stroke="#0b0f1a" strokeWidth="0.8" />
          ))}
        </svg>
      </div>
      <div className="flex flex-wrap items-center gap-3 text-xs text-muted">
        <span>Points: {cleaned.length}</span>
        {stepMs ? <span>Step: {stepMs} ms</span> : null}
        {totalMs ? <span>Total: {totalMs} ms</span> : null}
        <span>Min: {min.toFixed(2)}</span>
        <span>Max: {max.toFixed(2)}</span>
      </div>
    </div>
  );
}

export function QuickCustomCard() {
  const { t } = useI18n();
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
        .filter((n) => !Number.isNaN(n) && n > 0);
      setQuickSelection(nums);
    }
  }, [status.quickCsv]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    const base = Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
    const profileOpts = Array.from({ length: 3 }, (_, i) => ({
      idx: count + i + 1,
      label: `${t('label.profile', 'Profile')} ${i + 1}`,
    }));
    return [...base, ...profileOpts];
  }, [status.patternCount, t]);

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

  const parsed = customCsv
    .split(',')
    .map((v) => parseFloat(v.trim()))
    .filter((n) => !Number.isNaN(n));
  const values = (() => {
    const clamped = parsed.map((n) => Math.max(0, Math.min(1, n)));
    if (clamped.length === 1) return [clamped[0], clamped[0]];
    if (clamped.length === 0) return [];
    return clamped;
  })();
  const hasError = parsed.some((n) => Number.isNaN(n) || n < 0 || n > 1);

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader className="items-start">
          <CardTitle><Trans k="title.quick">Quick Tap Modes</Trans></CardTitle>
          <Input placeholder={t('search.pattern', 'Search pattern')} value={quickSearch} onChange={(e) => setQuickSearch(e.target.value)} className="w-40" />
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
            <Button onClick={saveQuickSelection}><Trans k="btn.saveQuick">Save quick list</Trans></Button>
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
          <textarea
            className={`input w-full ${hasError ? 'border-red-500 shadow-[0_0_0_2px_rgba(239,68,68,0.25)]' : ''}`}
            rows={3}
            placeholder="0.2,0.8,1.0,0.4"
            value={customCsv}
            onChange={(e) => setCustomCsv(e.target.value)}
          />
          <PreviewGraph values={values} stepMs={customStep} />
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
