import { RefreshCw, Zap, ZapOff } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Trans } from '@/i18n';

type PatternOption = { idx: number; label: string };

type Props = {
  wakeDuration: number;
  setWakeDuration: (v: number) => void;
  wakeMode: string;
  setWakeMode: (v: string) => void;
  wakeBri: string;
  setWakeBri: (v: string) => void;
  wakeSoft: boolean;
  setWakeSoft: (v: boolean) => void;
  handleWake: () => void;
  sleepMinutes: number;
  setSleepMinutes: (v: number) => void;
  patternOptions: PatternOption[];
  sendCmd: (cmd: string) => void;
};

export function WakeSleepCard({
  wakeDuration,
  setWakeDuration,
  wakeMode,
  setWakeMode,
  wakeBri,
  setWakeBri,
  wakeSoft,
  setWakeSoft,
  handleWake,
  sleepMinutes,
  setSleepMinutes,
  patternOptions,
  sendCmd,
}: Props) {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.wake">Wake</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <Label><Trans k="label.duration">Duration</Trans> (s)</Label>
              <Input type="number" min={1} value={wakeDuration} onChange={(e) => setWakeDuration(Number(e.target.value))} suffix="s" />
            </div>
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
          </div>
          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <Label><Trans k="label.briPct">Brightness (%)</Trans></Label>
              <Input type="number" placeholder="Bri %" value={wakeBri} onChange={(e) => setWakeBri(e.target.value)} suffix="%" />
            </div>
            <div className="space-y-2">
              <label className="pill cursor-pointer inline-flex items-center gap-2">
                <input type="checkbox" className="accent-accent" checked={wakeSoft} onChange={(e) => setWakeSoft(e.target.checked)} /> <Trans k="label.soft">Soft</Trans>
              </label>
              <div className="flex items-center gap-2">
                <Button variant="primary" onClick={handleWake}>
                  <RefreshCw className="mr-1 h-4 w-4" /> <Trans k="btn.wake">Wake</Trans>
                </Button>
                <Button variant="danger" onClick={() => sendCmd('wake stop')}>
                  <ZapOff className="mr-1 h-4 w-4" /> <Trans k="btn.stop">Stop</Trans>
                </Button>
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.sleep">Sleep</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="grid gap-3 md:grid-cols-[1fr_auto_auto]">
            <div>
              <Label><Trans k="label.duration">Duration</Trans> (min)</Label>
              <Input type="number" min={1} value={sleepMinutes} onChange={(e) => setSleepMinutes(Number(e.target.value))} suffix="min" />
            </div>
            <Button variant="primary" onClick={() => sendCmd(`sleep ${Math.max(1, sleepMinutes || 1)}`)}>
              <Zap className="mr-1 h-4 w-4" /> <Trans k="btn.sleep">Sleep</Trans>
            </Button>
            <Button variant="danger" onClick={() => sendCmd('sleep stop')}>
              <ZapOff className="mr-1 h-4 w-4" /> <Trans k="btn.stop">Stop</Trans>
            </Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
