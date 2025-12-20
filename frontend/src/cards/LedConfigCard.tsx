import { useEffect, useMemo, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function LedConfigCard() {
  const { status, sendCmd } = useConnection();
  const [pwmCurve, setPwmCurve] = useState(1.8);
  const [briMin, setBriMin] = useState<number | undefined>(undefined);
  const [briMax, setBriMax] = useState<number | undefined>(undefined);
  const pwmRaw = status.pwmRaw;
  const pwmMax = status.pwmMax ?? 65535;
  // pwmMax reflects DAC range in analog mode too
  const outputMode = status.outputMode ?? "pwm";
  const pwmPct =
    typeof pwmRaw === "number" && typeof pwmMax === "number" && pwmMax > 0
      ? (pwmRaw / pwmMax) * 100
      : undefined;

  useEffect(() => {
    if (typeof status.pwmCurve === "number") setPwmCurve(status.pwmCurve);
    if (typeof status.briMin === "number") setBriMin(Math.round(status.briMin));
    if (typeof status.briMax === "number") setBriMax(Math.round(status.briMax));
  }, [status.briMax, status.briMin, status.pwmCurve]);

  const handlePwm = (val: number) => {
    setPwmCurve(val);
    if (!Number.isNaN(val))
      sendCmd(`pwm curve ${val.toFixed(2)}`).catch((e) => console.warn(e));
  };

  const handleBriMin = () => {
    if (typeof briMin !== "number" || Number.isNaN(briMin)) return;
    const clamped = Math.min(100, Math.max(0, briMin));
    setBriMin(clamped);
    sendCmd(`bri min ${(clamped / 100).toFixed(3)}`).catch((e) => console.warn(e));
  };

  const handleBriMax = () => {
    if (typeof briMax !== "number" || Number.isNaN(briMax)) return;
    const clamped = Math.min(100, Math.max(0, briMax));
    setBriMax(clamped);
    sendCmd(`bri max ${(clamped / 100).toFixed(3)}`).catch((e) => console.warn(e));
  };

  const safeGamma = useMemo(() => {
    const g = Number.isFinite(pwmCurve) ? pwmCurve : 1;
    return Math.min(4, Math.max(0.5, g));
  }, [pwmCurve]);

  const curvePath = useMemo(() => {
    const steps = 32;
    const pad = 12;
    const w = 240;
    const h = 140;
    const toSvg = (x: number, y: number) =>
      `${pad + x * (w - 2 * pad)} ${pad + (1 - y) * (h - 2 * pad)}`;
    let path = "";
    for (let i = 0; i <= steps; i++) {
      const x = i / steps;
      const y = Math.pow(x, safeGamma);
      path += (i === 0 ? "M " : " L ") + toSvg(x, y);
    }
    return { path, width: w, height: h, pad };
  }, [safeGamma]);

  const pwmRatio =
    typeof pwmRaw === "number" && typeof pwmMax === "number" && pwmMax > 0
      ? Math.min(1, Math.max(0, pwmRaw / pwmMax))
      : undefined;
  const pwmInput = useMemo(() => {
    if (typeof pwmRatio !== "number") return undefined;
    return Math.pow(pwmRatio, 1 / safeGamma);
  }, [pwmRatio, safeGamma]);

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.ledConfig">Led configuration</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div>
          <Label>
            <Trans k="label.briMin">Min brightness after gamma (%)</Trans>
          </Label>
          <p className="text-xs text-muted-foreground mt-1">
            <Trans k="desc.briMin">
              Sets the low end of the post-gamma range; output 0% maps here.
            </Trans>
          </p>
          <div className="flex items-center gap-2 mt-1">
            <Input
              type="number"
              min={0}
              max={100}
              value={typeof briMin === "number" ? briMin : ""}
              onChange={(e) =>
                setBriMin(e.target.value === "" ? undefined : Number(e.target.value))
              }
              className="w-28"
              suffix="%"
            />
            <Button onClick={handleBriMin}>
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </div>
        </div>
        <div>
          <Label>
            <Trans k="label.briMax">Max brightness after gamma (%)</Trans>
          </Label>
          <p className="text-xs text-muted-foreground mt-1">
            <Trans k="desc.briMax">
              Sets the high end of the post-gamma range; output 100% maps here.
            </Trans>
          </p>
          <div className="flex items-center gap-2 mt-1">
            <Input
              type="number"
              min={0}
              max={100}
              value={typeof briMax === "number" ? briMax : ""}
              onChange={(e) =>
                setBriMax(e.target.value === "" ? undefined : Number(e.target.value))
              }
              className="w-28"
              suffix="%"
            />
            <Button onClick={handleBriMax}>
              <Trans k="btn.apply">Apply</Trans>
            </Button>
          </div>
        </div>
        <div>
          <Label>
            <Trans k="label.pwm">PWM Curve</Trans>
          </Label>
          <div className="flex items-center gap-2">
            <Input
              type="number"
              min={0.5}
              max={4}
              step={0.1}
              value={pwmCurve}
              onChange={(e) => setPwmCurve(Number(e.target.value))}
              onBlur={(e) => handlePwm(Number(e.target.value))}
              className="w-28"
              suffix="γ"
            />
          </div>
          <p className="text-xs text-muted-foreground mt-1">
            <Trans k="desc.pwm">
              Gamma to linearize LED brightness (0.5–4). Pick a simple step
              pattern (e.g. “Stufen”) and tweak until each step looks equally
              bright.
            </Trans>
          </p>
        </div>
        <div className="flex items-center justify-between text-sm text-muted-foreground">
          <div>
            <Label>
              <Trans k="label.outputMode">Output mode</Trans>
            </Label>
            <div className="mt-1">
              {outputMode === "analog" ? "Analog (DAC)" : "PWM"}
            </div>
          </div>
        </div>
        <div>
          <Label>
            <Trans k="label.pwmRaw">Output raw</Trans>
          </Label>
          <div className="text-sm text-muted-foreground">
            {typeof pwmRaw === "number" ? (
              <>
                {pwmRaw}/{pwmMax}{" "}
                {typeof pwmPct === "number" ? `(${pwmPct.toFixed(1)}%)` : null}
              </>
            ) : (
              <>-</>
            )}
          </div>
        </div>
        <div>
          <Label>
            <Trans k="label.gammaCurve">Gamma curve</Trans>
          </Label>
          <p className="text-xs text-muted-foreground mb-2">
            <Trans k="desc.gammaCurve">
              Shows how brightness maps to PWM with the current gamma. Dot =
              live PWM.
            </Trans>
          </p>
          <div className="rounded-lg border border-border bg-gradient-to-br from-background to-muted p-3 overflow-hidden">
            <svg
              viewBox={`0 0 ${curvePath.width} ${curvePath.height}`}
              role="img"
              aria-label="Gamma curve"
              className="w-full max-w-md block"
            >
              <defs>
                <linearGradient id="gammaFill" x1="0" x2="0" y1="0" y2="1">
                  <stop
                    offset="0%"
                    stopColor="var(--accent-color)"
                    stopOpacity="0.15"
                  />
                  <stop
                    offset="100%"
                    stopColor="var(--accent-color)"
                    stopOpacity="0"
                  />
                </linearGradient>
              </defs>
              <rect
                x={curvePath.pad}
                y={curvePath.pad}
                width={curvePath.width - 2 * curvePath.pad}
                height={curvePath.height - 2 * curvePath.pad}
                rx="6"
                ry="6"
                fill="url(#gammaFill)"
                stroke="rgba(255,255,255,0.12)"
                strokeWidth="1"
              />
              <path
                d={curvePath.path}
                fill="none"
                stroke="var(--accent-color)"
                strokeWidth="2"
              />
              {typeof pwmRatio === "number" && typeof pwmInput === "number" && (
                <>
                  <line
                    x1={
                      curvePath.pad +
                      pwmInput * (curvePath.width - 2 * curvePath.pad)
                    }
                    x2={
                      curvePath.pad +
                      pwmInput * (curvePath.width - 2 * curvePath.pad)
                    }
                    y1={curvePath.pad}
                    y2={curvePath.height - curvePath.pad}
                    stroke="var(--accent-color)"
                    strokeDasharray="4 4"
                    strokeOpacity="0.4"
                  />
                  <line
                    x1={curvePath.pad}
                    x2={curvePath.width - curvePath.pad}
                    y1={
                      curvePath.pad +
                      (1 - pwmRatio) * (curvePath.height - 2 * curvePath.pad)
                    }
                    y2={
                      curvePath.pad +
                      (1 - pwmRatio) * (curvePath.height - 2 * curvePath.pad)
                    }
                    stroke="var(--accent-color)"
                    strokeDasharray="4 4"
                    strokeOpacity="0.4"
                  />
                  <circle
                    cx={
                      curvePath.pad +
                      pwmInput * (curvePath.width - 2 * curvePath.pad)
                    }
                    cy={
                      curvePath.pad +
                      (1 - pwmRatio) * (curvePath.height - 2 * curvePath.pad)
                    }
                    r="5"
                    fill="var(--accent-color)"
                    stroke="white"
                    strokeWidth="2"
                  />
                </>
              )}
              <text
                x={curvePath.width - curvePath.pad}
                y={curvePath.height - 2}
                textAnchor="end"
                fontSize="10"
                fill="rgba(148,163,184,0.9)"
              >
                in →
              </text>
              <text
                x={2}
                y={curvePath.pad}
                textAnchor="start"
                fontSize="10"
                fill="rgba(148,163,184,0.9)"
              >
                ↑ out
              </text>
            </svg>
            {typeof pwmRatio === "number" && (
              <div className="mt-2 text-xs text-muted-foreground flex flex-wrap gap-3">
                <span>γ={safeGamma.toFixed(2)}</span>
                <span>
                  PWM/Analog: {(pwmRatio * 100).toFixed(1)}% ({pwmRaw}/{pwmMax})
                </span>
                {typeof pwmInput === "number" && (
                  <span>equiv input: {(pwmInput * 100).toFixed(1)}%</span>
                )}
              </div>
            )}
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
