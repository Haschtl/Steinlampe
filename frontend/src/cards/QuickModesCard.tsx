import { useEffect, useMemo, useState } from 'react';
import { toast } from 'react-toastify';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { PatternPalette } from '@/components/PatternPalette';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';
import { PROFILE_SLOTS } from '@/data/profiles';
import { Trans, useI18n } from '@/i18n';

export function QuickModesCard() {
  const { t } = useI18n();
  const { status, sendCmd } = useConnection();
  const [paletteOpen, setPaletteOpen] = useState(false);
  const [quickSelection, setQuickSelection] = useState<number[]>([]);

  // No artificial 64-entry cap here anymore - that only existed because the
  // firmware used to pack patterns AND profile slots into one shared 64-bit
  // mask. Profile slots now have their own independent mask on the firmware
  // side (see memory: quick-mode-profile-mask-overflow), so they're never
  // out of range regardless of how many patterns exist.
  const patternCount = status.patternCount || patternLabels.length;
  const quickMax = useMemo(() => patternCount + PROFILE_SLOTS, [patternCount]);

  useEffect(() => {
    if (status.quickCsv) {
      const nums = status.quickCsv
        .split(',')
        .map((n) => parseInt(n.trim(), 10))
        .filter((n) => !Number.isNaN(n) && n > 0);
      setQuickSelection(nums.filter((n) => n <= quickMax));
    }
  }, [status.quickCsv, quickMax]);

  const optionLabel = (idx: number) =>
    idx > patternCount ? `${t('label.profile', 'Profile')} ${idx - patternCount}` : patternLabel(idx);

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => (prev.includes(idx) ? prev.filter((n) => n !== idx) : [...prev, idx].sort((a, b) => a - b)));
  };

  const saveQuickSelection = (selection: number[]) => {
    if (selection.length === 0) return;
    const filtered = selection.filter((n) => n <= quickMax);
    if (filtered.length === 0) {
      toast.error('Quick list contains only invalid entries');
      return;
    }
    sendCmd(`quick ${filtered.join(',')}`).catch((e) => console.warn(e));
  };

  const handlePaletteOpenChange = (next: boolean) => {
    setPaletteOpen(next);
    if (!next) saveQuickSelection(quickSelection);
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.quick">Quick Tap Modes</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        {quickSelection.length === 0 ? (
          <p className="text-sm text-muted"><Trans k="desc.noQuickSelection">No quick modes selected yet.</Trans></p>
        ) : (
          <div className="flex flex-wrap gap-2">
            {quickSelection.map((idx) => (
              <span key={idx} className="pill whitespace-nowrap">{optionLabel(idx)}</span>
            ))}
          </div>
        )}
        <Button onClick={() => setPaletteOpen(true)}><Trans k="btn.editQuick">Select quick modes</Trans></Button>
        <p className="text-sm text-muted"><Trans k="desc.tapSwitch">Tap the physical switch to cycle through selected quick modes.</Trans></p>
      </CardContent>
      <PatternPalette
        open={paletteOpen}
        setOpen={handlePaletteOpenChange}
        multi
        includeProfiles
        selected={quickSelection}
        onToggle={toggleQuickSelection}
      />
    </Card>
  );
}
