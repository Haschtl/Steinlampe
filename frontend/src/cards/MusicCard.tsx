import { useEffect, useState } from 'react';
import { Mic } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';

export function MusicCard() {
  const { status, sendCmd } = useConnection();
  const { t } = useI18n();
  const [musicEnabled, setMusicEnabled] = useState(false);
  const [gain, setGain] = useState(1);
  const [thr, setThr] = useState(0.35);
  const [cool, setCool] = useState(800);
  const [autoOn, setAutoOn] = useState(false);
  const [autoThr, setAutoThr] = useState(0.4);
  const [mode, setMode] = useState<'direct' | 'beat'>('direct');
  const [clap1, setClap1] = useState('mode next');
  const [clap2, setClap2] = useState('toggle');
  const [clap3, setClap3] = useState('mode prev');
  const clapEnabled = status.clapEnabled ?? false;

  useEffect(() => {
    if (typeof status.musicEnabled === 'boolean') setMusicEnabled(status.musicEnabled);
    if (typeof status.musicGain === 'number') setGain(status.musicGain);
    if (typeof status.clapThreshold === 'number') setThr(status.clapThreshold);
    if (typeof status.clapCooldownMs === 'number') setCool(status.clapCooldownMs);
    if (typeof status.musicAuto === 'boolean') setAutoOn(status.musicAuto);
    if (typeof status.musicAutoThr === 'number') setAutoThr(status.musicAutoThr);
    if (status.musicMode) setMode(status.musicMode === 'beat' ? 'beat' : 'direct');
  }, [status.clapCooldownMs, status.clapThreshold, status.musicEnabled, status.musicGain, status.musicAuto, status.musicAutoThr, status.musicMode]);

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <div className="flex flex-wrap w-full justify-between items-center gap-2">
          <CardTitle>
            <Trans k="title.music">Music</Trans>
          </CardTitle>
          <div>
            <label className="pill cursor-pointer">
              <input
                type="checkbox"
                className="accent-accent"
                checked={musicEnabled}
                onChange={(e) => {
                  const next = e.target.checked;
                  setMusicEnabled(next);
                  sendCmd(`music ${next ? "on" : "off"}`);
                }}
              />{" "}
              <Trans k="title.music">Music</Trans>
            </label>
            <label className="pill cursor-pointer">
              <input
                type="checkbox"
                className="accent-accent"
                checked={autoOn}
                onChange={(e) => {
                  const next = e.target.checked;
                  setAutoOn(next);
                  sendCmd(`music auto ${next ? "on" : "off"}`);
                }}
              />{" "}
              <Trans k="music.autoLamp">Auto Lamp</Trans>
            </label>
            <label className="pill cursor-pointer">
              <input
                type="checkbox"
                className="accent-accent"
                checked={clapEnabled}
                onChange={(e) => {
                  const next = e.target.checked;
                  sendCmd(`clap ${next ? "on" : "off"}`);
                }}
              />{" "}
              <Trans k="label.clap">Clap</Trans>
            </label>
          </div>
        </div>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="space-y-2 rounded-lg border border-border/70 bg-panel/70 p-3">
          <div className="flex items-center gap-2 text-sm font-semibold text-muted">
            <Trans k="title.music">Musik</Trans>
          </div>
          <div className="flex items-center gap-2 text-sm text-muted">
            <Mic className="h-4 w-4 text-muted" />
            <span>
              <Trans k="music.modeInfo">
                Direct: brightness follows the level. Beat: peaks trigger short
                pulses with decay. Auto Lamp can turn on the lamp whenever the
                signal crosses the threshold (even if music is off).
              </Trans>
            </span>
          </div>
          <div className="flex flex-wrap items-center gap-3">
            <Label className="m-0">
              <Trans k="label.musicGain">Music gain</Trans>
            </Label>
            <Input
              type="number"
              min={0.1}
              max={5}
              step={0.1}
              value={gain}
              onChange={(e) => setGain(Number(e.target.value))}
              onBlur={(e) => sendCmd(`music sens ${e.target.value}`)}
              className="w-24"
              suffix="x"
            />
            <Label className="m-0">
              <Trans k="label.threshold">Thr</Trans>
            </Label>
            <Input
              type="number"
              min={0.05}
              max={1.5}
              step={0.01}
              value={autoThr}
              onChange={(e) => setAutoThr(Number(e.target.value))}
              onBlur={(e) => sendCmd(`music auto thr ${e.target.value}`)}
              className="w-20"
            />
            <Label className="m-0">
              <Trans k="label.mode">Mode</Trans>
            </Label>
            <select
              value={mode}
              onChange={(e) => {
                const v = e.target.value === "beat" ? "beat" : "direct";
                setMode(v);
                sendCmd(`music mode ${v}`);
              }}
              className="rounded-md border border-border bg-panel px-2 py-1 text-sm"
            >
              <option value="direct">Direct</option>
              <option value="beat">Beat</option>
            </select>
          </div>
          <div className="text-xs text-muted leading-snug">
            {mode === "direct"
              ? t(
                  "music.modeDirect",
                  "Direct: brightness follows the level. Auto-Lamp can additionally switch on with loud signal."
                )
              : t(
                  "music.modeBeat",
                  "Beat: peaks trigger short flashes, decay holds until the next beat. Auto-Lamp shares the threshold."
                )}
          </div>
        </div>
        <div className="space-y-3 rounded-lg border border-border/70 bg-panel/70 p-3">
          <div className="flex items-center gap-2 text-sm font-semibold text-muted">
            <Trans k="label.clap">Klatschen</Trans>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <Label className="m-0">
              <Trans k="label.clapThr">Clap thr</Trans>
            </Label>
            <Input
              type="number"
              min={0.05}
              max={1.5}
              step={0.01}
              value={thr}
              onChange={(e) => setThr(Number(e.target.value))}
              onBlur={(e) => sendCmd(`clap thr ${e.target.value}`)}
              className="w-24"
              suffix="x"
            />
            <Label className="m-0">
              <Trans k="label.cool">Cooldown</Trans>
            </Label>
            <Input
              type="number"
              min={200}
              max={5000}
              step={50}
              value={cool}
              onChange={(e) => setCool(Number(e.target.value))}
              onBlur={(e) => sendCmd(`clap cool ${e.target.value}`)}
              className="w-24"
              suffix="ms"
            />
          </div>
          <div className="space-y-2">
            <p className="text-sm text-muted">
              <Trans k="music.clapActions">
                Clap actions (send command on 1/2/3 claps)
              </Trans>
            </p>
            <div className="grid gap-2 md:grid-cols-3">
              <div className="space-y-1">
                <Label className="m-0 text-muted">1x Clap</Label>
                <Input
                  value={clap1}
                  onChange={(e) => setClap1(e.target.value)}
                  onBlur={(e) =>
                    e.target.value && sendCmd(`clap 1 ${e.target.value}`)
                  }
                />
              </div>
              <div className="space-y-1">
                <Label className="m-0 text-muted">2x Clap</Label>
                <Input
                  value={clap2}
                  onChange={(e) => setClap2(e.target.value)}
                  onBlur={(e) =>
                    e.target.value && sendCmd(`clap 2 ${e.target.value}`)
                  }
                />
              </div>
              <div className="space-y-1">
                <Label className="m-0 text-muted">3x Clap</Label>
                <Input
                  value={clap3}
                  onChange={(e) => setClap3(e.target.value)}
                  onBlur={(e) =>
                    e.target.value && sendCmd(`clap 3 ${e.target.value}`)
                  }
                />
              </div>
            </div>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
