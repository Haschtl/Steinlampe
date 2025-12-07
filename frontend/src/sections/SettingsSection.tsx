import { useEffect, useState } from 'react';
import { Mic, Shield, Sun } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

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

export function SettingsCard({ cap, setCap, idleMinutes, setIdleMinutes, pwmCurve, setPwmCurve, handleCap, handleIdle, handlePwm }: SettingsProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.settings">Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div>
          <Label><Trans k="label.cap">Brightness cap (%)</Trans></Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={1} max={100} value={cap} onChange={(e) => setCap(Number(e.target.value))} className="w-28" suffix="%" />
            <Button onClick={handleCap}>Set</Button>
          </div>
        </div>
        <div>
          <Label><Trans k="label.idle">Idle off (min)</Trans></Label>
          <div className="flex items-center gap-2">
            <Input type="number" min={0} max={180} value={idleMinutes} onChange={(e) => setIdleMinutes(Number(e.target.value))} className="w-28" suffix="min" />
            <Button onClick={handleIdle}>Set</Button>
          </div>
        </div>
        <div>
          <Label><Trans k="label.pwm">PWM Curve</Trans></Label>
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

type PresenceTouchProps = {
  presenceText?: string;
  onPresenceToggle: (on: boolean) => void;
  onPresenceMe: () => void;
  onPresenceClear: () => void;
  onTouchCalib: () => void;
  onTouchDebug: () => void;
  onTouchDimToggle: (on: boolean) => void;
};

function PresenceTouchCard({ presenceText, onPresenceToggle, onPresenceMe, onPresenceClear, onTouchCalib, onTouchDebug, onTouchDimToggle }: PresenceTouchProps) {
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

type LightMusicProps = {
  onLightToggle: (on: boolean) => void;
  onLightGain: (v: string) => void;
  onLightCalib: () => void;
  onMusicToggle: (on: boolean) => void;
  onMusicGain: (v: string) => void;
  onClapThr: (v: string) => void;
  onClapCool: (v: string) => void;
};

function LightMusicCard({ onLightToggle, onLightGain, onLightCalib, onMusicToggle, onMusicGain, onClapThr, onClapCool }: LightMusicProps) {
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

export function SettingsSection() {
  const { status, sendCmd } = useConnection();
  const [cap, setCap] = useState(100);
  const [idleMinutes, setIdleMinutes] = useState(0);
  const [pwmCurve, setPwmCurve] = useState(1.8);

  useEffect(() => {
    if (typeof status.cap === 'number') setCap(Math.round(status.cap));
    if (typeof status.idleMinutes === 'number') setIdleMinutes(status.idleMinutes);
    if (typeof status.pwmCurve === 'number') setPwmCurve(status.pwmCurve);
  }, [status.cap, status.idleMinutes, status.pwmCurve]);

  const handleCap = () => {
    const clamped = Math.min(100, Math.max(1, Math.round(cap)));
    setCap(clamped);
    sendCmd(`bri cap ${clamped}`).catch((e) => console.warn(e));
  };

  const handleIdle = () => {
    const minutes = Math.max(0, idleMinutes);
    setIdleMinutes(minutes);
    sendCmd(`idleoff ${minutes}`).catch((e) => console.warn(e));
  };

  const handlePwm = (val: number) => {
    setPwmCurve(val);
    if (!Number.isNaN(val)) sendCmd(`pwm curve ${val.toFixed(2)}`).catch((e) => console.warn(e));
  };

  return (
    <div className="space-y-4">
      <SettingsCard
        cap={cap}
        setCap={setCap}
        idleMinutes={idleMinutes}
        setIdleMinutes={setIdleMinutes}
        pwmCurve={pwmCurve}
        setPwmCurve={setPwmCurve}
        handleCap={handleCap}
        handleIdle={handleIdle}
        handlePwm={handlePwm}
      />
      <div className="grid gap-4 md:grid-cols-2">
        <PresenceTouchCard
          presenceText={status.presence}
          onPresenceToggle={(on) => sendCmd(`presence ${on ? 'on' : 'off'}`)}
          onPresenceMe={() => sendCmd('presence set me')}
          onPresenceClear={() => sendCmd('presence clear')}
          onTouchCalib={() => sendCmd('calibrate touch')}
          onTouchDebug={() => sendCmd('touch')}
          onTouchDimToggle={(on) => sendCmd(`touchdim ${on ? 'on' : 'off'}`)}
        />
        <LightMusicCard
          onLightToggle={(on) => sendCmd(`light ${on ? 'on' : 'off'}`)}
          onLightGain={(v) => sendCmd(`light gain ${v}`)}
          onLightCalib={() => sendCmd('light calib')}
          onMusicToggle={(on) => sendCmd(`music ${on ? 'on' : 'off'}`)}
          onMusicGain={(v) => sendCmd(`music sens ${v}`)}
          onClapThr={(v) => sendCmd(`clap thr ${v}`)}
          onClapCool={(v) => sendCmd(`clap cool ${v}`)}
        />
      </div>
    </div>
  );
}
