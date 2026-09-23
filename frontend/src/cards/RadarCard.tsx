import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { useEffect, useState } from 'react';

export function RadarCard() {
  const { status, sendCmd } = useConnection();

  const [dimNear, setDimNear] = useState(20);
  const [dimFar, setDimFar] = useState(150);
  const [motionThr, setMotionThr] = useState(5);
  const [motionHold, setMotionHold] = useState(5000);
  const [offCm, setOffCm] = useState(300);
  const [offGrace, setOffGrace] = useState(5000);

  useEffect(() => {
    if (typeof status.radarDimNearCm === 'number') setDimNear(status.radarDimNearCm);
    if (typeof status.radarDimFarCm === 'number') setDimFar(status.radarDimFarCm);
  }, [status.radarDimNearCm, status.radarDimFarCm]);

  useEffect(() => {
    if (typeof status.radarMotionSpeedThr === 'number') setMotionThr(status.radarMotionSpeedThr);
    if (typeof status.radarMotionHoldMs === 'number') setMotionHold(status.radarMotionHoldMs);
  }, [status.radarMotionSpeedThr, status.radarMotionHoldMs]);

  useEffect(() => {
    if (typeof status.radarOffDistanceCm === 'number') setOffCm(status.radarOffDistanceCm);
    if (typeof status.radarOffGraceMs === 'number') setOffGrace(status.radarOffGraceMs);
  }, [status.radarOffDistanceCm, status.radarOffGraceMs]);

  return (
    <Card>
      <CardHeader className="flex items-center justify-between gap-2">
        <CardTitle><Trans k="title.radar">Radar</Trans></CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={status.radarEnabled ?? false}
            onChange={(e) => sendCmd(`radar ${e.target.checked ? 'on' : 'off'}`)}
          />{' '}
          <Trans k="title.radar">Radar</Trans>
        </label>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="space-y-2">
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.radarDimEnabled ?? false}
              onChange={(e) => sendCmd(`radar dim ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="label.radarDimNear">Near distance (cm, full brightness)</Trans> / <Trans k="label.radarDimFar">Far distance (cm, min brightness)</Trans>
          </label>
          <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarDimNear">Near distance (cm, full brightness)</Trans></span>
              <Input
                type="number"
                value={dimNear}
                onChange={(e) => setDimNear(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar dim near ${e.target.value}`)}
              />
            </label>
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarDimFar">Far distance (cm, min brightness)</Trans></span>
              <Input
                type="number"
                value={dimFar}
                onChange={(e) => setDimFar(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar dim far ${e.target.value}`)}
              />
            </label>
          </div>
        </div>

        <div className="space-y-2">
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.radarMotionOnEnabled ?? false}
              onChange={(e) => sendCmd(`radar motion ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="label.radarMotionThr">Motion speed threshold (cm/s)</Trans>
          </label>
          <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarMotionThr">Motion speed threshold (cm/s)</Trans></span>
              <Input
                type="number"
                value={motionThr}
                onChange={(e) => setMotionThr(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar motion thr ${e.target.value}`)}
              />
            </label>
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarMotionHold">Hold time after motion (ms)</Trans></span>
              <Input
                type="number"
                value={motionHold}
                onChange={(e) => setMotionHold(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar motion hold ${e.target.value}`)}
              />
            </label>
          </div>
        </div>

        <div className="space-y-2">
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={status.radarOffDistanceEnabled ?? false}
              onChange={(e) => sendCmd(`radar offdist ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="label.radarOffDistance">Switch-off distance (cm)</Trans>
          </label>
          <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarOffDistance">Switch-off distance (cm)</Trans></span>
              <Input
                type="number"
                value={offCm}
                onChange={(e) => setOffCm(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar offdist cm ${e.target.value}`)}
              />
            </label>
            <label className="flex flex-col gap-1 text-sm">
              <span><Trans k="label.radarOffGrace">Switch-off grace (ms)</Trans></span>
              <Input
                type="number"
                value={offGrace}
                onChange={(e) => setOffGrace(parseInt(e.target.value, 10) || 0)}
                onBlur={(e) => sendCmd(`radar offdist grace ${e.target.value}`)}
              />
            </label>
          </div>
        </div>

        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={status.radarHwOverride ?? false}
            onChange={(e) => sendCmd(`radar hwoverride ${e.target.checked ? 'on' : 'off'}`)}
          />{' '}
          <Trans k="label.hwOverride">Auch gegen Schalter/Poti erzwingen</Trans>
        </label>

        <p className="text-sm text-muted">
          Status: {status.radarPresent ? 'detected' : 'clear'} dist={status.radarDistanceCm ?? '--'}cm speed={status.radarSpeedCmS ?? '--'}cm/s targets={status.radarTargetCount ?? 0}
        </p>
        {!status.radarPresent && <p className="text-xs text-muted"><Trans k="label.radarNone">No target detected</Trans></p>}
      </CardContent>
    </Card>
  );
}
