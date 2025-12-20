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
  notifyMin: number;
  setNotifyMin: (v: number) => void;
  handleNotify: (seq: string, fade?: number, repeat?: number) => void;
  sendCmd: (cmd: string) => void;
};

export function NotifyCard({ notifySeq, setNotifySeq, notifyFade, setNotifyFade, notifyRepeat, setNotifyRepeat, notifyMin, setNotifyMin, handleNotify, sendCmd }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.notify">Notify</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-3">
          <Button variant="primary" onClick={() => sendCmd("sos")}>
            <Zap className="mr-1 h-4 w-4" />{" "}
            <Trans k="notify.sosStart">SOS Start</Trans>
          </Button>
          <Button variant="danger" onClick={() => sendCmd("sos stop")}>
            <ZapOff className="mr-1 h-4 w-4" />{" "}
            <Trans k="notify.sosStop">SOS Stop</Trans>
          </Button>
        </div>
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
                />
              </div>
              <div >
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
                    sendCmd(`notify min ${v.toFixed(1)}`);
                  }}
                  className="w-24"
                  suffix="%"
                />
              </div>
            </div>
          </div>
          <div className="flex items-end gap-2">
            <Button
              variant="primary"
              onClick={() => handleNotify(notifySeq, notifyFade, notifyRepeat)}
            >
              <Send className="mr-1 h-4 w-4" />{" "}
              <Trans k="title.notify">Notify</Trans>
            </Button>
            <Button variant="danger" onClick={() => sendCmd("notify stop")}>
              <ZapOff className="mr-1 h-4 w-4" />{" "}
              <Trans k="btn.stop">Stop</Trans>
            </Button>
          </div>
        </div>
        <div className="flex flex-wrap gap-2">
          {[
            {
              key: "notify.preset.short",
              fallback: "Short blink",
              seq: "80 40 80 120",
              fade: 0,
            },
            {
              key: "notify.preset.soft",
              fallback: "Soft alert",
              seq: "200 100",
              fade: 100,
            },
            {
              key: "notify.preset.triple",
              fallback: "Triple pulse",
              seq: "120 80 120 80 120 200",
              fade: 40,
            },
            {
              key: "notify.preset.door",
              fallback: "Doorbell",
              seq: "200 80 200 400",
              fade: 30,
            },
            {
              key: "notify.preset.longfade",
              fallback: "Long Fade Alert",
              seq: "500 300",
              fade: 120,
            },
            {
              key: "notify.preset.double",
              fallback: "Double 60",
              seq: "60 60 60 200",
              fade: 0,
            },
            {
              key: "notify.preset.sos",
              fallback: "SOS",
              seq: "400 400 400 800 800 800 400 400 400 1200",
              fade: 0,
            },
          ].map((p) => (
            <Button key={p.seq} onClick={() => handleNotify(p.seq, p.fade, 1)}>
              <Trans k={p.key}>{p.fallback}</Trans>
            </Button>
          ))}
        </div>
      </CardContent>
    </Card>
  );
}
