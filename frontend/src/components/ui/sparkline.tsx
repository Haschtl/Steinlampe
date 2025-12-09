import { memo, useId, useMemo } from 'react';

type SparklineProps = {
  data: number[];
  height?: number;
  width?: number;
  color?: string;
  bg?: string;
  min?: number;
  max?: number;
};

export const Sparkline = memo(function Sparkline({
  data,
  height = 48,
  width = 160,
  color = 'var(--accent-color, #5be6ff)',
  bg = 'rgba(255,255,255,0.04)',
  min,
  max,
}: SparklineProps) {
  const clipId = useId();
  const samples = data.length === 0 ? [0, 0] : data.length === 1 ? [data[0], data[0]] : data;
  const { path, area } = useMemo(() => {
    if (!samples.length) return { path: '', area: '' };
    const yMin = min ?? Math.min(...samples);
    const yMax = max ?? Math.max(...samples);
    const span = yMax - yMin;
    const pts: { x: number; y: number }[] = [];
    for (let i = 0; i < samples.length; i++) {
      const v = samples[i];
      const x = (i / Math.max(1, samples.length - 1)) * width;
      const y =
        span < 1e-6
          ? height * 0.5
          : height - ((v - yMin) / span) * height;
      pts.push({ x, y });
    }
    if (!pts.length) return { path: '', area: '' };
    const p = `M ${pts.map((p) => `${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(' L ')}`;
    const a = `${p} L ${width} ${height} L 0 ${height} Z`;
    return { path: p, area: a };
  }, [samples, height, width, min, max]);

  return (
    <svg viewBox={`0 0 ${width} ${height}`} className="w-full" role="img" aria-label="sparkline">
      <defs>
        <clipPath id={clipId}>
          <rect x={0} y={0} width={width} height={height} rx={6} />
        </clipPath>
      </defs>
      <g clipPath={`url(#${clipId})`}>
        <rect x={0} y={0} width={width} height={height} rx={6} fill={bg} stroke="rgba(255,255,255,0.06)" />
        {area && <path d={area} fill={`${color}33`} />}
        {path && <path d={path} fill="none" stroke={color} strokeWidth={2} strokeLinecap="round" />}
      </g>
    </svg>
  );
});
