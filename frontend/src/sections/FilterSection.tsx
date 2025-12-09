import { useMemo } from 'react';
import { Activity, Flame, Sparkles, Waves } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { SliderRow } from '@/components/ui/slider-row';
import { Knob } from '@/components/ui/knob';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function FilterSection() {
  const { status, sendCmd } = useConnection();

  const waveLabel = useMemo(() => (status.filterTremWave === 1 ? 'triangle' : 'sine'), [status.filterTremWave]);
  const curveLabel = useMemo(() => (status.filterClipCurve === 1 ? 'soft' : 'tanh'), [status.filterClipCurve]);

  return (
    <div className="grid gap-4">
      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Activity className="h-4 w-4" />
            <Trans k="filter.iir">IIR Smoothing</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterIir}
              onChange={(e) => sendCmd(`filter iir ${e.target.checked ? 'on' : 'off'} ${status.filterIirAlpha ?? 0.2}`)}
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent>
          <SliderRow
            label={<Trans k="filter.alpha">Alpha</Trans>}
            description={<Trans k="filter.iirDesc">Low-pass amount on the final output</Trans>}
            inputProps={{
              min: 0,
              max: 1,
              step: 0.01,
              value: status.filterIirAlpha ?? 0.2,
            }}
            onInputChange={(val) => sendCmd(`filter iir on ${val.toFixed(2)}`)}
            disabled={!status.filterIir}
          />
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Flame className="h-4 w-4" />
            <Trans k="filter.clip">Soft Clip</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterClip}
              onChange={(e) =>
                sendCmd(
                  `filter clip ${e.target.checked ? 'on' : 'off'} ${status.filterClipAmt ?? 0.15} ${curveLabel}`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="space-y-3">
          <SliderRow
            label={<Trans k="filter.amount">Amount</Trans>}
            description={<Trans k="filter.clipDesc">Adds punch without hard clipping</Trans>}
            inputProps={{
              min: 0,
              max: 1,
              step: 0.05,
              value: status.filterClipAmt ?? 0.15,
            }}
            onInputChange={(val) => sendCmd(`filter clip on ${val.toFixed(2)} ${curveLabel}`)}
            disabled={!status.filterClip}
          />
          <div className="flex items-center gap-2">
            <span className="text-sm text-muted"><Trans k="filter.curve">Curve</Trans></span>
            <Select
              value={curveLabel}
              onValueChange={(val) => sendCmd(`filter clip on ${status.filterClipAmt ?? 0.15} ${val}`)}
              disabled={!status.filterClip}
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
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Waves className="h-4 w-4" />
            <Trans k="filter.trem">Tremolo</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterTrem}
              onChange={(e) =>
                sendCmd(
                  `filter trem ${e.target.checked ? 'on' : 'off'} ${status.filterTremRate ?? 1.5} ${
                    status.filterTremDepth ?? 0.3
                  } ${waveLabel}`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="space-y-3">
          <SliderRow
            label={<Trans k="filter.rate">Rate (Hz)</Trans>}
            description={<Trans k="filter.tremDesc">Amplitude modulation over time</Trans>}
            inputProps={{
              min: 0.05,
              max: 20,
              step: 0.05,
              value: status.filterTremRate ?? 1.5,
            }}
            onInputChange={(val) =>
              sendCmd(`filter trem on ${val.toFixed(2)} ${status.filterTremDepth ?? 0.3} ${waveLabel}`)
            }
            disabled={!status.filterTrem}
          />
          <SliderRow
            label={<Trans k="filter.depth">Depth</Trans>}
            description={<Trans k="filter.depthDesc">How strong the modulation is</Trans>}
            inputProps={{
              min: 0,
              max: 1,
              step: 0.05,
              value: status.filterTremDepth ?? 0.3,
            }}
            onInputChange={(val) =>
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
              disabled={!status.filterTrem}
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
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Sparkles className="h-4 w-4" />
            <Trans k="filter.spark">Sparkle Overlay</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterSpark}
              onChange={(e) =>
                sendCmd(
                  `filter spark ${e.target.checked ? 'on' : 'off'} ${status.filterSparkDens ?? 0.6} ${
                    status.filterSparkInt ?? 0.25
                  } ${status.filterSparkDecay ?? 200}`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="space-y-3">
          <SliderRow
            label={<Trans k="filter.density">Density (events/s)</Trans>}
            description={<Trans k="filter.sparkDesc">Random flashes added on top</Trans>}
            inputProps={{
              min: 0,
              max: 20,
              step: 0.1,
              value: status.filterSparkDens ?? 0.6,
            }}
            onInputChange={(val) =>
              sendCmd(
                `filter spark on ${val.toFixed(2)} ${status.filterSparkInt ?? 0.25} ${
                  status.filterSparkDecay ?? 200
                }`,
              )
            }
            disabled={!status.filterSpark}
          />
          <SliderRow
            label={<Trans k="filter.intensity">Intensity</Trans>}
            description={<Trans k="filter.intDesc">How bright each sparkle is</Trans>}
            inputProps={{
              min: 0,
              max: 1,
              step: 0.05,
              value: status.filterSparkInt ?? 0.25,
            }}
            onInputChange={(val) =>
              sendCmd(
                `filter spark on ${status.filterSparkDens ?? 0.6} ${val.toFixed(2)} ${
                  status.filterSparkDecay ?? 200
                }`,
              )
            }
            disabled={!status.filterSpark}
          />
          <SliderRow
            label={<Trans k="filter.decay">Decay (ms)</Trans>}
            description={<Trans k="filter.decayDesc">Fade-out time per sparkle</Trans>}
            inputProps={{
              min: 10,
              max: 5000,
              step: 10,
              value: status.filterSparkDecay ?? 200,
            }}
            onInputChange={(val) =>
              sendCmd(
                `filter spark on ${status.filterSparkDens ?? 0.6} ${status.filterSparkInt ?? 0.25} ${Math.round(
                  val,
                )}`,
              )
            }
            disabled={!status.filterSpark}
          />
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Activity className="h-4 w-4" />
            <Trans k="filter.comp">Compressor</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterComp}
              onChange={(e) =>
                sendCmd(
                  `filter comp ${e.target.checked ? 'on' : 'off'} ${status.filterCompThr ?? 0.8} ${
                    status.filterCompRatio ?? 3
                  } ${status.filterCompAttack ?? 20} ${status.filterCompRelease ?? 180}`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="space-y-3">
          <SliderRow
            label={<Trans k="filter.compThr">Threshold</Trans>}
            description={<Trans k="filter.compDesc">Tames peaks above threshold</Trans>}
            inputProps={{
              min: 0,
              max: 1.2,
              step: 0.02,
              value: status.filterCompThr ?? 0.8,
            }}
            onInputChange={(val) =>
              sendCmd(
                `filter comp on ${val.toFixed(2)} ${status.filterCompRatio ?? 3} ${
                  status.filterCompAttack ?? 20
                } ${status.filterCompRelease ?? 180}`,
              )
            }
            disabled={!status.filterComp}
          />
          <SliderRow
            label={<Trans k="filter.compRatio">Ratio</Trans>}
            inputProps={{
              min: 1,
              max: 10,
              step: 0.2,
              value: status.filterCompRatio ?? 3,
            }}
            onInputChange={(val) =>
              sendCmd(
                `filter comp on ${status.filterCompThr ?? 0.8} ${val.toFixed(2)} ${
                  status.filterCompAttack ?? 20
                } ${status.filterCompRelease ?? 180}`,
              )
            }
            disabled={!status.filterComp}
          />
          <div className="grid gap-2 md:grid-cols-2">
            <SliderRow
              label={<Trans k="filter.compAttack">Attack (ms)</Trans>}
              inputProps={{
                min: 1,
                max: 2000,
                step: 5,
                value: status.filterCompAttack ?? 20,
              }}
              onInputChange={(val) =>
                sendCmd(
                  `filter comp on ${status.filterCompThr ?? 0.8} ${status.filterCompRatio ?? 3} ${Math.round(
                    val,
                  )} ${status.filterCompRelease ?? 180}`,
                )
              }
              disabled={!status.filterComp}
            />
            <SliderRow
              label={<Trans k="filter.compRelease">Release (ms)</Trans>}
              inputProps={{
                min: 1,
                max: 4000,
                step: 10,
                value: status.filterCompRelease ?? 180,
              }}
              onInputChange={(val) =>
                sendCmd(
                  `filter comp on ${status.filterCompThr ?? 0.8} ${status.filterCompRatio ?? 3} ${
                    status.filterCompAttack ?? 20
                  } ${Math.round(val)}`,
                )
              }
              disabled={!status.filterComp}
            />
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Waves className="h-4 w-4" />
            <Trans k="filter.env">Attack/Release Envelope</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterEnv}
              onChange={(e) =>
                sendCmd(
                  `filter env ${e.target.checked ? 'on' : 'off'} ${status.filterEnvAttack ?? 30} ${
                    status.filterEnvRelease ?? 120
                  }`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="grid gap-3 md:grid-cols-2">
          <SliderRow
            label={<Trans k="filter.compAttack">Attack (ms)</Trans>}
            inputProps={{
              min: 1,
              max: 4000,
              step: 5,
              value: status.filterEnvAttack ?? 30,
            }}
            onInputChange={(val) =>
              sendCmd(`filter env on ${Math.round(val)} ${status.filterEnvRelease ?? 120}`)
            }
            disabled={!status.filterEnv}
          />
          <SliderRow
            label={<Trans k="filter.compRelease">Release (ms)</Trans>}
            inputProps={{
              min: 1,
              max: 6000,
              step: 10,
              value: status.filterEnvRelease ?? 120,
            }}
            onInputChange={(val) =>
              sendCmd(`filter env on ${status.filterEnvAttack ?? 30} ${Math.round(val)}`)
            }
            disabled={!status.filterEnv}
          />
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Flame className="h-4 w-4" />
            <Trans k="filter.fold">Wavefolder</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterFold}
              onChange={(e) =>
                sendCmd(`filter fold ${e.target.checked ? 'on' : 'off'} ${status.filterFoldAmt ?? 0.2}`)
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent>
          <SliderRow
            label={<Trans k="filter.amount">Amount</Trans>}
            description={<Trans k="filter.foldDesc">Adds harmonics by folding the waveform</Trans>}
            inputProps={{
              min: 0,
              max: 1,
              step: 0.05,
              value: status.filterFoldAmt ?? 0.2,
            }}
            onInputChange={(val) => sendCmd(`filter fold on ${val.toFixed(2)}`)}
            disabled={!status.filterFold}
          />
        </CardContent>
      </Card>

      <Card>
        <CardHeader className="flex items-center justify-between">
          <CardTitle className="flex items-center gap-2">
            <Waves className="h-4 w-4" />
            <Trans k="filter.delay">Delay</Trans>
          </CardTitle>
          <label className="pill cursor-pointer">
            <input
              type="checkbox"
              className="accent-accent"
              checked={!!status.filterDelay}
              onChange={(e) =>
                sendCmd(
                  `filter delay ${e.target.checked ? 'on' : 'off'} ${status.filterDelayMs ?? 180} ${
                    status.filterDelayFb ?? 0.35
                  } ${status.filterDelayMix ?? 0.3}`,
                )
              }
            />{' '}
            <Trans k="btn.enable">Enable</Trans>
          </label>
        </CardHeader>
        <CardContent className="grid gap-4 md:grid-cols-3">
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
        </CardContent>
      </Card>
    </div>
  );
}
