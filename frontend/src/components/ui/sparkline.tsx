import { memo, useMemo } from 'react';

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
  const { path, area } = useMemo(() => {
    if (!data.length) return { path: '', area: '' };
    const yMin = min ?? Math.min(...data);
    const yMax = max ?? Math.max(...data);
    const span = yMax - yMin || 1;
    const pts = data.map((v, i) => {
      const x = (i / Math.max(1, data.length - 1)) * width;
      const y = height - ((v - yMin) / span) * height;
      return { x, y };
    });
    const p = `M ${pts.map((p) => `${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(' L ')}`;
    const a = `${p} L ${width} ${height} L 0 ${height} Z`;
    return { path: p, area: a };
  }, [data, height, width, min, max]);

  return (
    <svg viewBox={`0 0 ${width} ${height}`} className="w-full" role="img" aria-label="sparkline">
      <rect x={0} y={0} width={width} height={height} rx={6} fill={bg} />
      {area && <path d={area} fill={`${color}33`} />}
      {path && <path d={path} fill="none" stroke={color} strokeWidth={2} strokeLinecap="round" />}
    </svg>
  );
});
