import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { useI18n, Trans } from '@/i18n';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';

export function UISettingsCard() {
  const { autoReconnect, setAutoReconnect, knownDevices, forgetDevice, renameDevice, knownSerials, forgetSerial, renameSerial, sendCmd, status } =
    useConnection();
  const { lang, setLang } = useI18n();
  const [theme, setTheme] = useState(() => {
    const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const fallback = prefersDark ? 'fancy' : 'fancy-light';
    return localStorage.getItem('ql-theme') || fallback;
  });
  const [bleName, setBleName] = useState(() => localStorage.getItem('ql-ble-name') || 'Quarzlampe');
  const [btName, setBtName] = useState(() => localStorage.getItem('ql-bt-name') || 'Quarzlampe-SPP');
  const [btSleepBoot, setBtSleepBoot] = useState(0);
  const [btSleepBle, setBtSleepBle] = useState(0);

  useEffect(() => {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('ql-theme', theme);
  }, [theme]);

  useEffect(() => {
    if (typeof status.btSleepBootMs === 'number') setBtSleepBoot(Math.round(status.btSleepBootMs / 600) / 100);
    if (typeof status.btSleepBleMs === 'number') setBtSleepBle(Math.round(status.btSleepBleMs / 600) / 100);
  }, [status.btSleepBootMs, status.btSleepBleMs]);

  const formatSerialLabel = (id: string) => {
    if (!id) return 'Device';
    try {
      const parsed = JSON.parse(id);
      if (parsed && typeof parsed === 'object') {
        if (parsed.bluetoothServiceClassId) return String(parsed.bluetoothServiceClassId);
        if (parsed.usbVendorId && parsed.usbProductId)
          return `USB ${parsed.usbVendorId}:${parsed.usbProductId}`;
        const keys = Object.keys(parsed);
        if (keys.length > 0) return `${keys[0]}=${String((parsed as any)[keys[0]])}`;
      }
    } catch {
      // not JSON, fall back
    }
    if (id.length > 24) return `${id.slice(0, 6)}…${id.slice(-6)}`;
    return id;
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.ui">UI Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={autoReconnect}
            onChange={(e) => setAutoReconnect(e.target.checked)}
          />{' '}
          <Trans k="toggle.autoReconnect">Auto-reconnect</Trans>
        </label>
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted"><Trans k="label.language">Language</Trans></span>
          <button
            type="button"
            className="pill"
            onClick={() => setLang(lang === 'en' ? 'de' : 'en')}
            aria-label={lang === 'en' ? 'Switch to German' : 'Switch to English'}
            title={lang === 'en' ? 'Switch to German' : 'Switch to English'}
          >
            <span className="text-lg" role="img" aria-hidden>
              {lang === 'en' ? '🇬🇧' : '🇩🇪'}
            </span>
          </button>
        </div>
        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="title.btSleep">BT-Serial Sleep</Trans></div>
          <p className="text-xs text-muted">
            <Trans k="desc.btSleep">Turn off Classic BT after a timeout (0 = disabled). BLE stays active.</Trans>
          </p>
          <div className="grid gap-2 sm:grid-cols-[1fr_auto] items-center">
            <div className="space-y-1">
              <label className="text-xs text-muted"><Trans k="label.btSleepBoot">After boot (min)</Trans></label>
              <Input
                type="number"
                min={0}
                step={0.1}
                value={btSleepBoot}
                onChange={(e) => setBtSleepBoot(Number(e.target.value))}
                suffix="min"
              />
            </div>
            <Button
              variant="secondary"
              onClick={() => sendCmd(`bt sleep boot ${btSleepBoot}`).catch(() => undefined)}
            >
              <Trans k="btn.apply">Apply</Trans>
            </Button>
            <div className="space-y-1">
              <label className="text-xs text-muted"><Trans k="label.btSleepBle">After last BLE cmd (min)</Trans></label>
              <Input
                type="number"
                min={0}
                step={0.1}
                value={btSleepBle}
                onChange={(e) => setBtSleepBle(Number(e.target.value))}
                suffix="min"
              />
            </div>
            <Button
              variant="secondary"
              onClick={() => sendCmd(`bt sleep ble ${btSleepBle}`).catch(() => undefined)}
            >
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted"><Trans k="label.theme">Theme</Trans></span>
          <Select value={theme} onValueChange={(v) => setTheme(v)}>
            <SelectTrigger className="w-40">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="dark">Dark</SelectItem>
              <SelectItem value="light">Light</SelectItem>
              <SelectItem value="fancy">Fancy Dark</SelectItem>
              <SelectItem value="fancy-light">Fancy Light</SelectItem>
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="title.devices">Manage Devices</Trans></div>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2">
            <div className="text-xs text-muted mb-1"><Trans k="label.bleDevices">BLE</Trans></div>
            <div className="flex flex-wrap gap-2">
              {Object.entries(knownDevices || {}).map(([id, name]) => (
                <div key={id} className="flex items-center gap-1 rounded-full bg-muted/30 px-2 py-1 text-xs">
                  <span className="max-w-[140px] truncate">{name || 'Device'}</span>
                  <button
                    type="button"
                    className="text-accent hover:underline"
                    onClick={() => {
                      const next = prompt('Rename device', name || id);
                      if (next !== null) renameDevice(id, next);
                    }}
                    title="Rename"
                  >
                    ✎
                  </button>
                  <button
                    type="button"
                    className="text-destructive hover:underline"
                    onClick={() => forgetDevice(id)}
                    title="Forget"
                  >
                    ×
                  </button>
                </div>
              ))}
              {Object.keys(knownDevices || {}).length === 0 && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
          </div>
          <div className="rounded-md border border-border/60 bg-bg/60 p-2">
            <div className="text-xs text-muted mb-1"><Trans k="label.serialDevices">Serial</Trans></div>
            <div className="flex flex-wrap gap-2">
              {Object.entries(knownSerials || {}).map(([id]) => (
                <div key={id} className="flex items-center gap-1 rounded-full bg-muted/30 px-2 py-1 text-xs">
                  <span className="max-w-[180px] truncate">{knownSerials[id] || formatSerialLabel(id)}</span>
                  <button
                    type="button"
                    className="text-accent hover:underline"
                    onClick={() => {
                      const next = prompt('Rename device', knownSerials[id] || formatSerialLabel(id));
                      if (next !== null) renameSerial(id, next);
                    }}
                    title="Rename"
                  >
                    ✎
                  </button>
                  <button
                    type="button"
                    className="text-destructive hover:underline"
                    onClick={() => forgetSerial(id)}
                    title="Forget"
                  >
                    ×
                  </button>
                </div>
              ))}
              {Object.keys(knownSerials || {}).length === 0 && (
                <span className="text-xs text-muted"><Trans k="label.noDevices">No trusted devices yet</Trans></span>
              )}
            </div>
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
