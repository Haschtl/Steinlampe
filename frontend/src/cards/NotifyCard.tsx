import { Zap, ZapOff, Send } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Trans } from '@/i18n';

type Props = {
  notifySeq: string;
  setNotifySeq: (v: string) => void;
  notifyFade: number;
  setNotifyFade: (v: number) => void;
  notifyRepeat: number;
  setNotifyRepeat: (v: number) => void;
  handleNotify: (seq: string, fade?: number, repeat?: number) => void;
  sendCmd: (cmd: string) => void;
};

export function NotifyCard({ notifySeq, setNotifySeq, notifyFade, setNotifyFade, notifyRepeat, setNotifyRepeat, handleNotify, sendCmd }: Props) {
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
