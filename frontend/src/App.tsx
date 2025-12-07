import { useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { HelpCircle, Home, LogOut, RefreshCw, Send, Settings, Wand2, Zap } from 'lucide-react';
import { ToastContainer } from 'react-toastify';
import 'react-toastify/dist/ReactToastify.css';
import { useConnection } from './context/connection';
import { Trans, useI18n } from './i18n';
import { HomeSection } from './sections/HomeSection';
import { SettingsSection } from './sections/SettingsSection';
import { ActionsSection } from './sections/ActionsSection';
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
    connect,
    connectBle,
    connectSerial,
    disconnect,
    refreshStatus,
    sendCmd,
  } = useConnection();
  const [activeTab, setActiveTab] = useState<'home' | 'settings' | 'actions' | 'help'>('home');
  const [logOpen, setLogOpen] = useState(false);
  const [commandInput, setCommandInput] = useState('');

  const iconHref = `${import.meta.env.BASE_URL}icon-lamp.svg`;
  const logLines = log.slice(-150);

  const tabs: { key: 'home' | 'settings' | 'actions' | 'help'; label: string; icon: JSX.Element }[] = [
    { key: 'home', label: t('nav.home', 'Home'), icon: <Home className="h-4 w-4" /> },
    { key: 'settings', label: t('nav.settings', 'Settings'), icon: <Settings className="h-4 w-4" /> },
    { key: 'actions', label: t('nav.actions', 'Extras'), icon: <Wand2 className="h-4 w-4" /> },
    { key: 'help', label: t('nav.help', 'Help'), icon: <HelpCircle className="h-4 w-4" /> },
  ];

  return (
    <div className="min-h-screen bg-bg text-text pb-36">
      <header className="sticky top-0 z-10 bg-gradient-to-br from-[#111a2d] via-[#0b0f1a] to-[#0b0f1a] px-4 py-3 shadow-lg shadow-black/40">
        <div className="mx-auto flex max-w-6xl flex-col gap-3">
          <div className="flex flex-wrap items-center gap-3">
            <img src={iconHref} alt="Lamp Icon" className="h-10 w-10 rounded-lg border border-border bg-[#0b0f1a] p-1.5" />
            <div className="flex flex-1 flex-col">
              <h1 className="text-xl font-semibold tracking-wide"><Trans k="title.app">Quarzlampe</Trans></h1>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              {status.connected ? (
                <Button onClick={disconnect}>
                  <LogOut className="mr-1 h-4 w-4" /> {t('btn.disconnect', 'Disconnect')}
                </Button>
              ) : (
                <>
                  <Button variant="primary" onClick={connectBle} disabled={status.connecting}>
                    {status.connecting ? 'Connecting…' : (
                      <span className="flex items-center gap-1"><Zap className="h-4 w-4" /> {t('btn.connect', 'Connect BLE')}</span>
                    )}
                  </Button>
                  <Button onClick={connectSerial} disabled={status.connecting}>
                    <Zap className="mr-1 h-4 w-4" /> <Trans k="btn.connectSerial">Connect Serial</Trans>
                  </Button>
                </>
              )}
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            {tabs.map((tab) => (
              <Button
                key={tab.key}
                variant={activeTab === tab.key ? 'primary' : 'ghost'}
                onClick={() => setActiveTab(tab.key)}
                className={`flex items-center gap-2 ${activeTab === tab.key ? 'bg-accent/10 ring-1 ring-accent' : ''}`}
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
        {activeTab === 'home' && <HomeSection />}
        {activeTab === 'settings' && <SettingsSection />}
        {activeTab === 'actions' && <ActionsSection />}
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
            <div className="flex items-center gap-2 text-xs text-muted">
              <span>{status.connected ? t('status.connected', 'Connected') : t('status.disconnected', 'Not connected')}</span>
              <span>•</span>
              <span>{t('status.last', 'Last status')}: {status.lastStatusAt ? new Date(status.lastStatusAt).toLocaleTimeString() : '--'}</span>
            </div>
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
      <ToastContainer position="bottom-right" newestOnTop closeOnClick pauseOnFocusLoss draggable theme="dark" />
    </div>
  );
}
