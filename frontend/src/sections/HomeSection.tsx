import { ReactNode } from 'react';
import { Activity, ArrowLeftCircle, ArrowRightCircle, ClipboardPaste, Copy, Flashlight, Pause, RefreshCw, Send } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';

export type PatternOption = { idx: number; label: string };

type LampProps = {
  status: any;
  brightness: number;
  pattern: number;
  patternOptions: PatternOption[];
  patternSpeed: number;
  patternFade: number;
  fadeEnabled: boolean;
  rampOn?: number;
  rampOff?: number;
  lampOn: boolean;
  onSync: () => void;
  onLampToggle: (next: boolean) => void;
  onBrightnessChange: (val: number) => void;
  onPatternChange: (val: number) => void;
  onPrev: () => void;
  onNext: () => void;
  onAutoCycle: (enabled: boolean) => void;
  onPatternSpeed: (val: number) => void;
  onPatternFade: (val: number, enable: boolean) => void;
  onRampOn: (val: number) => void;
  onRampOff: (val: number) => void;
  profileSlot: string;
  setProfileSlot: (val: string) => void;
  onProfileLoad: () => void;
  onProfileSave: () => void;
};

type QuickProps = {
  patternOptions: PatternOption[];
  quickSearch: string;
  setQuickSearch: (v: string) => void;
  quickSelection: number[];
  toggleQuickSelection: (idx: number) => void;
  saveQuickSelection: () => void;
  refreshStatus: () => void;
};

type CustomProps = {
  customCsv: string;
  setCustomCsv: (v: string) => void;
  customStep: number;
  setCustomStep: (v: number) => void;
  applyCustom: () => void;
  clearCustom: () => void;
  reloadCustom: () => void;
  customLen?: number;
  customStepMs?: number;
};

type ProfilesProps = {
  profileSlot: string;
  onProfileSave: () => void;
  onProfileLoad: () => void;
  onBackup: () => void;
  onImport: (text: string) => void;
  cfgText: string;
  setCfgText: (v: string) => void;
  children?: ReactNode;
};

export function LampCard({
  status,
  brightness,
  pattern,
  patternOptions,
  patternSpeed,
  patternFade,
  fadeEnabled,
  rampOn,
  rampOff,
  lampOn,
  onSync,
  onLampToggle,
  onBrightnessChange,
  onPatternChange,
  onPrev,
  onNext,
  onAutoCycle,
  onPatternSpeed,
  onPatternFade,
  onRampOn,
  onRampOff,
  profileSlot,
  setProfileSlot,
  onProfileLoad,
  onProfileSave,
}: LampProps) {
  return (
    <Card>
      <CardHeader>
        <CardTitle>Lamp &amp; Ramps</CardTitle>
        <div className="flex items-center gap-2">
          <Label className="m-0">Profile</Label>
          <select className="input" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
            <option value="1">1</option>
            <option value="2">2</option>
            <option value="3">3</option>
          </select>
          <Button onClick={onProfileLoad}>Load</Button>
          <Button onClick={onProfileSave}>Save</Button>
        </div>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex flex-wrap items-center gap-3">
          <span className="chip-muted">Switch: {status.switchState ?? '--'}</span>
          <span className="chip-muted">Touch: {status.touchState ?? '--'}</span>
          <Button size="sm" onClick={onSync}>
            <RefreshCw className="mr-1 h-4 w-4" /> Sync
          </Button>
          <Button size="sm" variant="primary" onClick={() => onLampToggle(!lampOn)}>
            {lampOn ? (
              <>
                <Pause className="mr-1 h-4 w-4" /> Off
              </>
            ) : (
              <>
                <Flashlight className="mr-1 h-4 w-4" /> On
              </>
            )}
          </Button>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <span className="text-sm text-muted">Brightness</span>
              <Input
                type="number"
                min={1}
                max={100}
                value={brightness}
                onChange={(e) => onBrightnessChange(Number(e.target.value))}
                onBlur={(e) => onBrightnessChange(Number(e.target.value))}
                className="w-24 text-right"
                suffix="%"
              />
            </div>
            <input
              type="range"
              min="1"
              max="100"
              value={brightness}
              onChange={(e) => onBrightnessChange(Number(e.target.value))}
              className="w-full accent-accent"
            />
            <div className="flex gap-2">
              <Button onClick={onPrev}>
                <ArrowLeftCircle className="mr-1 h-4 w-4" /> Prev
              </Button>
              <select className="input" value={pattern} onChange={(e) => onPatternChange(parseInt(e.target.value, 10))}>
                {patternOptions.map((p) => (
                  <option key={p.idx} value={p.idx}>
                    {p.idx === status.currentPattern ? `${p.label} (active)` : p.label}
                  </option>
                ))}
              </select>
              <Button onClick={onNext}>
                Next <ArrowRightCircle className="ml-1 h-4 w-4" />
              </Button>
            </div>
            <div className="flex flex-wrap gap-3">
              <div className="flex items-center gap-2">
                <Label className="m-0 text-muted">Speed</Label>
                <Input
                  type="number"
                  min={0.1}
                  max={5}
                  step={0.1}
                  value={patternSpeed}
                  onChange={(e) => onPatternSpeed(Number(e.target.value))}
                  onBlur={(e) => onPatternSpeed(Number(e.target.value))}
                  className="w-24"
                  suffix="x"
                  description="Pattern speed multiplier"
                />
                <input
                  type="range"
                  min="0.1"
                  max="5"
                  step="0.1"
                  value={patternSpeed}
                  onChange={(e) => onPatternSpeed(Number(e.target.value))}
                  className="accent-accent"
                />
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0 text-muted">Fade</Label>
                <select
                  className="input"
                  value={fadeEnabled ? patternFade : 'off'}
                  onChange={(e) => {
                    const val = e.target.value;
                    if (val === 'off') onPatternFade(1, false);
                    else onPatternFade(parseFloat(val), true);
                  }}
                >
                  <option value="off">Off</option>
                  <option value="0.5">0.5x</option>
                  <option value="1">1x</option>
                  <option value="2">2x</option>
                </select>
              </div>
            </div>
            <div className="flex flex-wrap gap-2">
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" onChange={(e) => onAutoCycle(e.target.checked)} />{' '}
                <span className="inline-flex items-center gap-1">
                  <Activity className="h-4 w-4" /> AutoCycle
                </span>
              </label>
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" disabled /> Pattern Fade
              </label>
            </div>
          </div>
          <div className="space-y-2 text-sm text-muted">
            <p>Profiles moved to header.</p>
          </div>
        </div>

        <div className="grid gap-3 md:grid-cols-2">
          <Card className="p-3">
            <CardTitle className="text-base text-text">Ramp On</CardTitle>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label className="m-0">Ease</Label>
                <select className="input">
                  <option>linear</option>
                  <option>ease</option>
                  <option>ease-in</option>
                  <option>ease-out</option>
                  <option>ease-in-out</option>
                </select>
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0">Power</Label>
                <Input type="number" min="0.01" max="10" step="0.01" defaultValue={2} className="w-24" />
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0">Duration</Label>
                <Input
                  type="number"
                  min={50}
                  max={10000}
                  step={10}
                  value={rampOn ?? ''}
                  onChange={(e) => onRampOn(Number(e.target.value))}
                  onBlur={(e) => {
                    const v = Number(e.target.value);
                    if (!Number.isNaN(v)) onRampOn(v);
                  }}
                  className="w-28"
                  suffix="ms"
                />
                <div className="h-2 w-full rounded bg-border">
                  <div className="h-2 rounded bg-accent" style={{ width: `${Math.min(100, Math.max(5, ((rampOn || 0) / 5000) * 100))}%` }} />
                </div>
              </div>
            </div>
          </Card>
          <Card className="p-3">
            <CardTitle className="text-base text-text">Ramp Off</CardTitle>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label className="m-0">Ease</Label>
                <select className="input">
                  <option>linear</option>
                  <option>ease</option>
                  <option>ease-in</option>
                  <option>ease-out</option>
                  <option>ease-in-out</option>
                </select>
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0">Power</Label>
                <Input type="number" min="0.01" max="10" step="0.01" defaultValue={2} className="w-24" />
              </div>
              <div className="flex items-center gap-2">
                <Label className="m-0">Duration</Label>
                <Input
                  type="number"
                  min={50}
                  max={10000}
                  step={10}
                  value={rampOff ?? ''}
                  onChange={(e) => onRampOff(Number(e.target.value))}
                  onBlur={(e) => {
                    const v = Number(e.target.value);
                    if (!Number.isNaN(v)) onRampOff(v);
                  }}
                  className="w-28"
                  suffix="ms"
                />
                <div className="h-2 w-full rounded bg-border">
                  <div className="h-2 rounded bg-accent2" style={{ width: `${Math.min(100, Math.max(5, ((rampOff || 0) / 5000) * 100))}%` }} />
                </div>
              </div>
            </div>
          </Card>
        </div>
      </CardContent>
    </Card>
  );
}

export function QuickCustomSection({
  patternOptions,
  quickSearch,
  setQuickSearch,
  quickSelection,
  toggleQuickSelection,
  saveQuickSelection,
  refreshStatus,
  customCsv,
  setCustomCsv,
  customStep,
  setCustomStep,
  applyCustom,
  clearCustom,
  reloadCustom,
  customLen,
  customStepMs,
}: QuickProps & CustomProps) {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader className="items-start">
          <CardTitle>Quick Tap Modes</CardTitle>
          <Input placeholder="Search pattern" value={quickSearch} onChange={(e) => setQuickSearch(e.target.value)} className="w-40" />
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex flex-wrap gap-2 max-h-48 overflow-y-auto">
            {patternOptions
              .filter((p) => p.label.toLowerCase().includes(quickSearch.toLowerCase()))
              .map((p) => (
                <label key={p.idx} className="pill cursor-pointer whitespace-nowrap">
                  <input type="checkbox" className="accent-accent" checked={quickSelection.includes(p.idx)} onChange={() => toggleQuickSelection(p.idx)} /> {p.label}
                </label>
              ))}
          </div>
          <div className="flex gap-2">
            <Button onClick={saveQuickSelection}>Save quick list</Button>
            <Button onClick={refreshStatus}>
              <RefreshCw className="mr-1 h-4 w-4" /> Reload
            </Button>
          </div>
          <p className="text-sm text-muted">Tap the physical switch to cycle through selected quick modes.</p>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Custom Pattern</CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <Label>Values (0..1 CSV)</Label>
          <textarea className="input w-full" rows={3} placeholder="0.2,0.8,1.0,0.4" value={customCsv} onChange={(e) => setCustomCsv(e.target.value)} />
          <div className="flex items-center gap-2">
            <Label className="m-0">Step (ms)</Label>
            <Input
              type="number"
              min={20}
              max={2000}
              step={20}
              value={customStep}
              onChange={(e) => setCustomStep(Number(e.target.value))}
              className="w-28"
              suffix="ms"
              description="Step duration for each custom value"
            />
            <Button onClick={() => applyCustom()}>Set step</Button>
          </div>
          <div className="flex gap-2">
            <Button variant="primary" onClick={applyCustom}>
              Apply
            </Button>
            <Button onClick={clearCustom}>Clear</Button>
            <Button onClick={reloadCustom}>Reload</Button>
          </div>
          <p className="text-sm text-muted">
            Current: len={customLen ?? '--'} step={customStepMs ?? '--'}ms
          </p>
        </CardContent>
      </Card>
    </div>
  );
}

export function ProfilesSection({ profileSlot, onProfileLoad, onProfileSave, onBackup, onImport, cfgText, setCfgText }: ProfilesProps) {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle>Import / Export</CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2">
            <Button onClick={onBackup}>
              <Copy className="mr-1 h-4 w-4" /> cfg export
            </Button>
            <Input placeholder="cfg import key=val ..." value={cfgText} onChange={(e) => setCfgText(e.target.value)} />
            <Button onClick={() => cfgText.trim() && onImport(cfgText)}>
              <ClipboardPaste className="mr-1 h-4 w-4" /> Import
            </Button>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Profiles</CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2 flex-wrap">
            <Button onClick={onProfileSave}>Save Profile {profileSlot}</Button>
            <Button onClick={onProfileLoad}>Load Profile {profileSlot}</Button>
            <Button onClick={onBackup}>Backup Settings</Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
