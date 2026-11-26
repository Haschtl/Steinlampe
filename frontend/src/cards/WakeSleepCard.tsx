import { useMemo, useState } from 'react';
import { Moon, Sun } from 'lucide-react';
import { ActiveToggleButton } from '@/components/ui/active-toggle-button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Collapsible } from '@/components/ui/collapsible';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

export function WakeSleepCard() {
  const { status, sendCmdReliable, refreshStatus } = useConnection();
  const [wakeDuration, setWakeDuration] = useState(180);
  const [wakeMode, setWakeMode] = useState('');
  const [wakeBri, setWakeBri] = useState('');
  const [wakeSoft, setWakeSoft] = useState(false);
  const [sleepMinutes, setSleepMinutes] = useState(15);
  const [wakePending, setWakePending] = useState(false);
  const [sleepPending, setSleepPending] = useState(false);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: `${i + 1} - ${patternLabel(i + 1)}`,
    }));
  }, [status.patternCount]);

  // write-with-response, then an explicit status refresh: starting/stopping a
  // wake or sleep fade only ever announces itself as a plain feedback line
  // ("[Wake] Starte Fade...") - the firmware never pushes a fresh full STATUS
  // snapshot on its own afterward, so wake_active/sleep_active (added
  // 2026-11-26, see memory: firmware-status-fields-added-2026-11) would
  // otherwise just sit stale in the frontend until something else happened
  // to trigger a refresh. Same explicit-refresh pattern already used after
  // profile load/save.
  const runAndRefresh = (cmd: string, setPending: (v: boolean) => void) => {
    setPending(true);
    sendCmdReliable(cmd)
      .then(() => refreshStatus())
      .catch((e) => console.warn(e))
      .finally(() => setPending(false));
  };

  const handleWake = () => {
    const parts = ['wake'];
    if (wakeSoft) parts.push('soft');
    if (wakeMode.trim()) parts.push(`mode=${wakeMode.trim()}`);
    if (wakeBri.trim()) parts.push(`bri=${wakeBri.trim()}`);
    parts.push(String(Math.max(1, wakeDuration || 1)));
    runAndRefresh(parts.join(' '), setWakePending);
  };

  const wakeActive = status.wakeActive ?? false;
  const sleepActive = status.sleepActive ?? false;

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.wake">Wake</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div>
            <Label><Trans k="label.duration">Duration</Trans> (s)</Label>
            <Input
              type="number"
              min={1}
              value={wakeDuration}
              onChange={(e) => setWakeDuration(Number(e.target.value))}
              suffix="s"
              disabled={wakeActive}
            />
          </div>
          <ActiveToggleButton
            active={wakeActive}
            pending={wakePending}
            onStart={handleWake}
            onStop={() => runAndRefresh('wake stop', setWakePending)}
            startIcon={Sun}
            stopIcon={Sun}
            startLabel={<Trans k="btn.wake">Wake</Trans>}
            stopLabel={<Trans k="btn.stop">Stop</Trans>}
          />
          <Collapsible title={<Trans k="label.moreOptions">More options</Trans>}>
            <div className="grid gap-3 md:grid-cols-2">
              <div>
                <Label><Trans k="label.mode">Mode</Trans></Label>
                <Select value={wakeMode || 'none'} onValueChange={(v) => setWakeMode(v === 'none' ? '' : v)}>
                  <SelectTrigger>
                    <SelectValue placeholder="Unverändert" />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="none">Unverändert</SelectItem>
                    {patternOptions.slice(0, 10).map((p) => (
                      <SelectItem key={p.idx} value={String(p.idx)}>
                        {p.label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <div>
                <Label><Trans k="label.briPct">Brightness (%)</Trans></Label>
                <Input type="number" placeholder="Bri %" value={wakeBri} onChange={(e) => setWakeBri(e.target.value)} suffix="%" />
              </div>
            </div>
            <label className="pill cursor-pointer inline-flex items-center gap-2">
              <input type="checkbox" checked={wakeSoft} onChange={(e) => setWakeSoft(e.target.checked)} /> <Trans k="label.soft">Soft</Trans>
            </label>
          </Collapsible>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.sleep">Sleep</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div>
            <Label><Trans k="label.duration">Duration</Trans> (min)</Label>
            <Input
              type="number"
              min={1}
              value={sleepMinutes}
              onChange={(e) => setSleepMinutes(Number(e.target.value))}
              suffix="min"
              disabled={sleepActive}
            />
          </div>
          <ActiveToggleButton
            active={sleepActive}
            pending={sleepPending}
            onStart={() => runAndRefresh(`sleep ${Math.max(1, sleepMinutes || 1)}`, setSleepPending)}
            onStop={() => runAndRefresh('sleep stop', setSleepPending)}
            startIcon={Moon}
            stopIcon={Moon}
            startLabel={<Trans k="btn.sleep">Sleep</Trans>}
            stopLabel={<Trans k="btn.stop">Stop</Trans>}
          />
        </CardContent>
      </Card>
    </div>
  );
}
