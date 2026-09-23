import { useEffect, useRef, useState } from 'react';
import { motion } from 'framer-motion';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

const WINDOW_MS = 10000; // scrolling window shown
const TICK_MS = 60;

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

function buildPath(samples: { t: number; v: number }[], now: number) {
  const w = 320;
  const h = 90;
  const pad = 14;
  const usableW = w - pad * 2;
  const usableH = h - pad * 2;
  const windowStart = now - WINDOW_MS;
  const visible = samples.filter((s) => s.t >= windowStart - 500);
  if (visible.length === 0) return { path: '', area: '', w, h, pad, usableW, usableH, last: undefined as number | undefined };

  const toXY = (s: { t: number; v: number }) => {
    const x = pad + clamp01((s.t - windowStart) / WINDOW_MS) * usableW;
    const y = h - pad - clamp01(s.v / 100) * usableH;
    return { x, y };
  };

  const pts = visible.map(toXY);
  // extend the trace to the current time at the last known level, so it visibly scrolls even when idle
  const last = visible[visible.length - 1];
  pts.push({ x: pad + usableW, y: toXY({ t: now, v: last.v }).y });

  const path = `M ${pts.map((p) => `${p.x.toFixed(2)} ${p.y.toFixed(2)}`).join(' L ')}`;
  const area = `${path} L ${pad + usableW} ${h - pad} L ${pad} ${h - pad} Z`;
  return { path, area, w, h, pad, usableW, usableH, last: last.v };
}

export function LiveOutputGraph() {
  const { status } = useConnection();
  const [now, setNow] = useState(() => Date.now());
  const rafRef = useRef<number | null>(null);
  const lastTickRef = useRef(0);

  useEffect(() => {
    const loop = (t: number) => {
      if (t - lastTickRef.current > TICK_MS) {
        lastTickRef.current = t;
        setNow(Date.now());
      }
      rafRef.current = requestAnimationFrame(loop);
    };
    rafRef.current = requestAnimationFrame(loop);
    return () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    };
  }, []);

  const samples = status.outputSamples ?? [];
  const { path, area, w, h, pad, usableW, usableH, last } = buildPath(samples, now);
  const live = status.connected && samples.length > 0;

  return (
    <div className="relative overflow-hidden rounded-xl border border-border bg-panel/70 shadow-inner">
      <div className="relative w-full" style={{ paddingTop: '30%' }}>
        <svg viewBox={`0 0 ${w} ${h}`} className="absolute inset-0 h-full w-full">
          <defs>
            <linearGradient id="liveOutGrad" x1="0%" x2="0%" y1="0%" y2="100%">
              <stop offset="0%" stopColor="#5be6ff" stopOpacity="0.5" />
              <stop offset="100%" stopColor="#5be6ff" stopOpacity="0.03" />
            </linearGradient>
            <pattern id="liveOutGrid" width="24" height="18" patternUnits="userSpaceOnUse">
              <path d="M24 0H0V18" fill="none" stroke="rgba(255,255,255,0.05)" strokeWidth="1" />
            </pattern>
          </defs>
          <rect x="0" y="0" width={w} height={h} fill="url(#liveOutGrid)" />
          {path && (
            <>
              <motion.path d={area} fill="url(#liveOutGrad)" opacity="0.6" initial={false} animate={{ d: area }} transition={{ duration: 0 }} />
              <motion.path
                d={path}
                fill="none"
                stroke="#5be6ff"
                strokeWidth="2.5"
                strokeLinecap="round"
                initial={false}
                animate={{ d: path }}
                transition={{ duration: 0 }}
              />
            </>
          )}
          <line x1={pad} y1={h - pad} x2={pad + usableW} y2={h - pad} stroke="rgba(255,255,255,0.15)" strokeWidth="1" />
          <text x={pad} y={pad - 3} className="text-muted" fill="currentColor" fontSize="9">
            100%
          </text>
          <text x={pad} y={h - pad + 10} className="text-muted" fill="currentColor" fontSize="9">
            0%
          </text>
          <text x={pad + usableW - 70} y={pad - 3} className="text-muted" fill="currentColor" fontSize="9">
            {live ? `${(last ?? 0).toFixed(0)}%` : '--'}
          </text>
        </svg>
      </div>
      <div className="flex items-center justify-between px-3 py-1.5 text-xs text-muted">
        <span className="flex items-center gap-1.5">
          <span className={`h-1.5 w-1.5 rounded-full ${live ? 'bg-emerald-400' : 'bg-muted'}`} />
          <Trans k="label.liveOutput">Live output</Trans>
        </span>
        <span>{WINDOW_MS / 1000}s</span>
      </div>
    </div>
  );
}
