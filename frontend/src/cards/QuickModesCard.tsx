import { useEffect, useMemo, useState } from 'react';
import { toast } from 'react-toastify';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Collapsible } from '@/components/ui/collapsible';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { patternGroups, patternLabel, patternLabels } from '@/data/patterns';
import { PROFILE_SLOTS } from '@/data/profiles';
import { Trans, useI18n } from '@/i18n';

export function QuickModesCard() {
  const { t } = useI18n();
  const { status, sendCmd } = useConnection();
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

  const searching = quickSearch.trim().length > 0;
  const filteredFlat = useMemo(
    () => patternOptions.filter((p) => p.label.toLowerCase().includes(quickSearch.toLowerCase())),
    [patternOptions, quickSearch],
  );
  const groupedOptions = useMemo(() => {
    const patternCount = status.patternCount || patternLabels.length;
    const groups = patternGroups
      .map((g) => ({ title: g.title, items: patternOptions.filter((p) => g.indices.includes(p.idx)) }))
      .filter((g) => g.items.length > 0);
    const profileItems = patternOptions.filter((p) => p.idx > patternCount);
    if (profileItems.length > 0) groups.push({ title: t('title.profiles', 'Profiles'), items: profileItems });
    return groups;
  }, [patternOptions, status.patternCount, t]);

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
        {searching ? (
          <div className="flex flex-wrap gap-2 max-h-48 overflow-y-auto">
            {filteredFlat.map((p) => (
              <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                <input type="checkbox" className="accent-accent" checked={quickSelection.includes(p.idx)} onChange={() => toggleQuickSelection(p.idx)} /> {p.label}
              </label>
            ))}
          </div>
        ) : (
          <div className="max-h-64 space-y-2 overflow-y-auto">
            {groupedOptions.map((g) => (
              <Collapsible key={g.title} title={g.title}>
                <div className="flex flex-wrap gap-2">
                  {g.items.map((p) => (
                    <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                      <input type="checkbox" className="accent-accent" checked={quickSelection.includes(p.idx)} onChange={() => toggleQuickSelection(p.idx)} /> {p.label}
                    </label>
                  ))}
                </div>
              </Collapsible>
            ))}
          </div>
        )}
        <div className="text-sm text-muted">Current: {status.quickCsv || '—'}</div>
        <div className="flex gap-2">
          <Button onClick={saveQuickSelection}><Trans k="btn.saveQuick">Save quick list</Trans></Button>
        </div>
        <p className="text-sm text-muted"><Trans k="desc.tapSwitch">Tap the physical switch to cycle through selected quick modes.</Trans></p>
      </CardContent>
    </Card>
  );
}
