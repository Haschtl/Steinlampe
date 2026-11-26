import { useEffect, useMemo, useState } from 'react';
import { Activity, Flame, Sparkles, Waves } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Knob } from '@/components/ui/knob';
import { Button } from '@/components/ui/button';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';
import { cn } from '@/lib/utils';

type EffectKey = 'iir' | 'clip' | 'trem' | 'spark' | 'comp' | 'env' | 'delay';

export function FilterSection() {
  const { status, sendCmd, sendCmdReliable } = useConnection();
  const { t } = useI18n();
  const [openEffect, setOpenEffect] = useState<EffectKey | null>(null);

  // Filter params are their own on-demand status group (status protocol
  // redesign) - fetch them only while this page is actually open, instead of
  // every filter_* field riding along on every general status broadcast.
  useEffect(() => {
    if (status.connected && status.statusReady) sendCmd('status filters').catch((e) => console.warn(e));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status.connected, status.statusReady]);

  // write-with-response + explicit refetch of just the FILTERS group for the
  // Enable toggles specifically (not the knobs below, which fire on every
  // drag tick and would otherwise flood the link) - the firmware only ever
  // announces a filter change as a plain feedback line, never a structured
  // one, on its own. See memory: status protocol redesign (and
  // firmware-status-fields-added-2026-11 for the original gap this follows).
  const toggleFilter = (cmd: string) =>
    sendCmdReliable(cmd).then(() => sendCmd('status filters')).catch((e) => console.warn(e));

  const waveLabel = useMemo(() => (status.filterTremWave === 1 ? 'triangle' : 'sine'), [status.filterTremWave]);
  const curveLabel = useMemo(() => (status.filterClipCurve === 1 ? 'soft' : 'tanh'), [status.filterClipCurve]);

  const presets: { name: string; commands: string[] }[] = [
    {
      name: 'Soft Glow',
      commands: [
        'filter iir on 0.20',
        'filter clip on 0.18 tanh',
        'filter spark off',
        'filter trem off',
        'filter comp off',
        'filter env off',
        'filter delay off',
      ],
    },
    {
      name: 'Punchy',
      commands: [
        'filter clip on 0.35 tanh',
        'filter comp on 0.70 3.0 20 160',
        'filter iir on 0.10',
        'filter trem off',
      ],
    },
    {
      name: 'Motion',
      commands: [
        'filter trem on 2.0 0.4 sine',
        'filter env on 30 220',
        'filter delay on 240 0.25 0.2',
      ],
    },
  ];

  const applyPreset = async (cmds: string[]) => {
    for (const c of cmds) {
      try {
        await sendCmd(c);
      } catch (err) {
        console.warn(err);
      }
    }
  };

  const effects: { key: EffectKey; icon: typeof Activity; titleKey: string; titleFallback: string; active: boolean }[] = [
    { key: 'iir', icon: Activity, titleKey: 'filter.iir', titleFallback: 'IIR Smoothing', active: !!status.filterIir },
    { key: 'clip', icon: Flame, titleKey: 'filter.clip', titleFallback: 'Soft Clip', active: !!status.filterClip },
    { key: 'trem', icon: Waves, titleKey: 'filter.trem', titleFallback: 'Tremolo', active: !!status.filterTrem },
    { key: 'spark', icon: Sparkles, titleKey: 'filter.spark', titleFallback: 'Sparkle Overlay', active: !!status.filterSpark },
    { key: 'comp', icon: Activity, titleKey: 'filter.comp', titleFallback: 'Compressor', active: !!status.filterComp },
    { key: 'env', icon: Waves, titleKey: 'filter.env', titleFallback: 'Attack/Release Envelope', active: !!status.filterEnv },
    { key: 'delay', icon: Waves, titleKey: 'filter.delay', titleFallback: 'Delay', active: !!status.filterDelay },
  ];

  return (
    <div className="grid gap-4">
      <Card>
        <CardHeader>
          <CardTitle><Trans k="filter.presets">Presets</Trans></CardTitle>
        </CardHeader>
        <CardContent className="flex flex-wrap gap-2">
          {presets.map((p) => (
            <Button
              key={p.name}
              variant="secondary"
              size="sm"
              className="h-9 whitespace-nowrap"
              onClick={() => applyPreset(p.commands)}
            >
              {p.name}
            </Button>
          ))}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="filter.effects">Effects</Trans></CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-4 gap-3 sm:grid-cols-5 md:grid-cols-7">
            {effects.map((eff) => (
              <button
                key={eff.key}
                type="button"
                onClick={() => setOpenEffect(eff.key)}
                title={t(eff.titleKey, eff.titleFallback)}
                className={cn(
                  'card flex flex-col items-center gap-1.5 p-3 text-center transition hover:border-accent hover:text-accent',
                  eff.active && 'border-accent bg-accent/10 text-accent',
                )}
              >
                <eff.icon className="h-6 w-6" />
                <span className="text-xs font-medium leading-tight">{t(eff.titleKey, eff.titleFallback)}</span>
              </button>
            ))}
          </div>
        </CardContent>
      </Card>

      <Dialog open={openEffect === 'iir'} onOpenChange={(o) => setOpenEffect(o ? 'iir' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Activity className="h-4 w-4" />
              <Trans k="filter.iir">IIR Smoothing</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterIir}
              onChange={(e) => toggleFilter(`filter iir ${e.target.checked ? 'on' : 'off'} ${status.filterIirAlpha ?? 0.2}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap items-start gap-4 justify-around pt-2">
            <Knob
              label={<Trans k="filter.alpha">Alpha</Trans>}
              min={0}
              max={1}
              step={0.01}
              value={status.filterIirAlpha ?? 0.2}
              onChange={(val) => sendCmd(`filter iir on ${val.toFixed(2)}`)}
              disabled={!status.filterIir}
            />
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'clip'} onOpenChange={(o) => setOpenEffect(o ? 'clip' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Flame className="h-4 w-4" />
              <Trans k="filter.clip">Soft Clip</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterClip}
              onChange={(e) => toggleFilter(`filter clip ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap items-start gap-4 justify-around pt-2">
            <Knob
              label={<Trans k="filter.amount">Amount</Trans>}
              min={0}
              max={1}
              step={0.05}
              value={status.filterClipAmt ?? 0.15}
              onChange={(val) => sendCmd(`filter clip on ${val.toFixed(2)} ${curveLabel}`)}
              disabled={!status.filterClip}
            />
            <div className="flex items-center gap-2">
              <span className="text-sm text-muted"><Trans k="filter.curve">Curve</Trans></span>
              <Select
                value={curveLabel}
                onValueChange={(val) => sendCmd(`filter clip on ${status.filterClipAmt ?? 0.15} ${val}`)}
              >
                <SelectTrigger className="w-32">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="tanh">tanh</SelectItem>
                  <SelectItem value="soft">soft</SelectItem>
                </SelectContent>
              </Select>
            </div>
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'trem'} onOpenChange={(o) => setOpenEffect(o ? 'trem' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Waves className="h-4 w-4" />
              <Trans k="filter.trem">Tremolo</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterTrem}
              onChange={(e) => toggleFilter(`filter trem ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap justify-around items-start gap-4 pt-2">
            <Knob
              label={<Trans k="filter.rate">Rate (Hz)</Trans>}
              min={0.05}
              max={20}
              step={0.05}
              value={status.filterTremRate ?? 1.5}
              onChange={(val) =>
                sendCmd(`filter trem on ${val.toFixed(2)} ${status.filterTremDepth ?? 0.3} ${waveLabel}`)
              }
              disabled={!status.filterTrem}
            />
            <Knob
              label={<Trans k="filter.depth">Depth</Trans>}
              min={0}
              max={1}
              step={0.05}
              value={status.filterTremDepth ?? 0.3}
              onChange={(val) =>
                sendCmd(`filter trem on ${status.filterTremRate ?? 1.5} ${val.toFixed(2)} ${waveLabel}`)
              }
              disabled={!status.filterTrem}
            />
            <div className="flex items-center gap-2">
              <span className="text-sm text-muted"><Trans k="filter.wave">Wave</Trans></span>
              <Select
                value={waveLabel}
                onValueChange={(val) =>
                  sendCmd(`filter trem on ${status.filterTremRate ?? 1.5} ${status.filterTremDepth ?? 0.3} ${val}`)
                }
              >
                <SelectTrigger className="w-32">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="sine">sine</SelectItem>
                  <SelectItem value="triangle">triangle</SelectItem>
                </SelectContent>
              </Select>
            </div>
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'spark'} onOpenChange={(o) => setOpenEffect(o ? 'spark' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Sparkles className="h-4 w-4" />
              <Trans k="filter.spark">Sparkle Overlay</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterSpark}
              onChange={(e) => toggleFilter(`filter spark ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap items-start gap-4 justify-around pt-2">
            <Knob
              label={<Trans k="filter.density">Density (events/s)</Trans>}
              min={0}
              max={20}
              step={0.1}
              value={status.filterSparkDens ?? 0.6}
              onChange={(val) =>
                sendCmd(
                  `filter spark on ${val.toFixed(2)} ${status.filterSparkInt ?? 0.25} ${
                    status.filterSparkDecay ?? 200
                  }`,
                )
              }
              disabled={!status.filterSpark}
            />
            <Knob
              label={<Trans k="filter.intensity">Intensity</Trans>}
              min={0}
              max={1}
              step={0.05}
              value={status.filterSparkInt ?? 0.25}
              onChange={(val) =>
                sendCmd(
                  `filter spark on ${status.filterSparkDens ?? 0.6} ${val.toFixed(2)} ${
                    status.filterSparkDecay ?? 200
                  }`,
                )
              }
              disabled={!status.filterSpark}
            />
            <Knob
              label={<Trans k="filter.decay">Decay (ms)</Trans>}
              min={10}
              max={5000}
              step={10}
              value={status.filterSparkDecay ?? 200}
              onChange={(val) =>
                sendCmd(
                  `filter spark on ${status.filterSparkDens ?? 0.6} ${status.filterSparkInt ?? 0.25} ${Math.round(
                    val,
                  )}`,
                )
              }
              disabled={!status.filterSpark}
            />
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'comp'} onOpenChange={(o) => setOpenEffect(o ? 'comp' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Activity className="h-4 w-4" />
              <Trans k="filter.comp">Compressor</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterComp}
              onChange={(e) => toggleFilter(`filter comp ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap items-start gap-4 justify-around pt-2">
            <Knob
              label={<Trans k="filter.compThr">Threshold</Trans>}
              min={0}
              max={1.2}
              step={0.02}
              value={status.filterCompThr ?? 0.8}
              onChange={(val) =>
                sendCmd(
                  `filter comp on ${val.toFixed(2)} ${status.filterCompRatio ?? 3} ${
                    status.filterCompAttack ?? 20
                  } ${status.filterCompRelease ?? 180}`,
                )
              }
              disabled={!status.filterComp}
            />
            <Knob
              label={<Trans k="filter.compRatio">Ratio</Trans>}
              min={1}
              max={10}
              step={0.2}
              value={status.filterCompRatio ?? 3}
              onChange={(val) =>
                sendCmd(
                  `filter comp on ${status.filterCompThr ?? 0.8} ${val.toFixed(2)} ${
                    status.filterCompAttack ?? 20
                  } ${status.filterCompRelease ?? 180}`,
                )
              }
              disabled={!status.filterComp}
            />
            <Knob
              label={<Trans k="filter.compAttack">Attack (ms)</Trans>}
              min={1}
              max={2000}
              step={5}
              value={status.filterCompAttack ?? 20}
              onChange={(val) =>
                sendCmd(
                  `filter comp on ${status.filterCompThr ?? 0.8} ${status.filterCompRatio ?? 3} ${Math.round(
                    val,
                  )} ${status.filterCompRelease ?? 180}`,
                )
              }
              disabled={!status.filterComp}
            />
            <Knob
              label={<Trans k="filter.compRelease">Release (ms)</Trans>}
              min={1}
              max={4000}
              step={10}
              value={status.filterCompRelease ?? 180}
              onChange={(val) =>
                sendCmd(
                  `filter comp on ${status.filterCompThr ?? 0.8} ${status.filterCompRatio ?? 3} ${
                    status.filterCompAttack ?? 20
                  } ${Math.round(val)}`,
                )
              }
              disabled={!status.filterComp}
            />
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'env'} onOpenChange={(o) => setOpenEffect(o ? 'env' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Waves className="h-4 w-4" />
              <Trans k="filter.env">Attack/Release Envelope</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterEnv}
              onChange={(e) => toggleFilter(`filter env ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="flex flex-wrap gap-4 justify-around pt-2">
            <Knob
              label={<Trans k="filter.compAttack">Attack (ms)</Trans>}
              min={1}
              max={4000}
              step={5}
              value={status.filterEnvAttack ?? 30}
              onChange={(val) =>
                sendCmd(`filter env on ${Math.round(val)} ${status.filterEnvRelease ?? 120}`)
              }
              disabled={!status.filterEnv}
            />
            <Knob
              label={<Trans k="filter.compRelease">Release (ms)</Trans>}
              min={1}
              max={6000}
              step={10}
              value={status.filterEnvRelease ?? 120}
              onChange={(val) =>
                sendCmd(`filter env on ${status.filterEnvAttack ?? 30} ${Math.round(val)}`)
              }
              disabled={!status.filterEnv}
            />
          </div>
        </DialogContent>
      </Dialog>

      <Dialog open={openEffect === 'delay'} onOpenChange={(o) => setOpenEffect(o ? 'delay' : null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2">
              <Waves className="h-4 w-4" />
              <Trans k="filter.delay">Delay</Trans>
            </DialogTitle>
          </DialogHeader>
          <label className="pill cursor-pointer self-start">
            <input
              type="checkbox"
              checked={!!status.filterDelay}
              onChange={(e) => toggleFilter(`filter delay ${e.target.checked ? 'on' : 'off'}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
          <div className="grid gap-4 md:grid-cols-3 pt-2">
            <Knob
              label={<Trans k="filter.delayMs">Delay (ms)</Trans>}
              min={10}
              max={5000}
              step={10}
              value={status.filterDelayMs ?? 180}
              disabled={!status.filterDelay}
              onChange={(val) =>
                sendCmd(
                  `filter delay on ${Math.round(val)} ${status.filterDelayFb ?? 0.35} ${
                    status.filterDelayMix ?? 0.3
                  }`,
                )
              }
            />
            <Knob
              label={<Trans k="filter.delayFb">Feedback</Trans>}
              min={0}
              max={0.95}
              step={0.01}
              value={status.filterDelayFb ?? 0.35}
              disabled={!status.filterDelay}
              onChange={(val) =>
                sendCmd(
                  `filter delay on ${status.filterDelayMs ?? 180} ${val.toFixed(2)} ${
                    status.filterDelayMix ?? 0.3
                  }`,
                )
              }
            />
            <Knob
              label={<Trans k="filter.delayMix">Mix</Trans>}
              min={0}
              max={1}
              step={0.01}
              value={status.filterDelayMix ?? 0.3}
              disabled={!status.filterDelay}
              onChange={(val) =>
                sendCmd(
                  `filter delay on ${status.filterDelayMs ?? 180} ${status.filterDelayFb ?? 0.35} ${val.toFixed(2)}`,
                )
              }
            />
          </div>
        </DialogContent>
      </Dialog>
    </div>
  );
}
