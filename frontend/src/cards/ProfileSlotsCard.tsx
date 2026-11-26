import { useCallback, useEffect, useState } from 'react';
import { AlertTriangle, Save } from 'lucide-react';
import { toast } from 'react-toastify';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { patternLabel } from '@/data/patterns';
import { Trans, useI18n } from '@/i18n';
import { PROFILE_SLOTS } from '@/data/profiles';

const SLOT_INDICES = Array.from({ length: PROFILE_SLOTS }, (_, i) => i + 1);

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
  const { sendCmdReliable, log, status, refreshStatus } = useConnection();
  const { t } = useI18n();
  const [previews, setPreviews] = useState<Record<number, ProfilePreview>>({});
  const [confirmSlot, setConfirmSlot] = useState<number | null>(null);

  // writeValueWithResponse (sendCmdReliable), not the fire-and-forget sendCmd:
  // confirmed live on a real Android phone (2026-11-26, chrome://inspect console)
  // that "profile show 1..6" fired as a staggered sequence of
  // writeValueWithoutResponse calls got ZERO responses for any of the 6, with no
  // JS exception anywhere, while a lone "status raw" right before/after always
  // got its response immediately and reliably - i.e. not a firmware bug (same
  // command text verified correct over a live Serial connection) and not a
  // thrown/caught error, which matches Android's Web Bluetooth
  // writeValueWithoutResponse sometimes resolving before the write actually
  // reached the radio, silently dropping a write that races the previous one.
  // write-with-response forces a real GATT-level ack per write, which both
  // confirms delivery and naturally paces the sequence - no artificial delay
  // needed between iterations anymore.
  const refreshPreviews = useCallback(async () => {
    for (const slot of SLOT_INDICES) {
      await sendCmdReliable(`profile show ${slot}`).catch((e) => console.warn(e));
    }
  }, [sendCmdReliable]);

  useEffect(() => {
    // Gate on statusReady (set once refreshStatus() has completed), not the
    // raw connected flag - firing this any earlier races the initial status
    // snapshot for the shared BLE notify queue/characteristic and can starve
    // STATUS_END past useBle's wait timeout (see memory: Incomplete status
    // snapshot).
    if (status.connected && status.statusReady) refreshPreviews();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status.connected, status.statusReady]);

  useEffect(() => {
    const next: Record<number, ProfilePreview> = {};
    let changed = false;
    for (const slot of SLOT_INDICES) {
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
    // writeValueWithResponse, not the fire-and-forget sendCmd: on Android,
    // writeValueWithoutResponse can silently drop a write over a flaky link
    // without the JS promise ever rejecting (OTA chunks hit the same risk,
    // see sendCmdReliable's own comment) - for a single, infrequent command
    // like this, trading a bit of latency for an actual GATT-level
    // delivery confirmation is worth it. The UI's selected slot is only
    // updated once that confirmation comes back, so a dropped command
    // shows up as the dropdown not moving instead of silently lying about
    // what's loaded.
    sendCmdReliable(`profile load ${slot}`)
      .then(() => {
        setProfileSlot(String(slot));
        setTimeout(refreshPreviews, 200);
        // The firmware does push an unsolicited status snapshot right
        // after applying a profile (loadProfileSlot's announce path), so
        // this is normally redundant - but that push has no delivery
        // guarantee (one BLE notification among several dropping is not
        // implausible) and nothing here was otherwise verifying it
        // actually arrived. Explicitly re-requesting status is the same
        // mechanism already used on connect, with its own retry/fallback -
        // the one place this matters most is ramp settings (duration +
        // ease type), since those only ever reach the UI through this
        // kind of status push, never through live user interaction with
        // this card.
        refreshStatus().catch((e) => console.warn(e));
      })
      .catch((e) => {
        console.warn(e);
        const msg = e instanceof Error ? e.message : String(e);
        toast.error(t('err.profileLoadFailed', 'Profil konnte nicht geladen werden') + ': ' + msg);
      });
  };

  const handleSaveConfirmed = (slot: number) => {
    setConfirmSlot(null);
    // Same write-with-response reasoning as handleLoad - a dropped "save"
    // would silently discard the user's settings into an unchanged slot.
    sendCmdReliable(`profile save ${slot}`)
      .then(() => setTimeout(refreshPreviews, 200))
      .catch((e) => {
        console.warn(e);
        const msg = e instanceof Error ? e.message : String(e);
        toast.error(t('err.profileSaveFailed', 'Profil konnte nicht gespeichert werden') + ': ' + msg);
      });
  };

  const activeSlot = Number(profileSlot) || 1;

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.profiles">Profile</Trans></CardTitle>
      </CardHeader>
      <CardContent>
        <div className="flex items-center justify-between gap-2 rounded-lg border border-accent bg-accent/10 px-3 py-2">
          <div className="min-w-0 flex-1">
            <div className="text-xs text-muted">
              <Trans k="label.profile">Profile</Trans> {activeSlot}
            </div>
            <div className="truncate text-sm font-medium">{previewText(previews[activeSlot], t)}</div>
          </div>
          <Button
            size="sm"
            variant="ghost"
            title={t('btn.save', 'Save')}
            aria-label={t('btn.save', 'Save')}
            onClick={() => setConfirmSlot(activeSlot)}
          >
            <Save className="h-4 w-4" />
          </Button>
        </div>
        <Select value={profileSlot} onValueChange={(v) => handleLoad(Number(v))}>
          <SelectTrigger>
            <SelectValue placeholder={t('btn.load', 'Load')} />
          </SelectTrigger>
          <SelectContent>
            {SLOT_INDICES.map((slot) => (
              <SelectItem key={slot} value={String(slot)}>
                {t('label.profile', 'Profile')} {slot} - {previewText(previews[slot], t)}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
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
