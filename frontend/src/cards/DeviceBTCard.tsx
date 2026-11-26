import { useState } from 'react';
import { Pencil } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Dialog, DialogContent, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';

export function DeviceBTCard() {
  const { sendCmd, status } = useConnection();
  const [bleName, setBleName] = useState(() => localStorage.getItem('ql-ble-name') || 'Quarzlampe');
  const [editOpen, setEditOpen] = useState(false);
  const [editValue, setEditValue] = useState(bleName);

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
                    className="text-destructive hover:underline"
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
    </Card>
  );
}
