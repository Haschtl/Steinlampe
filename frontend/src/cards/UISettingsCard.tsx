import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { useI18n, Trans } from '@/i18n';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';

export function UISettingsCard() {
  const { autoReconnect, setAutoReconnect, knownDevices, forgetDevice, knownSerials, forgetSerial } = useConnection();
  const { lang, setLang } = useI18n();
  const [theme, setTheme] = useState(() => {
    const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const fallback = prefersDark ? 'fancy' : 'fancy-light';
    return localStorage.getItem('ql-theme') || fallback;
  });

  useEffect(() => {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('ql-theme', theme);
  }, [theme]);

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
                  <span className="max-w-[180px] truncate">{formatSerialLabel(id)}</span>
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
      </CardContent>
    </Card>
  );
}
