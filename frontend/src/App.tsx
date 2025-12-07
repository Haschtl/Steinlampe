import { useEffect, useMemo, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { HelpCircle, Home, LogOut, RefreshCw, Send, Settings, Wand2, Zap } from 'lucide-react';
import { patternLabels } from './data/patterns';
import { useConnection } from './context/connection';
import { Trans, useI18n } from './i18n';
import { LampCard, PatternOption, ProfilesSection, QuickCustomSection } from './sections/HomeSection';
import { SettingsCard, PresenceTouchCard, LightMusicCard } from './sections/SettingsSection';
import { NotifyCard, WakeSleepCard } from './sections/ActionsSection';
import { HelpSection } from './sections/HelpSection';

const bleGuids = [
  { label: 'Service', value: 'd94d86d7-1eaf-47a4-9d1e-7a90bf34e66b' },
  { label: 'Command Char', value: '4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7' },
  { label: 'Status Char', value: 'c5ad78b6-9b77-4a96-9a42-8e6e9a40c123' },
];

const commands = [
  { cmd: 'on / off / toggle', desc: 'Switch lamp power' },
  { cmd: 'mode N / next / prev', desc: 'Select pattern' },
  { cmd: 'bri X', desc: 'Brightness 0..100%' },
  { cmd: 'bri cap X', desc: 'Brightness cap (%)' },
  { cmd: 'ramp on/off <ms>', desc: 'Set ramp durations' },
  { cmd: 'wake [soft] <s>', desc: 'Wake fade' },
  { cmd: 'sleep <min>', desc: 'Sleep fade' },
  { cmd: 'notify ...', desc: 'Blink sequence' },
  { cmd: 'morse <text>', desc: 'Morse blink' },
  { cmd: 'profile save/load', desc: 'User profiles' },
];

export default function App() {
  const { t, lang, setLang } = useI18n();
  const {
    status,
    log,
    liveLog,
    setLiveLog,
    autoReconnect,
    setAutoReconnect,
    connect,
    disconnect,
    refreshStatus,
    sendCmd,
  } = useConnection();
  const [brightness, setBrightness] = useState(70);
  const [cap, setCap] = useState(100);
  const [pattern, setPattern] = useState(1);
  const [notifySeq, setNotifySeq] = useState('80 40 80 120');
  const [notifyFade, setNotifyFade] = useState(0);
  const [notifyRepeat, setNotifyRepeat] = useState(1);
  const [wakeDuration, setWakeDuration] = useState(180);
  const [wakeMode, setWakeMode] = useState('');
  const [wakeBri, setWakeBri] = useState('');
  const [wakeSoft, setWakeSoft] = useState(false);
  const [sleepMinutes, setSleepMinutes] = useState(15);
  const [idleMinutes, setIdleMinutes] = useState(0);
  const [profileSlot, setProfileSlot] = useState('1');
  const [commandInput, setCommandInput] = useState('');
  const [lampOn, setLampOn] = useState(false);
  const [patternSpeed, setPatternSpeed] = useState(1.0);
  const [patternFade, setPatternFade] = useState(1.0);
  const [fadeEnabled, setFadeEnabled] = useState(false);
  const [rampOn, setRampOn] = useState<number | undefined>();
  const [rampOff, setRampOff] = useState<number | undefined>();
  const [pwmCurve, setPwmCurve] = useState(1.8);
  const [quickSearch, setQuickSearch] = useState('');
  const [quickSelection, setQuickSelection] = useState<number[]>([]);
  const [customCsv, setCustomCsv] = useState('');
  const [customStep, setCustomStep] = useState(400);
  const [cfgExportText, setCfgExportText] = useState('');
  const [activeTab, setActiveTab] = useState<'home' | 'settings' | 'actions' | 'help'>('home');
  const [logOpen, setLogOpen] = useState(false);

  useEffect(() => {
    if (typeof status.brightness === 'number') setBrightness(Math.round(status.brightness));
  }, [status.brightness]);

  useEffect(() => {
    if (typeof status.cap === 'number') setCap(Math.round(status.cap));
  }, [status.cap]);

  useEffect(() => {
    if (status.currentPattern) setPattern(status.currentPattern);
  }, [status.currentPattern]);

  useEffect(() => {
    setLampOn(status.lampState === 'ON');
  }, [status.lampState]);

  useEffect(() => {
    if (typeof status.patternSpeed === 'number') setPatternSpeed(status.patternSpeed);
  }, [status.patternSpeed]);

  useEffect(() => {
    if (typeof status.patternFade === 'number') {
      setPatternFade(status.patternFade);
      setFadeEnabled(true);
    }
  }, [status.patternFade]);

  useEffect(() => {
    if (typeof status.rampOnMs === 'number') setRampOn(status.rampOnMs);
    if (typeof status.rampOffMs === 'number') setRampOff(status.rampOffMs);
  }, [status.rampOnMs, status.rampOffMs]);

  useEffect(() => {
    if (status.quickCsv) {
      const nums = status.quickCsv
        .split(',')
        .map((n) => parseInt(n.trim(), 10))
        .filter((n) => !Number.isNaN(n));
      setQuickSelection(nums);
    }
  }, [status.quickCsv]);

  const patternOptions = useMemo(() => {
    const count = status.patternCount || patternLabels.length;
    return Array.from({ length: count }, (_, i) => ({
      idx: i + 1,
      label: patternLabels[i] ? `${i + 1} - ${patternLabels[i]}` : `Pattern ${i + 1}`,
    }));
  }, [status.patternCount]);

  const handleBrightness = (value: number) => {
    const clamped = Math.min(100, Math.max(1, Math.round(value)));
    setBrightness(clamped);
    sendCmd(`bri ${clamped}`).catch((e) => console.warn(e));
  };

  const handleCap = () => {
    const clamped = Math.min(100, Math.max(1, Math.round(cap)));
    setCap(clamped);
    sendCmd(`bri cap ${clamped}`).catch((e) => console.warn(e));
  };

  const handleLampToggle = (next: boolean) => {
    setLampOn(next);
    sendCmd(next ? 'on' : 'off').catch((e) => console.warn(e));
  };

  const handlePatternChange = (val: number) => {
    setPattern(val);
    sendCmd(`mode ${val}`).catch((e) => console.warn(e));
  };

  const buildNotifyCmd = (seq: string, fade?: number, repeat = 1) => {
    const parts = seq
      .split(/\s+/)
      .map((n) => parseInt(n, 10))
      .filter((n) => !Number.isNaN(n) && n > 0);
    if (!parts.length) return '';
    const expanded: number[] = [];
    for (let i = 0; i < repeat; i += 1) expanded.push(...parts);
    let cmd = `notify ${expanded.join(' ')}`;
    if (fade && fade > 0) cmd += ` fade=${fade}`;
    return cmd;
  };

  const handleNotify = (seq: string, fade?: number, repeat = 1) => {
    const cmd = buildNotifyCmd(seq, fade, repeat);
    if (!cmd) return;
    sendCmd(cmd).catch((e) => console.warn(e));
  };

  const handleWake = () => {
    const parts = ['wake'];
    if (wakeSoft) parts.push('soft');
    if (wakeMode.trim()) parts.push(`mode=${wakeMode.trim()}`);
    if (wakeBri.trim()) parts.push(`bri=${wakeBri.trim()}`);
    parts.push(String(Math.max(1, wakeDuration || 1)));
    sendCmd(parts.join(' ')).catch((e) => console.warn(e));
  };

  const handlePatternSpeed = (val: number) => {
    const clamped = Math.max(0.1, Math.min(5, val));
    setPatternSpeed(clamped);
    sendCmd(`pat scale ${clamped.toFixed(2)}`).catch((e) => console.warn(e));
  };

  const handlePatternFade = (val: number, enable: boolean) => {
    setPatternFade(val);
    setFadeEnabled(enable);
    if (!enable) {
      sendCmd('pat fade off').catch((e) => console.warn(e));
    } else {
      sendCmd(`pat fade on ${val.toFixed(2)}`).catch((e) => console.warn(e));
    }
  };

  const toggleQuickSelection = (idx: number) => {
    setQuickSelection((prev) => {
      if (prev.includes(idx)) {
        return prev.filter((n) => n !== idx);
      }
      return [...prev, idx].sort((a, b) => a - b);
    });
  };

  const saveQuickSelection = () => {
    if (quickSelection.length === 0) return;
    sendCmd(`quick ${quickSelection.join(',')}`).catch((e) => console.warn(e));
  };

  const applyCustom = () => {
    if (customCsv.trim()) sendCmd(`custom ${customCsv.replace(/\s+/g, '')}`);
    if (customStep) sendCmd(`custom step ${customStep}`);
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

  const iconHref = `${import.meta.env.BASE_URL}icon-lamp.svg`;
  const logLines = log.slice(-150);

  const tabs: { key: 'home' | 'settings' | 'actions' | 'help'; label: string; icon: JSX.Element }[] = [
    { key: 'home', label: t('tab.home', 'Home'), icon: <Home className="h-4 w-4" /> },
    { key: 'settings', label: t('tab.settings', 'Settings'), icon: <Settings className="h-4 w-4" /> },
    { key: 'actions', label: t('tab.actions', 'Extras'), icon: <Wand2 className="h-4 w-4" /> },
    { key: 'help', label: t('tab.help', 'Help'), icon: <HelpCircle className="h-4 w-4" /> },
  ];

  return (
    <div className="min-h-screen bg-bg text-text pb-36">
      <header className="sticky top-0 z-10 bg-gradient-to-br from-[#111a2d] via-[#0b0f1a] to-[#0b0f1a] px-4 py-3 shadow-lg shadow-black/40">
        <div className="mx-auto flex max-w-6xl flex-col gap-3">
          <div className="flex flex-wrap items-center gap-3">
            <img src={iconHref} alt="Lamp Icon" className="h-10 w-10 rounded-lg border border-border bg-[#0b0f1a] p-1.5" />
            <div className="flex flex-1 flex-col">
              <h1 className="text-xl font-semibold tracking-wide"><Trans k="title.app">Quarzlampe Control</Trans></h1>
              <div className="flex flex-wrap items-center gap-2 text-sm text-muted">
                <span className="pill text-accent2">{status.connected ? `${t('status.connected', 'Connected')} ${status.deviceName ?? ''}` : t('status.disconnected', 'Not connected')}</span>
                <span className="pill">{t('status.last', 'Last status')}: {status.lastStatusAt ? new Date(status.lastStatusAt).toLocaleTimeString() : '--'}</span>
                {status.patternName && <span className="pill">Pattern: {status.patternName}</span>}
                {typeof status.patternSpeed === 'number' && <span className="pill">Speed {status.patternSpeed.toFixed(2)}x</span>}
              </div>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              <Button variant="primary" onClick={connect} disabled={status.connecting}>
                {status.connecting ? 'Connecting…' : (
                  <span className="flex items-center gap-1"><Zap className="h-4 w-4" /> {t('btn.connect', 'Connect BLE')}</span>
                )}
              </Button>
              <Button disabled><Trans k="btn.connectSerial">Connect Serial</Trans></Button>
              {status.connected && (
                <Button onClick={disconnect}>
                  <LogOut className="mr-1 h-4 w-4" /> {t('btn.disconnect', 'Disconnect')}
                </Button>
              )}
              <label className="pill cursor-pointer">
                <input type="checkbox" className="accent-accent" checked={autoReconnect} onChange={(e) => setAutoReconnect(e.target.checked)} />{' '}
                <Trans k="toggle.autoReconnect">Auto-reconnect</Trans>
              </label>
              <select className="input w-24" value={lang} onChange={(e) => setLang(e.target.value as 'en' | 'de')}>
                <option value="en">EN</option>
                <option value="de">DE</option>
              </select>
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            {tabs.map((tab) => (
              <Button
                key={tab.key}
                variant={activeTab === tab.key ? 'primary' : 'ghost'}
                onClick={() => setActiveTab(tab.key)}
                className="flex items-center gap-2"
              >
                {tab.icon}
                <span>{tab.label}</span>
              </Button>
            ))}
            <div className="flex-1" />
            <Button size="sm" onClick={refreshStatus}>
              <RefreshCw className="mr-1 h-4 w-4" /> {t('btn.reload', 'Reload')}
            </Button>
          </div>
        </div>
      </header>

      <main className="mx-auto max-w-6xl space-y-4 px-4 py-4">
        {activeTab === 'home' && (
          <div className="space-y-4">
            <LampCard
              status={status}
              brightness={brightness}
              pattern={pattern}
              patternOptions={patternOptions as PatternOption[]}
              patternSpeed={patternSpeed}
              patternFade={patternFade}
              fadeEnabled={fadeEnabled}
              rampOn={rampOn}
              rampOff={rampOff}
              lampOn={lampOn}
              onSync={() => sendCmd('sync')}
              onLampToggle={handleLampToggle}
              onBrightnessChange={handleBrightness}
              onPatternChange={handlePatternChange}
              onPrev={() => sendCmd('prev')}
              onNext={() => sendCmd('next')}
              onAutoCycle={(on) => sendCmd(`auto ${on ? 'on' : 'off'}`)}
              onPatternSpeed={handlePatternSpeed}
              onPatternFade={handlePatternFade}
              onRampOn={(val) => {
                setRampOn(val);
                sendCmd(`ramp on ${val}`);
              }}
              onRampOff={(val) => {
                setRampOff(val);
                sendCmd(`ramp off ${val}`);
              }}
              profileSlot={profileSlot}
              setProfileSlot={setProfileSlot}
              onProfileLoad={() => sendCmd(`profile load ${profileSlot}`)}
              onProfileSave={() => sendCmd(`profile save ${profileSlot}`)}
            />

            <QuickCustomSection
              patternOptions={patternOptions as PatternOption[]}
              quickSearch={quickSearch}
              setQuickSearch={setQuickSearch}
              quickSelection={quickSelection}
              toggleQuickSelection={toggleQuickSelection}
              saveQuickSelection={saveQuickSelection}
              refreshStatus={refreshStatus}
              customCsv={customCsv}
              setCustomCsv={setCustomCsv}
              customStep={customStep}
              setCustomStep={setCustomStep}
              applyCustom={applyCustom}
              clearCustom={() => setCustomCsv('')}
              reloadCustom={() => sendCmd('custom')}
              customLen={status.customLen}
              customStepMs={status.customStepMs}
            />

            <ProfilesSection
              profileSlot={profileSlot}
              onProfileSave={() => sendCmd(`profile save ${profileSlot}`)}
              onProfileLoad={() => sendCmd(`profile load ${profileSlot}`)}
              onBackup={() => sendCmd('cfg export')}
              onImport={(txt) => sendCmd(txt)}
              cfgText={cfgExportText}
              setCfgText={setCfgExportText}
            />
          </div>
        )}

        {activeTab === 'settings' && (
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
        )}

        {activeTab === 'actions' && (
          <div className="space-y-4">
            <WakeSleepCard
              wakeDuration={wakeDuration}
              setWakeDuration={setWakeDuration}
              wakeMode={wakeMode}
              setWakeMode={setWakeMode}
              wakeBri={wakeBri}
              setWakeBri={setWakeBri}
              wakeSoft={wakeSoft}
              setWakeSoft={setWakeSoft}
              handleWake={handleWake}
              sleepMinutes={sleepMinutes}
              setSleepMinutes={setSleepMinutes}
              patternOptions={patternOptions}
              sendCmd={sendCmd}
            />
            <NotifyCard
              notifySeq={notifySeq}
              setNotifySeq={setNotifySeq}
              notifyFade={notifyFade}
              setNotifyFade={setNotifyFade}
              notifyRepeat={notifyRepeat}
              setNotifyRepeat={setNotifyRepeat}
              handleNotify={handleNotify}
              sendCmd={sendCmd}
            />
          </div>
        )}

        {activeTab === 'help' && <HelpSection bleGuids={bleGuids} commands={commands} />}
      </main>

      <footer className="sticky bottom-0 z-20 border-t border-border/60 bg-[#0b0f1a]/95 backdrop-blur">
        <div className="mx-auto max-w-6xl px-4 py-2">
          <button
            type="button"
            onClick={() => setLogOpen((v) => !v)}
            className="flex w-full items-center justify-between rounded-lg border border-border bg-[#0d1425] px-3 py-2 text-left hover:border-accent"
          >
            <div className="flex items-center gap-2">
              <span className="font-semibold"><Trans k="title.log">Log</Trans></span>
              <span className="text-xs text-muted">{logOpen ? t('log.hide', 'Hide') : t('log.show', 'Show')}</span>
              <span className="text-xs text-muted">({log.length} lines)</span>
            </div>
            <span className="text-xs text-muted">{status.connected ? t('status.connected', 'Connected') : t('status.disconnected', 'Not connected')}</span>
          </button>

          {logOpen && (
            <div className="mt-2 space-y-2">
              <div className="flex flex-wrap items-center gap-2">
                <label className="pill cursor-pointer">
                  <input type="checkbox" className="accent-accent" checked={liveLog} onChange={(e) => setLiveLog(e.target.checked)} /> Live log
                </label>
                <Button size="sm" onClick={refreshStatus}>
                  <RefreshCw className="mr-1 h-4 w-4" /> {t('btn.reload', 'Reload status')}
                </Button>
                <Button size="sm" onClick={() => sendCmd('cfg export')}>
                  <Send className="mr-1 h-4 w-4" /> cfg export
                </Button>
              </div>
              <div className="max-h-64 overflow-y-auto rounded-lg border border-border bg-[#0b0f1a] p-3 font-mono text-sm text-accent space-y-1">
                {logLines.length === 0 && <p className="text-muted">Waiting for connection…</p>}
                {logLines.map((l, idx) => (
                  <div key={`${l.ts}-${idx}`} className="flex items-start gap-2 border-b border-border/40 pb-1 last:border-b-0 last:pb-0">
                    <span className="rounded bg-[#10172a] px-2 py-0.5 text-[11px] text-muted">
                      {new Date(l.ts).toLocaleTimeString([], { hour12: false })}
                    </span>
                    <span className="text-accent whitespace-pre-wrap">{l.line}</span>
                  </div>
                ))}
              </div>
              <div className="flex gap-2">
                <Input
                  placeholder={t('input.command', 'Type command')}
                  value={commandInput}
                  onChange={(e) => setCommandInput(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') {
                      const val = commandInput.trim();
                      if (val) {
                        sendCmd(val);
                        setCommandInput('');
                      }
                    }
                  }}
                />
                <Button
                  variant="primary"
                  size="sm"
                  onClick={() => {
                    const val = commandInput.trim();
                    if (val) {
                      sendCmd(val);
                      setCommandInput('');
                    }
                  }}
                >
                  <Send className="mr-1 h-4 w-4" /> {t('btn.send', 'Send')}
                </Button>
              </div>
            </div>
          )}
        </div>
      </footer>
    </div>
  );
}
