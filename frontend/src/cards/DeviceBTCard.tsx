import { useEffect, useMemo, useState } from 'react';
import { AlertTriangle, Loader2, Pencil } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';

export function DeviceBTCard() {
  const { sendCmdReliable, status } = useConnection();
  // status.deviceName (the connected GATT device's own advertised name) is the live
  // source of truth - this used to default straight to a hardcoded "Quarzlampe" from
  // localStorage and never synced from the actual device, so it could show a name
  // belonging to a completely different lamp (or just the wrong default) while the
  // page header elsewhere correctly showed the real one. localStorage is now only a
  // fallback for while disconnected/before the first status arrives.
  const [bleName, setBleName] = useState(() => status.deviceName || localStorage.getItem('ql-ble-name') || 'Quarzlampe');
  useEffect(() => {
    if (status.deviceName) setBleName(status.deviceName);
  }, [status.deviceName]);
  const [editOpen, setEditOpen] = useState(false);
  const [editValue, setEditValue] = useState(bleName);
  const [clearBondsOpen, setClearBondsOpen] = useState(false);
  const [reloadingTrust, setReloadingTrust] = useState(false);

  // "trust list" alone already returns the trust list AND the bond list in
  // one go (trustListFeedback() on the firmware prints both) - there was
  // never a need for the separate "trust ble bonds" command/button, which
  // is also why the two sections showed what looked like the same data
  // twice. Merged into one reload + one list below.
  const reloadTrust = () => {
    setReloadingTrust(true);
    sendCmdReliable('trust list').catch(() => undefined).finally(() => setReloadingTrust(false));
  };

  // Trust (app-level authorization) and bonds (the BLE stack's own crypto
  // keys) are different things underneath - see desc.bleBonds below - but
  // in the one list the user actually looks at, showing the union once
  // with a small per-entry note is simpler than two near-duplicate lists.
  const allAddrs = useMemo(() => {
    const set = new Set<string>([...(status.trustedBle || []), ...(status.trustedBleBonds || [])]);
    return Array.from(set);
  }, [status.trustedBle, status.trustedBleBonds]);

  const handleClearBonds = () => {
    setClearBondsOpen(false);
    sendCmdReliable('trust ble clearbonds').catch(() => undefined);
  };

  const applyBleName = () => {
    const next = editValue.trim() || 'Quarzlampe';
    setEditOpen(false);
    // write-with-response, and only update the displayed/cached name once the
    // device actually confirms it - same reasoning as the profile-slot fixes
    // (memory: profile-show-no-response-investigation): a silently-dropped
    // rename must not leave the UI showing a name the lamp never actually took.
    sendCmdReliable(`name ble ${next}`)
      .then(() => {
        setBleName(next);
        localStorage.setItem('ql-ble-name', next);
      })
      .catch(() => undefined);
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.btsettings">Bluetooth</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="space-y-1">
          <div className="text-sm font-medium"><Trans k="label.bleName">BLE Name</Trans></div>
          <div className="flex items-center gap-2">
            <span className="text-sm text-text">{bleName}</span>
            <button
              type="button"
              className="rounded-full p-1 text-muted hover:text-accent"
              onClick={() => {
                setEditValue(bleName);
                setEditOpen(true);
              }}
              title="Edit"
              aria-label="Edit"
            >
              <Pencil className="h-3.5 w-3.5" />
            </button>
          </div>
        </div>
        <div className="space-y-2">
          <div className="flex items-center justify-between">
            <div className="text-sm font-medium"><Trans k="title.trustController">Trusted devices</Trans></div>
            <Button size="sm" variant="ghost" disabled={reloadingTrust} onClick={reloadTrust}>
              {reloadingTrust ? <Loader2 className="mr-1 h-3.5 w-3.5 animate-spin" /> : null}
              <Trans k="btn.reload">Reload</Trans>
            </Button>
          </div>
          <p className="text-xs text-muted">
            <Trans k="desc.bleBonds">
              Devices allowed to send commands. Clear bonds (below) if the OS keeps showing pairing prompts or a
              reconnect fails right after a reflash.
            </Trans>
          </p>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2 space-y-2">
            <div className="flex flex-wrap gap-2">
              {allAddrs.map((addr) => (
                <div key={addr} className="flex items-center gap-1 rounded-full bg-muted/30 px-2 py-1 text-xs">
                  <span className="max-w-[160px] truncate">{addr}</span>
                  <button
                    type="button"
                    className="text-red-400 hover:underline"
                    onClick={() => sendCmdReliable(`trust ble del ${addr}`).catch(() => undefined)}
                    title="Forget"
                  >
                    ×
                  </button>
                </div>
              ))}
              {allAddrs.length === 0 && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
            <Button size="sm" variant="danger" onClick={() => setClearBondsOpen(true)}>
              <Trans k="btn.clearBonds">Clear bonds</Trans>
            </Button>
          </div>
        </div>
      </CardContent>
      <Dialog open={editOpen} onOpenChange={setEditOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle><Trans k="label.bleName">BLE Name</Trans></DialogTitle>
          </DialogHeader>
          <Input
            value={editValue}
            onChange={(e) => setEditValue(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') applyBleName();
            }}
            autoFocus
          />
          <DialogFooter>
            <Button variant="ghost" onClick={() => setEditOpen(false)}>
              <Trans k="btn.cancel">Cancel</Trans>
            </Button>
            <Button variant="primary" onClick={applyBleName}>
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
      <Dialog open={clearBondsOpen} onOpenChange={setClearBondsOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <AlertTriangle className="h-4 w-4 text-amber-400" />
              <Trans k="title.clearBondsConfirm">Clear all BLE bonds?</Trans>
            </DialogTitle>
            <DialogDescription>
              <Trans k="desc.clearBondsConfirm">
                Every paired device (this browser included) will need to pair again - on Linux, also remove the
                lamp from bluetoothctl/the system Bluetooth settings first, or the OS will keep retrying its now
                stale pairing keys.
              </Trans>
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="ghost" onClick={() => setClearBondsOpen(false)}>
              <Trans k="btn.cancel">Cancel</Trans>
            </Button>
            <Button variant="danger" onClick={handleClearBonds}>
              <Trans k="btn.clearBonds">Clear bonds</Trans>
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </Card>
  );
}
