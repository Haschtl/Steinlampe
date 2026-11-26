import { useMemo } from 'react';
import { applyPatternAdjustments, getPatternBrightness } from '@/lib/patternSim';

const WINDOW_MS = 6000;
const STEP_MS = 40;

type Props = {
  pattern: number;
  speed: number;
  marginLow: number;
  marginHigh: number;
  invert: boolean;
  fade: number;
};

export function PatternPreviewGraph({ pattern, speed, marginLow, marginHigh, invert, fade }: Props) {
  const { rawPath, fadedPath, w, h, pad, usableW, usableH, marginLowY, marginHighY } = useMemo(() => {
    const w = 320;
    const h = 120;
    const pad = 14;
    const usableW = w - pad * 2;
    const usableH = h - pad * 2;
    const steps = Math.round(WINDOW_MS / STEP_MS);

    const toY = (v: number) => pad + (1 - Math.min(1, Math.max(0, v))) * usableH;
    const toX = (i: number) => pad + (i / steps) * usableW;

    let faded = 0;
    const rawPts: string[] = [];
    const fadedPts: string[] = [];
    for (let i = 0; i <= steps; i += 1) {
      const simMs = i * STEP_MS * speed;
      const raw = getPatternBrightness(pattern, simMs);
      const adjusted = applyPatternAdjustments(raw, { invert, marginLow, marginHigh });
      if (fade > 0) {
        const alpha = Math.min(1, STEP_MS / (400 * fade));
        faded += (adjusted - faded) * alpha;
      } else {
        faded = adjusted;
      }
      const x = toX(i);
      rawPts.push(`${x.toFixed(1)} ${toY(adjusted).toFixed(1)}`);
      fadedPts.push(`${x.toFixed(1)} ${toY(faded).toFixed(1)}`);
    }
    return {
      rawPath: `M ${rawPts.join(' L ')}`,
      fadedPath: `M ${fadedPts.join(' L ')}`,
      w,
      h,
      pad,
      usableW,
      usableH,
      marginLowY: toY(marginLow),
      marginHighY: toY(marginHigh),
    };
  }, [pattern, speed, marginLow, marginHigh, invert, fade]);

  return (
    <div className="relative overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner">
      <div className="relative w-full" style={{ paddingTop: '52%' }}>
        <svg viewBox={`0 0 ${w} ${h}`} className="absolute inset-0 h-full w-full">
          <defs>
            <linearGradient id="patPreviewGrad" x1="0%" x2="100%" y1="0%" y2="0%">
              <stop offset="0%" stopColor="#5be6ff" stopOpacity="0.3" />
              <stop offset="100%" stopColor="#5be6ff" stopOpacity="0.9" />
            </linearGradient>
            <pattern id="patPreviewGrid" width="24" height="24" patternUnits="userSpaceOnUse">
              <path d="M24 0H0V24" fill="none" stroke="rgba(255,255,255,0.05)" strokeWidth="1" />
            </pattern>
          </defs>
          <rect x="0" y="0" width={w} height={h} fill="url(#patPreviewGrid)" />
          <rect x={pad} y={pad} width={usableW} height={usableH} rx="10" fill="rgba(255,255,255,0.02)" />
          {(marginLow > 0 || marginHigh < 1) && (
            <>
              <line x1={pad} x2={pad + usableW} y1={marginLowY} y2={marginLowY} stroke="rgba(255,255,255,0.25)" strokeDasharray="4 4" />
              <line x1={pad} x2={pad + usableW} y1={marginHighY} y2={marginHighY} stroke="rgba(255,255,255,0.25)" strokeDasharray="4 4" />
            </>
          )}
          {fade > 0 && <path d={rawPath} fill="none" stroke="rgba(91,230,255,0.35)" strokeWidth="1.5" />}
          <path
            d={fade > 0 ? fadedPath : rawPath}
            fill="none"
            stroke="url(#patPreviewGrad)"
            strokeWidth="2.5"
            strokeLinecap="round"
          />
          <line x1={pad} y1={h - pad} x2={pad + usableW} y2={h - pad} stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
          <line x1={pad} y1={pad} x2={pad} y2={h - pad} stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
        </svg>
      </div>
    </div>
  );
}
