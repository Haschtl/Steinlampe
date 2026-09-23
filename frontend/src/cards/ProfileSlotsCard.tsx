import { useEffect, useState } from 'react';
import { AlertTriangle, Download, Save } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { useConnection } from '@/context/connection';
import { patternLabel } from '@/data/patterns';
import { Trans, useI18n } from '@/i18n';

type ProfilePreview = {
  patternIdx?: number;
  brightnessPct?: number;
  rampOnMs?: number;
  rampOffMs?: number;
  raw?: string;
};

function parseProfileCfg(cfg: string): ProfilePreview {
  if (!cfg || cfg === 'empty') return { raw: cfg };
  const kv: Record<string, string> = {};
  cfg.split(' ').forEach((tok) => {
    const eq = tok.indexOf('=');
    if (eq > 0) kv[tok.slice(0, eq)] = tok.slice(eq + 1);
  });
  return {
    patternIdx: kv.mode ? parseInt(kv.mode, 10) : undefined,
    brightnessPct: kv.bri ? Math.round(parseFloat(kv.bri) * 100) : undefined,
    rampOnMs: kv.ramp_on_ms ? parseInt(kv.ramp_on_ms, 10) : undefined,
    rampOffMs: kv.ramp_off_ms ? parseInt(kv.ramp_off_ms, 10) : undefined,
    raw: cfg,
  };
}

function previewText(p: ProfilePreview | undefined, t: (k: string, fb: string) => string): string {
  if (!p || !p.raw) return t('label.profileLoading', 'Lädt…');
  if (p.raw === 'empty') return t('label.profileEmpty', 'Leer');
  const parts: string[] = [];
  if (p.patternIdx) parts.push(patternLabel(p.patternIdx));
  if (typeof p.brightnessPct === 'number') parts.push(`${p.brightnessPct}%`);
  if (typeof p.rampOnMs === 'number' || typeof p.rampOffMs === 'number') {
    parts.push(`Ramp ${p.rampOnMs ?? '?'}/${p.rampOffMs ?? '?'}ms`);
  }
  return parts.length > 0 ? parts.join(' · ') : t('label.profileUnknown', 'Unbekannt');
}

export function ProfileSlotsCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { sendCmd, log } = useConnection();
  const { t } = useI18n();
  const [previews, setPreviews] = useState<Record<number, ProfilePreview>>({});
  const [confirmSlot, setConfirmSlot] = useState<number | null>(null);

  const refreshPreviews = () => {
    [1, 2, 3].forEach((slot) => sendCmd(`profile show ${slot}`).catch((e) => console.warn(e)));
  };

  useEffect(() => {
    refreshPreviews();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    const next: Record<number, ProfilePreview> = {};
    let changed = false;
    for (const slot of [1, 2, 3]) {
      const entry = [...log].reverse().find((l) => l.line.startsWith(`[Profile ${slot}]`));
      if (entry) {
        const cfg = entry.line.replace(`[Profile ${slot}] `, '');
        next[slot] = parseProfileCfg(cfg);
        changed = true;
      }
    }
    if (changed) setPreviews((prev) => ({ ...prev, ...next }));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [log]);

  const handleLoad = (slot: number) => {
    setProfileSlot(String(slot));
    sendCmd(`profile load ${slot}`)
      .then(() => setTimeout(refreshPreviews, 200))
      .catch((e) => console.warn(e));
  };

  const handleSaveConfirmed = (slot: number) => {
    setConfirmSlot(null);
    sendCmd(`profile save ${slot}`)
      .then(() => setTimeout(refreshPreviews, 200))
      .catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.profiles">Profile</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-2">
        {[1, 2, 3].map((slot) => (
          <div
            key={slot}
            className={`flex items-center justify-between gap-2 rounded-lg border px-3 py-2 ${
              profileSlot === String(slot) ? 'border-accent bg-accent/10' : 'border-border bg-panel/10'
            }`}
          >
            <div className="min-w-0 flex-1">
              <div className="text-xs text-muted">
                <Trans k="label.profile">Profile</Trans> {slot}
              </div>
              <div className="truncate text-sm font-medium">{previewText(previews[slot], t)}</div>
            </div>
            <div className="flex items-center gap-1">
              <Button
                size="sm"
                variant="ghost"
                title={t('btn.load', 'Load')}
                aria-label={t('btn.load', 'Load')}
                onClick={() => handleLoad(slot)}
              >
                <Download className="h-4 w-4" />
              </Button>
              <Button
                size="sm"
                variant="ghost"
                title={t('btn.save', 'Save')}
                aria-label={t('btn.save', 'Save')}
                onClick={() => setConfirmSlot(slot)}
              >
                <Save className="h-4 w-4" />
              </Button>
            </div>
          </div>
        ))}
      </CardContent>
      <Dialog open={confirmSlot !== null} onOpenChange={(o) => !o && setConfirmSlot(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <AlertTriangle className="h-4 w-4 text-amber-400" />
              <Trans k="title.overwriteProfile">Slot überschreiben?</Trans>
            </DialogTitle>
            <DialogDescription>
              <Trans k="desc.overwriteProfile">
                Die aktuellen Einstellungen (Pattern, Helligkeit, Ramps) werden in diesem Slot gespeichert und der bisherige Inhalt geht verloren.
              </Trans>
              {confirmSlot !== null && previews[confirmSlot]?.raw && previews[confirmSlot]?.raw !== 'empty' && (
                <div className="mt-2 text-xs text-muted">
                  <Trans k="label.currentContent">Bisheriger Inhalt</Trans>: {previewText(previews[confirmSlot], t)}
                </div>
              )}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="ghost" onClick={() => setConfirmSlot(null)}>
              <Trans k="btn.cancel">Abbrechen</Trans>
            </Button>
            <Button variant="danger" onClick={() => confirmSlot !== null && handleSaveConfirmed(confirmSlot)}>
              <Save className="mr-1 h-4 w-4" /> <Trans k="btn.overwrite">Überschreiben</Trans>
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </Card>
  );
}
