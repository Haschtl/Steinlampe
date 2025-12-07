import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function TouchCard() {
  const { status, sendCmd } = useConnection();
  const [showCalib, setShowCalib] = useState(false);
  const [touchOn, setTouchOn] = useState(10);
  const [touchOff, setTouchOff] = useState(6);
  const [touchHold, setTouchHold] = useState(800);
  const [lastLine, setLastLine] = useState<string | undefined>(undefined);

  useEffect(() => {
    if (typeof status.touchThrOn === 'number') setTouchOn(status.touchThrOn);
    if (typeof status.touchThrOff === 'number') setTouchOff(status.touchThrOff);
    if (typeof status.lastTouchLine === 'string') setLastLine(status.lastTouchLine);
  }, [status.lastTouchLine, status.touchThrOff, status.touchThrOn]);

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle><Trans k="title.touch">Touch</Trans></CardTitle>
        <label className="pill cursor-pointer">
          <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`touchdim ${e.target.checked ? 'on' : 'off'}`)} /> TouchDim
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <Dialog open={showCalib} onOpenChange={setShowCalib}>
            <DialogTrigger asChild>
              <Button><Trans k="btn.calibrate">Calibrate</Trans></Button>
            </DialogTrigger>
            <DialogContent>
              <DialogHeader>
                <DialogTitle><Trans k="touch.calibTitle">Touch calibration</Trans></DialogTitle>
                <DialogDescription><Trans k="touch.calibHint">Choose a calibration mode.</Trans></DialogDescription>
              </DialogHeader>
              <div className="grid gap-3">
                <div className="rounded-lg border border-border bg-panel p-3 space-y-1">
                  <p className="font-semibold text-text"><Trans k="touch.baseline">Baseline Kalibrieren</Trans></p>
                  <p className="text-xs text-muted"><Trans k="touch.baselineHint">Berühren, dann drücken.</Trans></p>
                  <div className="mt-2 flex justify-end">
                    <Button
                      onClick={() => {
                        sendCmd('touch baseline');
                        setShowCalib(false);
                      }}
                    >
                      <Trans k="btn.apply">Apply</Trans>
                    </Button>
                  </div>
                </div>
                <div className="rounded-lg border border-border bg-panel p-3 space-y-1">
                  <p className="font-semibold text-text"><Trans k="touch.fullCalib">Touch Kalibrierung</Trans></p>
                  <p className="text-xs text-muted"><Trans k="touch.fullCalibHint">Drücken und nicht berühren, 2s warten, dann 2s berühren.</Trans></p>
                  <div className="mt-2 flex justify-end">
                    <Button
                      onClick={() => {
                        sendCmd('calibrate touch');
                        setShowCalib(false);
                      }}
                    >
                      <Trans k="btn.apply">Apply</Trans>
                    </Button>
                  </div>
                </div>
              </div>
            </DialogContent>
          </Dialog>
        </div>
        <div className="flex items-center justify-between gap-2 rounded-md border border-border/60 bg-panel/60 px-3 py-2 text-xs text-muted">
          <span className="truncate">
            {lastLine ? lastLine : <Trans k="touch.noDebug">No debug data yet.</Trans>}
          </span>
          <Button size="sm" variant="ghost" onClick={() => sendCmd('touch')}>
            <Trans k="btn.debug">Debug</Trans>
          </Button>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <div className="space-y-1">
            <p className="text-sm text-muted"><Trans k="touch.onThr">Touch On threshold</Trans></p>
            <Input
              type="number"
              min={1}
              max={200}
              step={1}
              value={touchOn}
              onChange={(e) => setTouchOn(Number(e.target.value))}
              onBlur={(e) => sendCmd(`touch tune ${e.target.value} ${touchOff}`)}
              className="w-24"
            />
          </div>
          <div className="space-y-1">
            <p className="text-sm text-muted"><Trans k="touch.offThr">Touch Off threshold</Trans></p>
            <Input
              type="number"
              min={1}
              max={200}
              step={1}
              value={touchOff}
              onChange={(e) => setTouchOff(Number(e.target.value))}
              onBlur={(e) => sendCmd(`touch tune ${touchOn} ${e.target.value}`)}
              className="w-24"
            />
          </div>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <div className="space-y-1">
            <p className="text-sm text-muted"><Trans k="touch.hold">Touch hold duration</Trans></p>
            <Input
              type="number"
              min={300}
              max={5000}
              step={50}
              value={touchHold}
              onChange={(e) => setTouchHold(Number(e.target.value))}
              onBlur={(e) => sendCmd(`touch hold ${e.target.value}`)}
              className="w-28"
              suffix="ms"
            />
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
