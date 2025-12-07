import { useEffect, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Bluetooth, HelpCircle, Home, LogOut, RefreshCw, Send, Settings, Wand2, Wrench, Zap } from 'lucide-react';
import { ToastContainer } from 'react-toastify';
import 'react-toastify/dist/ReactToastify.css';
import { AnimatePresence, motion } from 'framer-motion';
import { useConnection } from './context/connection';
import { Trans, useI18n } from './i18n';
import { HomeSection } from './sections/HomeSection';
import { SettingsSection } from './sections/SettingsSection';
import { ActionsSection } from './sections/ActionsSection';
import { HelpSection } from './sections/HelpSection';
import { AdvancedSection } from './sections/AdvancedSection';

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
    filterParsed,
    setFilterParsed,
    connect,
    connectBle,
    connectSerial,
    disconnect,
    refreshStatus,
    sendCmd,
  } = useConnection();
  const [activeTab, setActiveTab] = useState<'home' | 'settings' | 'advanced' | 'actions' | 'help'>('home');
  const [logOpen, setLogOpen] = useState(false);
  const [commandInput, setCommandInput] = useState('');

  const iconHref = `${import.meta.env.BASE_URL}icon-lamp.svg`;
  const logLines = log.slice(-150);

  useEffect(() => {
    const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const fallback = prefersDark ? 'fancy' : 'fancy-light';
    const theme = localStorage.getItem('ql-theme') || fallback;
    document.body.setAttribute('data-theme', theme);
  }, []);

  useEffect(() => {
    if (!status.connected) return;
    const id = setInterval(() => {
      refreshStatus();
    }, 10000);
    return () => clearInterval(id);
  }, [status.connected, refreshStatus]);

  const tabs: { key: 'home' | 'settings' | 'advanced' | 'actions' | 'help'; label: string; icon: JSX.Element }[] = [
    { key: 'home', label: t('nav.home', 'Home'), icon: <Home className="h-4 w-4" /> },
    { key: 'settings', label: t('nav.settings', 'Settings'), icon: <Settings className="h-4 w-4" /> },
    { key: 'advanced', label: t('nav.hardware', 'Hardware'), icon: <Wrench className="h-4 w-4" /> },
    { key: 'actions', label: t('nav.actions', 'Extras'), icon: <Wand2 className="h-4 w-4" /> },
    { key: 'help', label: t('nav.help', 'Help'), icon: <HelpCircle className="h-4 w-4" /> },
  ];

  return (
    <>
      <div className="min-h-screen bg-bg text-text">
        <header className="sticky top-0 z-10 bg-header px-4 py-3 shadow-lg shadow-black/20">
          <div className="mx-auto flex max-w-6xl flex-col gap-3">
            <div className="flex flex-wrap items-center gap-3">
              <img
                src={iconHref}
                alt="Lamp Icon"
                className="h-10 w-10 rounded-lg border border-border bg-panel p-1.5"
              />
              <div className="flex flex-1 flex-col">
                <h1 className="text-xl font-semibold tracking-wide">
                  <Trans k="title.app">Quarzlampe</Trans>
                </h1>
              </div>
              <div className="flex flex-wrap items-center gap-2">
                {status.connected ? (
                  <Button onClick={disconnect}>
                    <LogOut className="mr-1 h-4 w-4" />{" "}
                    {t("btn.disconnect", "Disconnect")}
                  </Button>
                ) : (
                  <div className="flex overflow-hidden rounded-md border border-border">
                    <Button
                      variant="primary"
                      className="rounded-none border-none border-r border-border"
                      onClick={connectBle}
                      disabled={status.connecting}
                    >
                      <Bluetooth className="mr-1 h-4 w-4" /> BTE
                    </Button>
                    <Button
                      className="rounded-none border-none"
                      onClick={connectSerial}
                      disabled={status.connecting}
                    >
                      <Zap className="mr-1 h-4 w-4" /> Serial (BT)
                    </Button>
                  </div>
                )}
              </div>
            </div>

            <div className="flex flex-wrap items-center gap-2">
              {tabs.map((tab) => (
                <Button
                  key={tab.key}
                  variant={activeTab === tab.key ? "primary" : "ghost"}
                  onClick={() => setActiveTab(tab.key)}
                  className={`flex items-center gap-2 ${
                    activeTab === tab.key
                      ? "bg-accent/10 ring-1 ring-accent"
                      : ""
                  }`}
                >
                  {tab.icon}
                  <span className="hidden sm:inline">{tab.label}</span>
                </Button>
              ))}
              <div className="flex-1" />
              <Button size="sm" onClick={refreshStatus}>
                <RefreshCw className="mr-1 h-4 w-4" />{" "}
                <span className="hidden sm:inline">
                  {t("btn.reload", "Reload")}
                </span>
              </Button>
            </div>
          </div>
        </header>

        <main className="relative mx-auto max-w-6xl px-4 py-4">
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              initial={{ opacity: 0, y: 12, scale: 0.99 }}
              animate={{ opacity: 1, y: 0, scale: 1, transition: { duration: 0.35, ease: [0.16, 1, 0.3, 1] } }}
              exit={{ opacity: 0, y: -8, scale: 0.995, transition: { duration: 0.25, ease: 'easeInOut' } }}
              className="space-y-4"
            >
              {activeTab === "home" && <HomeSection />}
              {activeTab === "settings" && <SettingsSection />}
              {activeTab === "advanced" && <AdvancedSection />}
              {activeTab === "actions" && <ActionsSection />}
              {activeTab === "help" && (
                <HelpSection bleGuids={bleGuids} commands={commands} />
              )}
            </motion.div>
          </AnimatePresence>
          <motion.div
            key={`${activeTab}-glow`}
            className="pointer-events-none absolute -inset-10 -z-10"
            initial={{ opacity: 0, scale: 0.9 }}
            animate={{ opacity: 0.6, scale: 1, transition: { duration: 0.6 } }}
            exit={{ opacity: 0, scale: 0.95, transition: { duration: 0.3 } }}
            style={{
              background: 'radial-gradient(60% 40% at 50% 40%, rgba(var(--accent-rgb),0.18), transparent), radial-gradient(40% 30% at 30% 60%, rgba(140,245,155,0.1), transparent)',
              filter: 'blur(40px)',
            }}
          />
        </main>

        <footer className="sticky bottom-0 z-20 border-t border-border/60 backdrop-blur-sm relative">
          <div className="pointer-events-none absolute -top-3 inset-x-0 h-4 bg-gradient-to-t from-bg/0 via-bg/0 to-transparent" aria-hidden />
          <div className="mx-auto max-w-6xl px-4 py-2">
            <button
              type="button"
              onClick={() => setLogOpen((v) => !v)}
              className="flex w-full items-center justify-between rounded-lg border border-border bg-panel px-3 py-2 text-left hover:border-accent"
            >
              <div className="flex items-center gap-2">
                <span className="font-semibold">
                  <Trans k="title.log">Log</Trans>
                </span>
                <span className="text-xs text-muted">
                  {logOpen ? t("log.hide", "Hide") : t("log.show", "Show")}
                </span>
                <span className="text-xs text-muted">
                  ({log.length} {t("log.lines", "lines")})
                </span>
              </div>
              <div className="flex items-center gap-2 text-xs text-muted">
                <span>
                  {status.connected
                    ? t("status.connected", "Connected")
                    : t("status.disconnected", "Not connected")}
                </span>
                <span>•</span>
                <span
                  className={`inline-flex h-2.5 w-2.5 items-center justify-center rounded-full ${
                    status.connected ? 'bg-green-500/30' : status.connecting ? 'bg-yellow-400/40' : 'bg-red-500/30'
                  }`}
                  title={
                    status.lastStatusAt
                      ? new Date(status.lastStatusAt).toLocaleTimeString()
                      : "--"
                  }
                >
                  <span
                    className={`inline-block h-1.5 w-1.5 rounded-full ${
                      status.connected ? 'bg-green-400' : status.connecting ? 'bg-yellow-300' : 'bg-red-500'
                    } animate-pulse`}
                  />
                </span>
              </div>
            </button>

            {logOpen && (
              <div className="mt-2 space-y-2">
                <div className="flex flex-wrap items-center gap-2">
                  <label className="pill cursor-pointer">
                    <input
                      type="checkbox"
                      className="accent-accent"
                      checked={liveLog}
                      onChange={(e) => setLiveLog(e.target.checked)}
                    />{" "}
                    Live log
                  </label>
                  <label className="pill cursor-pointer">
                    <input
                      type="checkbox"
                      className="accent-accent"
                      checked={filterParsed}
                      onChange={(e) => setFilterParsed(e.target.checked)}
                    />{" "}
                    {t("log.filter", "Filter status lines")}
                  </label>
                </div>
                <div className="max-h-64 overflow-y-auto rounded-lg border border-border bg-bg p-3 font-mono text-sm text-accent space-y-1 shadow-inner">
                  {logLines.length === 0 && (
                    <p className="text-muted">Waiting for connection…</p>
                  )}
                  {logLines.map((l, idx) => (
                    <div
                      key={`${l.ts}-${idx}`}
                      className="flex items-start gap-2 border-b border-border/40 pb-1 last:border-b-0 last:pb-0"
                    >
                      <span className="rounded bg-[#10172a] px-2 py-0.5 text-[11px] text-muted">
                        {new Date(l.ts).toLocaleTimeString([], {
                          hour12: false,
                        })}
                      </span>
                      <span className="text-accent whitespace-pre-wrap">
                        {l.line}
                      </span>
                    </div>
                  ))}
                </div>
                <div className="flex items-stretch overflow-hidden rounded-md border border-border">
                  <Input
                    className="w-full border-none"
                    placeholder={t("input.command", "Type command")}
                    value={commandInput}
                    onChange={(e) => setCommandInput(e.target.value)}
                    onKeyDown={(e) => {
                      if (e.key === "Enter") {
                        const val = commandInput.trim();
                        if (val) {
                          sendCmd(val);
                          setCommandInput("");
                        }
                      }
                    }}
                  />
                  <Button
                    variant="primary"
                    size="sm"
                    className="rounded-none border-l border-border"
                    onClick={() => {
                      const val = commandInput.trim();
                      if (val) {
                        sendCmd(val);
                        setCommandInput("");
                      }
                    }}
                  >
                    <Send className="h-4 w-4" />
                  </Button>
                </div>
              </div>
            )}
          </div>
        </footer>
        <ToastContainer
          position="bottom-right"
          newestOnTop
          closeOnClick
          pauseOnFocusLoss
          draggable
          theme="dark"
        />
      </div>
      <div className="h-36 bg-bg" />
    </>
  );
}
