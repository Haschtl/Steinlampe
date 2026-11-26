import { useState } from 'react';
import { AlertTriangle, Pencil } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';

export function DeviceBTCard() {
  const { sendCmd, status } = useConnection();
  const [bleName, setBleName] = useState(() => localStorage.getItem('ql-ble-name') || 'Quarzlampe');
  const [editOpen, setEditOpen] = useState(false);
  const [editValue, setEditValue] = useState(bleName);
  const [clearBondsOpen, setClearBondsOpen] = useState(false);

  const handleClearBonds = () => {
    setClearBondsOpen(false);
    sendCmd('trust ble clearbonds').catch(() => undefined);
  };

  const applyBleName = () => {
    const next = editValue.trim() || 'Quarzlampe';
    setBleName(next);
    localStorage.setItem('ql-ble-name', next);
    sendCmd(`name ble ${next}`).catch(() => undefined);
    setEditOpen(false);
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
            <div className="text-sm font-medium"><Trans k="title.trustController">Controller trusted devices</Trans></div>
            <Button size="sm" variant="ghost" onClick={() => sendCmd('trust list').catch(() => undefined)}>
              <Trans k="btn.reload">Reload</Trans>
            </Button>
          </div>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2 space-y-2">
            <div className="flex flex-wrap gap-2">
              {(status.trustedBle || []).map((addr) => (
                <div key={addr} className="flex items-center gap-1 rounded-full bg-muted/30 px-2 py-1 text-xs">
                  <span className="max-w-[160px] truncate">{addr}</span>
                  <button
                    type="button"
                    className="text-red-400 hover:underline"
                    onClick={() => sendCmd(`trust ble del ${addr}`).catch(() => undefined)}
                    title="Forget"
                  >
                    ×
                  </button>
                </div>
              ))}
              {(!status.trustedBle || status.trustedBle.length === 0) && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
          </div>
        </div>
        <div className="space-y-2">
          <div className="flex items-center justify-between">
            <div className="text-sm font-medium"><Trans k="title.bleBonds">BLE bonds</Trans></div>
            <Button size="sm" variant="ghost" onClick={() => sendCmd('trust ble bonds').catch(() => undefined)}>
              <Trans k="btn.reload">Reload</Trans>
            </Button>
          </div>
          <p className="text-xs text-muted">
            <Trans k="desc.bleBonds">
              The actual pairing/encryption keys (separate from the trust list above). Clear these if the OS keeps
              showing pairing prompts or a reconnect fails right after a reflash.
            </Trans>
          </p>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2 space-y-2">
            <div className="flex flex-wrap gap-2">
              {(status.trustedBleBonds || []).map((addr) => (
                <span key={addr} className="rounded-full bg-muted/30 px-2 py-1 text-xs">
                  {addr}
                </span>
              ))}
              {(!status.trustedBleBonds || status.trustedBleBonds.length === 0) && (
                <span className="text-xs text-muted"><Trans k="label.noBonds">No bonds stored</Trans></span>
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
