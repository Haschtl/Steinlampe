import { useMemo, useState } from 'react';
import { Zap, ZapOff, Send, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

type PatternOption = { idx: number; label: string };

type NotifyProps = {
  notifySeq: string;
  setNotifySeq: (v: string) => void;
  notifyFade: number;
  setNotifyFade: (v: number) => void;
  notifyRepeat: number;
  setNotifyRepeat: (v: number) => void;
  handleNotify: (seq: string, fade?: number, repeat?: number) => void;
  sendCmd: (cmd: string) => void;
};

type WakeSleepProps = {
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

function NotifyCard({ notifySeq, setNotifySeq, notifyFade, setNotifyFade, notifyRepeat, setNotifyRepeat, handleNotify, sendCmd }: NotifyProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.notify">Notify</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-3">
          <Button variant="primary" onClick={() => sendCmd('sos')}>
            <Zap className="mr-1 h-4 w-4" /> SOS Start
          </Button>
          <Button variant="danger" onClick={() => sendCmd('sos stop')}>
            <ZapOff className="mr-1 h-4 w-4" /> SOS Stop
          </Button>
        </div>
        <div className="grid gap-2 md:grid-cols-[1fr_auto]">
          <div className="space-y-1">
            <Label><Trans k="label.sequence">Sequence</Trans></Label>
            <Input placeholder="80 40 80 120" value={notifySeq} onChange={(e) => setNotifySeq(e.target.value)} suffix="ms" />
            <div className="flex items-center gap-3">
              <Label className="m-0 text-muted"><Trans k="label.fade">Fade</Trans></Label>
              <Input type="number" min={0} max={500} step={10} value={notifyFade} onChange={(e) => setNotifyFade(Number(e.target.value))} className="w-24" suffix="ms" />
              <Label className="m-0 text-muted"><Trans k="label.repeat">Repeat</Trans></Label>
              <Input type="number" min={1} max={10} step={1} value={notifyRepeat} onChange={(e) => setNotifyRepeat(Number(e.target.value))} className="w-20" />
            </div>
          </div>
          <div className="flex items-end gap-2">
            <Button variant="primary" onClick={() => handleNotify(notifySeq, notifyFade, notifyRepeat)}>
              <Send className="mr-1 h-4 w-4" /> Notify
            </Button>
            <Button variant="danger" onClick={() => sendCmd('notify stop')}>
              <ZapOff className="mr-1 h-4 w-4" /> Stop
            </Button>
          </div>
        </div>
        <div className="flex flex-wrap gap-2">
          {[
            { label: 'Short blink', seq: '80 40 80 120', fade: 0 },
            { label: 'Soft alert', seq: '200 100', fade: 100 },
            { label: 'Triple pulse', seq: '120 80 120 80 120 200', fade: 40 },
            { label: 'Doorbell', seq: '200 80 200 400', fade: 30 },
            { label: 'Long Fade Alert', seq: '500 300', fade: 120 },
            { label: 'Double 60', seq: '60 60 60 200', fade: 0 },
            { label: 'SOS', seq: '400 400 400 800 800 800 400 400 400 1200', fade: 0 },
          ].map((p) => (
            <Button key={p.label} onClick={() => handleNotify(p.seq, p.fade, 1)}>
              {p.label}
            </Button>
          ))}
        </div>
      </CardContent>
    </Card>
  );
}

function WakeSleepCard({ wakeDuration, setWakeDuration, wakeMode, setWakeMode, wakeBri, setWakeBri, wakeSoft, setWakeSoft, handleWake, sleepMinutes, setSleepMinutes, patternOptions, sendCmd }: WakeSleepProps) {
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
              <select className="input" value={wakeMode} onChange={(e) => setWakeMode(e.target.value)}>
                <option value="">Unverändert</option>
                {patternOptions.slice(0, 10).map((p) => (
                  <option key={p.idx} value={p.idx}>
                    {p.label}
                  </option>
                ))}
              </select>
            </div>
          </div>
          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <Label><Trans k="label.briPct">Brightness (%)</Trans></Label>
              <Input type="number" placeholder="Bri %" value={wakeBri} onChange={(e) => setWakeBri(e.target.value)} suffix="%" />
            </div>
            <div className="flex items-center gap-2">
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" checked={wakeSoft} onChange={(e) => setWakeSoft(e.target.checked)} /> Soft
              </label>
              <Button variant="primary" onClick={handleWake}>
                <RefreshCw className="mr-1 h-4 w-4" /> Wake
              </Button>
              <Button variant="danger" onClick={() => sendCmd('wake stop')}>
                <ZapOff className="mr-1 h-4 w-4" /> Stop
              </Button>
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
              <Label>Duration (min)</Label>
              <Input type="number" min={1} value={sleepMinutes} onChange={(e) => setSleepMinutes(Number(e.target.value))} suffix="min" />
            </div>
            <Button variant="primary" onClick={() => sendCmd(`sleep ${Math.max(1, sleepMinutes || 1)}`)}>
              <Zap className="mr-1 h-4 w-4" /> Sleep
            </Button>
            <Button variant="danger" onClick={() => sendCmd('sleep stop')}>
              <ZapOff className="mr-1 h-4 w-4" /> Stop
            </Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

export function ActionsSection() {
  const { status, sendCmd } = useConnection();
  const [notifySeq, setNotifySeq] = useState('80 40 80 120');
  const [notifyFade, setNotifyFade] = useState(0);
  const [notifyRepeat, setNotifyRepeat] = useState(1);
  const [wakeDuration, setWakeDuration] = useState(180);
  const [wakeMode, setWakeMode] = useState('');
  const [wakeBri, setWakeBri] = useState('');
  const [wakeSoft, setWakeSoft] = useState(false);
  const [sleepMinutes, setSleepMinutes] = useState(15);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabels[i] ? `${i + 1} - ${patternLabels[i]}` : `Pattern ${i + 1}`,
    }));
  }, [status.patternCount]);

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

  const handleNotify = (seq: string, fade?: number, repeat = 1) => {
    const cmd = buildNotifyCmd(seq, fade, repeat);
    if (!cmd) return;
    sendCmd(cmd).catch((e) => console.warn(e));
  };

  const handleWake = () => {
    const parts = ['wake'];
    if (wakeSoft) parts.push('soft');
    if (wakeMode.trim()) parts.push(`mode=${wakeMode.trim()}`);
    if (wakeBri.trim()) parts.push(`bri=${wakeBri.trim()}`);
    parts.push(String(Math.max(1, wakeDuration || 1)));
    sendCmd(parts.join(' ')).catch((e) => console.warn(e));
  };

  return (
    <div className="space-y-4">
      <WakeSleepCard
        wakeDuration={wakeDuration}
        setWakeDuration={setWakeDuration}
        wakeMode={wakeMode}
        setWakeMode={setWakeMode}
        wakeBri={wakeBri}
        setWakeBri={setWakeBri}
        wakeSoft={wakeSoft}
        setWakeSoft={setWakeSoft}
        handleWake={handleWake}
        sleepMinutes={sleepMinutes}
        setSleepMinutes={setSleepMinutes}
        patternOptions={patternOptions}
        sendCmd={sendCmd}
      />
      <NotifyCard
        notifySeq={notifySeq}
        setNotifySeq={setNotifySeq}
        notifyFade={notifyFade}
        setNotifyFade={setNotifyFade}
        notifyRepeat={notifyRepeat}
        setNotifyRepeat={setNotifyRepeat}
        handleNotify={handleNotify}
        sendCmd={sendCmd}
      />
    </div>
  );
}
