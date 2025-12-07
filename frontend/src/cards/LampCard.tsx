import { useEffect, useMemo, useState } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle, Flashlight, Pause, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { patternLabels } from '@/data/patterns';
import { Trans } from '@/i18n';

const RampGraph = ({ duration, reverse }: { duration?: number; reverse?: boolean }) => {
  const dur = Math.max(50, Math.min(5000, duration ?? 500));
  const path = reverse
    ? 'M8 10 C 42 12 74 50 112 58'
    : 'M8 58 C 42 50 74 12 112 10';
  const gradientId = reverse ? 'rampGradOff' : 'rampGradOn';
  return (
    <div className="flex items-center gap-3 rounded-lg border border-border bg-panel px-2 py-2">
      <svg viewBox="0 0 120 70" className="h-14 w-24">
        <defs>
          <linearGradient id={gradientId} x1="0%" x2="100%" y1="0%" y2="0%">
            <stop offset="0%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.2" />
            <stop offset="100%" stopColor={reverse ? '#f97316' : '#22d3ee'} stopOpacity="0.6" />
          </linearGradient>
        </defs>
        <rect x="0" y="0" width="120" height="70" fill="url(#{gradientId})" fillOpacity="0.08" />
        <path d={path} stroke={`url(#{gradientId})`} strokeWidth="4" fill="none" strokeLinecap="round" />
        <line x1="8" y1="8" x2="8" y2="62" stroke="#1f2937" strokeWidth="1.5" />
        <line x1="8" y1="58" x2="112" y2="58" stroke="#1f2937" strokeWidth="1.5" />
      </svg>
      <div className="flex flex-col text-xs text-muted">
        <span className="text-sm font-semibold text-text">{dur} ms</span>
        <span>{reverse ? 'Fade out' : 'Fade in'}</span>
      </div>
    </div>
  );
};

