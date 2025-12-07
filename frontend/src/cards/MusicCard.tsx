import { Mic } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Trans } from '@/i18n';

type Props = {
  onMusicToggle: (on: boolean) => void;
  onMusicGain: (v: string) => void;
  onClapThr: (v: string) => void;
  onClapCool: (v: string) => void;
};

export function MusicCard({ onMusicToggle, onMusicGain, onClapThr, onClapCool }: Props) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.music">Music</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onMusicToggle(e.target.checked)} /> Music
          </label>
        </div>
        <div className="flex items-center gap-2">
          <Mic className="h-4 w-4 text-muted" />
          <Label className="m-0">Music gain</Label>
          <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => onMusicGain(e.target.value)} className="w-24" suffix="x" />
        </div>
        <div className="flex items-center gap-2">
          <Label className="m-0">Clap thr</Label>
          <Input type="number" min={0.05} max={1.5} step={0.01} defaultValue={0.35} onBlur={(e) => onClapThr(e.target.value)} className="w-24" suffix="x" />
          <Label className="m-0">Cool</Label>
          <Input type="number" min={200} max={5000} step={50} defaultValue={800} onBlur={(e) => onClapCool(e.target.value)} className="w-24" suffix="ms" />
        </div>
      </CardContent>
    </Card>
  );
}
