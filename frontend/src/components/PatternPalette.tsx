import { useMemo } from 'react';
import { Palette } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { useConnection } from '@/context/connection';
import { patternLabel, patternLabels } from '@/data/patterns';

export function PatternPalette({ open, setOpen }: { open: boolean; setOpen: (v: boolean) => void }) {
  const { sendCmd, status } = useConnection();

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabel(i + 1),
    }));
  }, [status.patternCount]);

  const previewStyles = useMemo(
    () => [
      'linear-gradient(135deg, #5be6ff, #1f2937)',
      'radial-gradient(circle at 30% 20%, #ffaf45, #1f2937)',
      'linear-gradient(180deg, #facc15, #ef4444)',
      'radial-gradient(circle at 40% 40%, #a855f7, #0f172a)',
      'linear-gradient(120deg, #22d3ee, #0ea5e9, #0f172a)',
      'linear-gradient(135deg, #f59e0b, #b91c1c)',
      'linear-gradient(135deg, #34d399, #0f172a)',
      'radial-gradient(circle at 70% 20%, #67e8f9, #0f172a)',
    ],
    [],
  );

  return (
    <Dialog open={open} onOpenChange={setOpen}>
      <DialogTrigger asChild>
        <Button variant="primary" size="sm" className="ml-auto flex items-center gap-2">
          <Palette className="h-4 w-4" />
          <span className="hidden sm:inline">Pattern Palette</span>
        </Button>
      </DialogTrigger>
      <DialogContent className="max-w-4xl">
        <DialogHeader>
          <DialogTitle>Pattern Palette</DialogTitle>
        </DialogHeader>
        <div className="grid max-h-[70vh] gap-3 overflow-y-auto sm:grid-cols-2 lg:grid-cols-3">
          {patternOptions.map((p, i) => {
            const bg = previewStyles[i % previewStyles.length];
            return (
              <button
                key={p.idx}
                type="button"
                onClick={() => {
                  sendCmd(`mode ${p.idx}`);
                  setOpen(false);
                }}
                className="relative overflow-hidden rounded-2xl border border-border/70 p-4 text-left shadow-lg transition-transform hover:-translate-y-1 focus:outline-none focus:ring-2 focus:ring-accent"
                style={{ backgroundImage: bg }}
              >
                <div className="absolute inset-0 bg-black/25 backdrop-blur-[2px]" />
                <div className="relative flex items-center justify-between">
                  <div>
                    <div className="text-xs uppercase tracking-wide text-muted">Mode {p.idx}</div>
                    <div className="text-lg font-semibold text-text drop-shadow">{p.label}</div>
                  </div>
                  <div className="h-10 w-10 rounded-full bg-white/10 shadow-inner" />
                </div>
              </button>
            );
          })}
        </div>
      </DialogContent>
    </Dialog>
  );
}
