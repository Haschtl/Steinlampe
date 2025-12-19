import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { SliderRow } from '@/components/ui/slider-row';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function PotiCard() {
  const { status, sendCmd } = useConnection();
  const [enabled, setEnabled] = useState(true);
  const [alpha, setAlpha] = useState(0.25);
  const [delta, setDelta] = useState(0.025);
  const [off, setOff] = useState(0.02);
  const [sample, setSample] = useState(80);
  const [history, setHistory] = useState<number[]>([]);

  useEffect(() => {
    if (typeof status.potiEnabled === 'boolean') setEnabled(status.potiEnabled);
    if (typeof status.potiAlpha === 'number') setAlpha(status.potiAlpha);
    if (typeof status.potiDelta === 'number') setDelta(status.potiDelta);
    if (typeof status.potiOff === 'number') setOff(status.potiOff);
    if (typeof status.potiSample === 'number') setSample(status.potiSample);
  }, [status.potiAlpha, status.potiDelta, status.potiEnabled, status.potiOff, status.potiSample]);

  useEffect(() => {
    if (typeof status.potiVal === 'number') {
      const v = Math.min(1, Math.max(0, status.potiVal));
      setHistory((prev) => {
        const next = [...prev, v];
        return next.slice(-120); // keep last ~120 samples
      });
    }
  }, [status.potiVal]);

  const apply = (cmd: string) => sendCmd(cmd).catch((err) => console.warn(err));

  const lastVal = history.length > 0 ? history[history.length - 1] : typeof status.potiVal === 'number' ? status.potiVal : undefined;
  const lastRaw = typeof status.potiRaw === 'number' ? status.potiRaw : undefined;
  const spark = (() => {
    if (history.length === 0) return null;
    const w = 280;
    const h = 90;
    const pad = 10;
    const span = Math.max(1, history.length - 1);
    const pts = history
      .map((v, i) => {
        const x = pad + (i / span) * (w - 2 * pad);
        const y = pad + (1 - v) * (h - 2 * pad);
        return `${x},${y}`;
      })
      .join(' ');
    const offY = off !== undefined ? pad + (1 - Math.min(1, Math.max(0, off))) * (h - 2 * pad) : null;
    const midY = pad + 0.5 * (h - 2 * pad);
    return { w, h, pad, pts, offY, midY };
  })();

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle>
          <Trans k="title.poti">Potentiometer</Trans>
        </CardTitle>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={enabled}
            onChange={(e) => {
              const next = e.target.checked;
              setEnabled(next);
              apply(`poti ${next ? 'on' : 'off'}`);
            }}
          />{' '}
          <Trans k="label.enabled">Enabled</Trans>
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <SliderRow
          label={<Trans k="label.alpha">Alpha</Trans>}
          description={<Trans k="desc.potiAlpha">Filter strength</Trans>}
          inputProps={{
            min: 0.01,
            max: 1,
            step: 0.01,
            value: alpha,
          }}
          onInputChange={(val) => {
            setAlpha(val);
            if (!Number.isNaN(val)) apply(`poti alpha ${val.toFixed(2)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.delta">Delta</Trans>}
          description={<Trans k="desc.potiDelta">Min. change before updating</Trans>}
          inputProps={{
            min: 0.001,
            max: 0.3,
            step: 0.001,
            value: delta,
          }}
          onInputChange={(val) => {
            setDelta(val);
            if (!Number.isNaN(val)) apply(`poti delta ${val.toFixed(3)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.offThresh">Off threshold</Trans>}
          description={<Trans k="desc.potiOff">Below this the lamp switches off</Trans>}
          inputProps={{
            min: 0,
            max: 0.4,
            step: 0.005,
            value: off,
          }}
          onInputChange={(val) => {
            setOff(val);
            if (!Number.isNaN(val)) apply(`poti off ${val.toFixed(3)}`);
          }}
          disabled={!enabled}
        />
        <SliderRow
          label={<Trans k="label.sampleMs">Sample (ms)</Trans>}
          description={<Trans k="desc.potiSample">Read interval</Trans>}
          inputProps={{
            min: 10,
            max: 2000,
            step: 10,
            value: sample,
          }}
          onInputChange={(val) => {
            setSample(val);
            if (!Number.isNaN(val)) apply(`poti sample ${Math.round(val)}`);
          }}
          disabled={!enabled}
        />
        <div>
          <div className="flex items-center justify-between">
            <div>
              <div className="text-sm font-medium">
                <Trans k="label.potiVal">Live value</Trans>
              </div>
              <div className="text-xs text-muted-foreground">
                <Trans k="desc.potiVal">Filtered knob position (0..1)</Trans>
              </div>
            </div>
            <div className="text-sm text-muted-foreground">
              {typeof lastVal === 'number' ? `${(lastVal * 100).toFixed(1)}%` : '--'}{' '}
              {typeof lastRaw === 'number' ? `(raw ${lastRaw})` : ''}
            </div>
          </div>
          <div className="mt-2 rounded-lg border border-border bg-panel p-3">
            {spark ? (
              <svg viewBox={`0 0 ${spark.w} ${spark.h}`} className="w-full" role="img" aria-label="Poti value">
                <rect
                  x={spark.pad}
                  y={spark.pad}
                  width={spark.w - 2 * spark.pad}
                  height={spark.h - 2 * spark.pad}
                  rx="6"
                  ry="6"
                  fill="rgba(255,255,255,0.02)"
                  stroke="rgba(255,255,255,0.08)"
                />
                <line
                  x1={spark.pad}
                  x2={spark.w - spark.pad}
                  y1={spark.midY}
                  y2={spark.midY}
                  stroke="rgba(255,255,255,0.08)"
                  strokeDasharray="4 4"
                />
                {typeof spark.offY === 'number' && (
                  <line
                    x1={spark.pad}
                    x2={spark.w - spark.pad}
                    y1={spark.offY}
                    y2={spark.offY}
                    stroke="var(--accent-color)"
                    strokeDasharray="6 4"
                    strokeOpacity="0.4"
                  />
                )}
                <polyline
                  points={spark.pts}
                  fill="none"
                  stroke="var(--accent-color)"
                  strokeWidth="2"
                  strokeLinejoin="round"
                  strokeLinecap="round"
                />
              </svg>
            ) : (
              <div className="text-xs text-muted">Waiting for data…</div>
            )}
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
