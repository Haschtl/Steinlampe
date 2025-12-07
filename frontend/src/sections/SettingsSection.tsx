import { Mic, Shield, Sun } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';

type SettingsProps = {
  cap: number;
  setCap: (v: number) => void;
  idleMinutes: number;
  setIdleMinutes: (v: number) => void;
  pwmCurve: number;
  setPwmCurve: (v: number) => void;
  handleCap: () => void;
  handleIdle: () => void;
  handlePwm: (v: number) => void;
};

type PresenceTouchProps = {
  presenceText?: string;
  onPresenceToggle: (on: boolean) => void;
  onPresenceMe: () => void;
  onPresenceClear: () => void;
  onTouchCalib: () => void;
  onTouchDebug: () => void;
  onTouchDimToggle: (on: boolean) => void;
};

type LightMusicProps = {
  onLightToggle: (on: boolean) => void;
  onLightGain: (v: string) => void;
  onLightCalib: () => void;
  onMusicToggle: (on: boolean) => void;
  onMusicGain: (v: string) => void;
  onClapThr: (v: string) => void;
  onClapCool: (v: string) => void;
};

export function SettingsCard({ cap, setCap, idleMinutes, setIdleMinutes, pwmCurve, setPwmCurve, handleCap, handleIdle, handlePwm }: SettingsProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle>Settings</CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div>
          <Label>Brightness cap (%)</Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={1} max={100} value={cap} onChange={(e) => setCap(Number(e.target.value))} className="w-28" suffix="%" />
            <Button onClick={handleCap}>Set</Button>
          </div>
        </div>
        <div>
          <Label>Idle off (min)</Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" suffix="min" />
            <Button onClick={handleIdle}>Set</Button>
          </div>
        </div>
        <div>
          <Label>PWM Curve</Label>
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
              description="Gamma to linearize LED brightness (0.5–4)"
            />
          </div>
        </div>
      </CardContent>
    </Card>
  );
}

export function PresenceTouchCard({
  presenceText,
  onPresenceToggle,
  onPresenceMe,
  onPresenceClear,
  onTouchCalib,
  onTouchDebug,
  onTouchDimToggle,
}: PresenceTouchProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle>Presence &amp; Touch</CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onPresenceToggle(e.target.checked)} /> Presence
          </label>
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onTouchDimToggle(e.target.checked)} /> TouchDim
          </label>
          <Button onClick={onPresenceMe}>
            <Shield className="mr-1 h-4 w-4" /> Set me
          </Button>
          <Button onClick={onPresenceClear}>Clear</Button>
        </div>
        <p className="text-sm text-muted">Status: {presenceText ?? '---'}</p>
        <div className="flex gap-2">
          <Button onClick={onTouchCalib}>Calibrate touch</Button>
          <Button onClick={onTouchDebug}>Touch debug</Button>
        </div>
      </CardContent>
    </Card>
  );
}

export function LightMusicCard({
  onLightToggle,
  onLightGain,
  onLightCalib,
  onMusicToggle,
  onMusicGain,
  onClapThr,
  onClapCool,
}: LightMusicProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle>Light &amp; Music</CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex items-center gap-2 flex-wrap">
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onLightToggle(e.target.checked)} /> Light
          </label>
          <label className="pill cursor-pointer">
            <input type="checkbox" className="accent-accent" onChange={(e) => onMusicToggle(e.target.checked)} /> Music
          </label>
        </div>
        <div className="flex items-center gap-2">
          <Sun className="h-4 w-4 text-muted" />
          <Label className="m-0">Light gain</Label>
          <Input type="number" min={0.1} max={5} step={0.1} defaultValue={1} onBlur={(e) => onLightGain(e.target.value)} className="w-24" suffix="x" />
          <Button onClick={onLightCalib}>Calibrate</Button>
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
