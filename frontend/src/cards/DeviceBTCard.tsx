import {  useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';

export function DeviceBTCard() {
  const { sendCmd } =
    useConnection();
  const [bleName, setBleName] = useState(() => localStorage.getItem('ql-ble-name') || 'Quarzlampe');
  const [btName, setBtName] = useState(() => localStorage.getItem('ql-bt-name') || 'Quarzlampe-SPP');

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.btsettings">BT Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="space-y-2">
          <div className="flex items-center justify-between">
            <div className="text-sm font-medium"><Trans k="title.trustController">Controller trusted devices</Trans></div>
            <Button size="sm" variant="ghost" onClick={() => sendCmd('trust list').catch(() => undefined)}>
              <Trans k="btn.reload">Reload</Trans>
            </Button>
          </div>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2 space-y-2">
            <div className="text-xs text-muted mb-1"><Trans k="label.bleDevices">BLE</Trans></div>
            <div className="flex flex-wrap gap-2">
              {(useConnection().status.trustedBle || []).map((addr) => (
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
              {(!useConnection().status.trustedBle || useConnection().status.trustedBle?.length === 0) && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
            <div className="text-xs text-muted mb-1"><Trans k="label.serialDevices">Serial</Trans></div>
            <div className="flex flex-wrap gap-2">
              {(useConnection().status.trustedBt || []).map((addr) => (
                <div key={addr} className="flex items-center gap-1 rounded-full bg-muted/30 px-2 py-1 text-xs">
                  <span className="max-w-[160px] truncate">{addr}</span>
                  <button
                    type="button"
                    className="text-destructive hover:underline"
                    onClick={() => sendCmd(`trust bt del ${addr}`).catch(() => undefined)}
                    title="Forget"
                  >
                    ×
                  </button>
                </div>
              ))}
              {(!useConnection().status.trustedBt || useConnection().status.trustedBt?.length === 0) && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
          </div>
        </div>
        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="title.names">Lamp Names</Trans></div>
          <div className="grid gap-2 sm:grid-cols-[1fr_auto] items-center">
            <div className="space-y-1">
              <label className="text-xs text-muted"><Trans k="label.bleName">BLE Name</Trans></label>
              <Input value={bleName} onChange={(e) => setBleName(e.target.value)} />
            </div>
            <Button
              variant="secondary"
              onClick={() => {
                localStorage.setItem('ql-ble-name', bleName);
                sendCmd(`name ble ${bleName}`).catch(() => undefined);
              }}
            >
              <Trans k="btn.apply">Apply</Trans>
            </Button>
            <div className="space-y-1">
              <label className="text-xs text-muted"><Trans k="label.btName">BT Name</Trans></label>
              <Input value={btName} onChange={(e) => setBtName(e.target.value)} />
            </div>
            <Button
              variant="secondary"
              onClick={() => {
                localStorage.setItem('ql-bt-name', btName);
                sendCmd(`name bt ${btName}`).catch(() => undefined);
              }}
            >
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
