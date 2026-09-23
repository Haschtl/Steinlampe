import { MinusCircle, Shield, ToggleLeft } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';
import { useEffect, useMemo, useState } from 'react';

export function PresenceBleCard() {
  const { status, sendCmd } = useConnection();
  const [addr, setAddr] = useState('');
  const [threshold, setThreshold] = useState(-80);
  const [grace, setGrace] = useState(3000);
  const { t } = useI18n();

  useEffect(() => {
    if (typeof status.presenceBleThreshold === 'number') setThreshold(status.presenceBleThreshold);
    if (typeof status.presenceBleGraceMs === 'number') setGrace(status.presenceBleGraceMs);
  }, [status.presenceBleThreshold, status.presenceBleGraceMs]);

  const devices = useMemo(() => status.presenceBleList ?? [], [status.presenceBleList]);

  return (
    <Card>
      <CardHeader className="flex items-center justify-between gap-2">
        <CardTitle><Trans k="title.presenceBle">Presence (BLE)</Trans></CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={status.presenceBle?.toUpperCase().startsWith('ON') ?? false}
            onChange={(e) => sendCmd(`presence_ble ${e.target.checked ? 'on' : 'off'}`)}
          />{' '}
          <Trans k="title.presenceBle">Presence (BLE)</Trans>
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <Input
            placeholder={t('label.presenceBleAddr', 'Presence MAC')}
            value={addr}
            onChange={(e) => setAddr(e.target.value)}
            className="w-48"
          />
          <Button onClick={() => sendCmd(`presence_ble add ${addr || 'me'}`)}>
            <Shield className="mr-1 h-4 w-4" /> <Trans k="btn.save">Add</Trans>
          </Button>
          <Button variant="secondary" disabled={!status.connected} onClick={() => sendCmd('presence_ble add me')}>
            Add connected
          </Button>
          <Button variant="ghost" onClick={() => sendCmd('presence_ble clear')}>
            <Trans k="btn.clear">Clear</Trans>
          </Button>
        </div>
        {devices.length > 0 ? (
          <div className="flex flex-wrap gap-2">
            {devices.map((d) => (
              <span key={d} className="chip-muted flex items-center gap-1">
                {d}
                <button className="text-muted hover:text-foreground" onClick={() => sendCmd(`presence_ble del ${d}`)}>
                  <MinusCircle className="h-3 w-3" />
                </button>
              </span>
            ))}
          </div>
        ) : (
          <p className="text-xs text-muted"><Trans k="label.presenceBleNone">No devices</Trans></p>
        )}
        <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
          <label className="flex flex-col gap-1 text-sm">
            <span className="flex items-center gap-2">
              <ToggleLeft className="h-4 w-4" /> RSSI Schwelle (dBm)
            </span>
            <Input
              type="number"
              value={threshold}
              onChange={(e) => setThreshold(parseInt(e.target.value, 10) || -80)}
              onBlur={(e) => sendCmd(`presence_ble thr ${e.target.value}`)}
            />
          </label>
          <label className="flex flex-col gap-1 text-sm">
            <span><Trans k="label.presenceBleGrace">Timeout nach Last-Leave (ms)</Trans></span>
            <Input
              type="number"
              value={grace}
              onChange={(e) => setGrace(parseInt(e.target.value, 10) || 0)}
              onBlur={(e) => sendCmd(`presence_ble grace ${e.target.value}`)}
            />
          </label>
        </div>
        <div className="flex gap-4 flex-wrap">
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.presenceBleAutoOn ?? true}
              onChange={(e) => sendCmd(`presence_ble auto on ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            Auto-ON (wenn Presence ausgeschaltet hat)
          </label>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.presenceBleAutoOff ?? true}
              onChange={(e) => sendCmd(`presence_ble auto off ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            Auto-OFF (Last leave)
          </label>
        </div>
        <p className="text-sm text-muted">Status: {status.presenceBle ?? '---'} ({status.presenceBleCount ?? 0} devices)</p>
        <p className="text-xs text-muted">Switch: {status.switchState ?? '--'}</p>
      </CardContent>
    </Card>
  );
}
