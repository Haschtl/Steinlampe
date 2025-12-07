import { useEffect, useState } from 'react';
import { LightCard } from '@/cards/LightCard';
import { MusicCard } from '@/cards/MusicCard';
import { PresenceCard } from '@/cards/PresenceCard';
import { TouchCard } from '@/cards/TouchCard';
import { SettingsCard } from '@/cards/SettingsCard';
import { useConnection } from '@/context/connection';

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
      <PresenceCard
        presenceText={status.presence}
        onPresenceToggle={(on) => sendCmd(`presence ${on ? 'on' : 'off'}`)}
        onPresenceMe={() => sendCmd('presence set me')}
        onPresenceClear={() => sendCmd('presence clear')}
      />
      <TouchCard
        onTouchCalib={() => sendCmd('calibrate touch')}
        onTouchDebug={() => sendCmd('touch')}
        onTouchDimToggle={(on) => sendCmd(`touchdim ${on ? 'on' : 'off'}`)}
      />
      <LightCard
        onLightToggle={(on) => sendCmd(`light ${on ? 'on' : 'off'}`)}
        onLightGain={(v) => sendCmd(`light gain ${v}`)}
        onLightCalib={() => sendCmd('light calib')}
      />
      <MusicCard
        onMusicToggle={(on) => sendCmd(`music ${on ? 'on' : 'off'}`)}
        onMusicGain={(v) => sendCmd(`music sens ${v}`)}
        onClapThr={(v) => sendCmd(`clap thr ${v}`)}
        onClapCool={(v) => sendCmd(`clap cool ${v}`)}
      />
    </div>
  );
}
