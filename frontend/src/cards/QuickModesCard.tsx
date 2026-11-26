import { useEffect, useMemo, useState } from 'react';
import { toast } from 'react-toastify';
import { RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';
import { PROFILE_SLOTS } from '@/data/profiles';
import { Trans, useI18n } from '@/i18n';

export function QuickModesCard() {
  const { t } = useI18n();
  const { status, refreshStatus, sendCmd } = useConnection();
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);

  const quickMax = useMemo(() => Math.min((status.patternCount || patternLabels.length) + PROFILE_SLOTS, 64), [status.patternCount]);

  useEffect(() => {
    if (status.quickCsv) {
      const nums = status.quickCsv
        .split(',')
        .map((n) => parseInt(n.trim(), 10))
        .filter((n) => !Number.isNaN(n) && n > 0);
      setQuickSelection(nums.filter((n) => n <= quickMax));
    }
  }, [status.quickCsv, quickMax]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    const base = Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
    const profileOpts = Array.from({ length: PROFILE_SLOTS }, (_, i) => ({
      idx: count + i + 1,
      label: `${t('label.profile', 'Profile')} ${i + 1}`,
    }));
    return [...base, ...profileOpts].filter((p) => p.idx <= quickMax);
  }, [status.patternCount, t, quickMax]);

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => (prev.includes(idx) ? prev.filter((n) => n !== idx) : [...prev, idx].sort((a, b) => a - b)));
  };

  const saveQuickSelection = () => {
    if (quickSelection.length === 0) return;
    const filtered = quickSelection.filter((n) => n <= quickMax);
    if (filtered.length === 0) {
      toast.error('Quick list contains only invalid entries');
      return;
    }
    if (filtered.length !== quickSelection.length) {
      toast.warn('Some quick entries were removed (out of range)');
      setQuickSelection(filtered);
    }
    sendCmd(`quick ${filtered.join(',')}`).catch((e) => console.warn(e));
  };

  return (
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
        <div className="text-sm text-muted">Current: {status.quickCsv || '—'}</div>
        <div className="flex gap-2">
          <Button onClick={saveQuickSelection}><Trans k="btn.saveQuick">Save quick list</Trans></Button>
          <Button onClick={refreshStatus}>
            <RefreshCw className="mr-1 h-4 w-4" /> <Trans k="btn.reload">Reload</Trans>
          </Button>
        </div>
        <p className="text-sm text-muted"><Trans k="desc.tapSwitch">Tap the physical switch to cycle through selected quick modes.</Trans></p>
      </CardContent>
    </Card>
  );
}
