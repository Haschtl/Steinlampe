import { useEffect, useState } from 'react';
import { Bell, Send, StopCircle } from 'lucide-react';
import { ActiveToggleButton } from '@/components/ui/active-toggle-button';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Collapsible } from '@/components/ui/collapsible';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

const PRESETS = [
  { key: 'notify.preset.short', fallback: 'Short blink', seq: '80 40 80 120', fade: 0 },
  { key: 'notify.preset.soft', fallback: 'Soft alert', seq: '200 100', fade: 100 },
  { key: 'notify.preset.triple', fallback: 'Triple pulse', seq: '120 80 120 80 120 200', fade: 40 },
  { key: 'notify.preset.door', fallback: 'Doorbell', seq: '200 80 200 400', fade: 30 },
  { key: 'notify.preset.longfade', fallback: 'Long Fade Alert', seq: '500 300', fade: 120 },
  { key: 'notify.preset.double', fallback: 'Double 60', seq: '60 60 60 200', fade: 0 },
];

export function NotifyCard() {
  const { status, sendCmd, sendCmdReliable, refreshStatus } = useConnection();
  const [notifySeq, setNotifySeq] = useState('80 40 80 120');
  const [notifyFade, setNotifyFade] = useState(0);
  const [notifyRepeat, setNotifyRepeat] = useState(1);
  const [notifyMin, setNotifyMin] = useState(10);
  const [pending, setPending] = useState(false);
  const active = status.notifyActive ?? false;

  useEffect(() => {
    if (typeof status.notifyMin === 'number' && !Number.isNaN(status.notifyMin)) {
      setNotifyMin(status.notifyMin);
    }
  }, [status.notifyMin]);

  const buildNotifyCmd = (seq: string, fade?: number, repeat = 1) => {
    const parts = seq
      .split(/\s+/)
      .map((n) => parseInt(n, 10))
      .filter((n) => !Number.isNaN(n) && n > 0);
    if (!parts.length) return '';
    const expanded: number[] = [];
    for (let i = 0; i < repeat; i += 1) expanded.push(...parts);
    let cmd = `notify ${expanded.join(' ')}`;
    if (fade && fade > 0) cmd += ` fade=${fade}`;
    return cmd;
  };

  // write-with-response + explicit refresh - see the identical comment in
  // WakeSleepCard.tsx (memory: firmware-status-fields-added-2026-11).
  const runAndRefresh = (cmd: string) => {
    setPending(true);
    sendCmdReliable(cmd)
      .then(() => refreshStatus())
      .catch((e) => console.warn(e))
      .finally(() => setPending(false));
  };

  const handleNotify = (seq: string, fade?: number, repeat = 1) => {
    const cmd = buildNotifyCmd(seq, fade, repeat);
    if (!cmd) return;
    runAndRefresh(cmd);
  };

  const stop = () => runAndRefresh('notify stop');

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.notify">Notify</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        {active ? (
          <div className="flex items-center justify-between gap-2 rounded-lg border border-accent/40 bg-accent/10 px-3 py-2">
            <div className="flex items-center gap-2 text-sm text-accent">
              <Bell className="h-4 w-4 animate-pulse" />
              <Trans k="label.notifyRunning">Benachrichtigung läuft…</Trans>
            </div>
            <Button size="sm" variant="danger" disabled={pending} onClick={stop}>
              <StopCircle className="mr-1 h-4 w-4" /> <Trans k="btn.stop">Stop</Trans>
            </Button>
          </div>
        ) : (
          <div className="grid grid-cols-2 gap-2 sm:grid-cols-3">
            {PRESETS.map((p) => (
              <Button
                key={p.seq}
                disabled={pending}
                className="h-auto min-h-[2.5rem] whitespace-normal py-2 text-center leading-snug"
                onClick={() => handleNotify(p.seq, p.fade, 1)}
              >
                <Trans k={p.key}>{p.fallback}</Trans>
              </Button>
            ))}
            <Button disabled={pending} className="h-auto min-h-[2.5rem] py-2" onClick={() => runAndRefresh('sos')}>
              <Trans k="notify.preset.sos">SOS</Trans>
            </Button>
          </div>
        )}
        <Collapsible title={<Trans k="label.customSequence">Custom sequence</Trans>}>
          <div className="grid gap-2 md:grid-cols-[1fr_auto]">
            <div className="space-y-1">
              <Label>
                <Trans k="label.sequence">Sequence</Trans>
              </Label>
              <Input
                placeholder="80 40 80 120"
                className="w-full"
                value={notifySeq}
                onChange={(e) => setNotifySeq(e.target.value)}
                suffix="ms"
                disabled={active}
              />
              <div className="flex items-center gap-3 flex-wrap">
                <div>
                  <Label className="m-0 text-muted">
                    <Trans k="label.fade">Fade</Trans>
                  </Label>
                  <Input
                    type="number"
                    min={0}
                    max={500}
                    step={10}
                    value={notifyFade}
                    onChange={(e) => setNotifyFade(Number(e.target.value))}
                    className="w-24"
                    suffix="ms"
                    disabled={active}
                  />
                </div>
                <div>
                  <Label className="m-0 text-muted">
                    <Trans k="label.repeat">Repeat</Trans>
                  </Label>
                  <Input
                    type="number"
                    min={1}
                    max={10}
                    step={1}
                    value={notifyRepeat}
                    onChange={(e) => setNotifyRepeat(Number(e.target.value))}
                    className="w-20"
                    disabled={active}
                  />
                </div>
                <div>
                  <Label className="m-0 text-muted">
                    <Trans k="label.notifyMin">Notify min (%)</Trans>
                  </Label>
                  <Input
                    type="number"
                    min={0}
                    max={100}
                    step={1}
                    value={notifyMin}
                    onChange={(e) => setNotifyMin(Number(e.target.value))}
                    onBlur={(e) => {
                      const v = Math.min(100, Math.max(0, Number(e.target.value)));
                      setNotifyMin(v);
                      sendCmd(`notify min ${v.toFixed(1)}`).catch((e2) => console.warn(e2));
                    }}
                    className="w-24"
                    suffix="%"
                  />
                </div>
              </div>
            </div>
            <div className="flex items-end">
              <ActiveToggleButton
                active={active}
                pending={pending}
                onStart={() => handleNotify(notifySeq, notifyFade, notifyRepeat)}
                onStop={stop}
                startIcon={Send}
                stopIcon={StopCircle}
                startLabel={<Trans k="title.notify">Notify</Trans>}
                stopLabel={<Trans k="btn.stop">Stop</Trans>}
              />
            </div>
          </div>
        </Collapsible>
      </CardContent>
    </Card>
  );
}
