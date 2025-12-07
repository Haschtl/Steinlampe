import { Mic } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';
import { useState } from 'react';

export function MusicCard() {
  const { sendCmd } = useConnection();
  const [clap1, setClap1] = useState('mode next');
  const [clap2, setClap2] = useState('toggle');
  const [clap3, setClap3] = useState('mode prev');

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
        <div className="space-y-2">
          <p className="text-sm text-muted"><Trans k="music.clapActions">Clap actions (send command on 1/2/3 claps)</Trans></p>
          <div className="grid gap-2 md:grid-cols-3">
            <div className="space-y-1">
              <Label className="m-0 text-muted">1x Clap</Label>
              <Input value={clap1} onChange={(e) => setClap1(e.target.value)} onBlur={(e) => e.target.value && sendCmd(`clap 1 ${e.target.value}`)} />
            </div>
            <div className="space-y-1">
              <Label className="m-0 text-muted">2x Clap</Label>
              <Input value={clap2} onChange={(e) => setClap2(e.target.value)} onBlur={(e) => e.target.value && sendCmd(`clap 2 ${e.target.value}`)} />
            </div>
            <div className="space-y-1">
              <Label className="m-0 text-muted">3x Clap</Label>
              <Input value={clap3} onChange={(e) => setClap3(e.target.value)} onBlur={(e) => e.target.value && sendCmd(`clap 3 ${e.target.value}`)} />
            </div>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
