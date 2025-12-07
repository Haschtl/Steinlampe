import { Mic } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function MusicCard() {
  const { sendCmd } = useConnection();

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle><Trans k="title.music">Music</Trans></CardTitle>
        <label className="pill cursor-pointer">
          <input type="checkbox" className="accent-accent" onChange={(e) => sendCmd(`music ${e.target.checked ? 'on' : 'off'}`)} /> Music
        </label>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-2">
          <Mic className="h-4 w-4 text-muted" />
          <Label className="m-0">Music gain</Label>
          <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => sendCmd(`music sens ${e.target.value}`)} className="w-24" suffix="x" />
        </div>
        <div className="flex items-center gap-2">
          <Label className="m-0">Clap thr</Label>
          <Input type="number" min={0.05} max={1.5} step={0.01} defaultValue={0.35} onBlur={(e) => sendCmd(`clap thr ${e.target.value}`)} className="w-24" suffix="x" />
          <Label className="m-0">Cool</Label>
          <Input type="number" min={200} max={5000} step={50} defaultValue={800} onBlur={(e) => sendCmd(`clap cool ${e.target.value}`)} className="w-24" suffix="ms" />
        </div>
      </CardContent>
    </Card>
  );
}
