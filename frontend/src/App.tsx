import { useCallback, useEffect, useRef, useState } from 'react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Bluetooth, HelpCircle, Home, LogOut, RefreshCw, Send, Settings, Wand2, Wrench, Zap, Sliders } from 'lucide-react';
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
import { FilterSection } from './sections/FilterSection';

const bleGuids = [
  { label: 'Service', value: 'd94d86d7-1eaf-47a4-9d1e-7a90bf34e66b' },
  { label: 'Command Char', value: '4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7' },
  { label: 'Status Char', value: 'c5ad78b6-9b77-4a96-9a42-8e6e9a40c123' },
];

const commands = [
  { cmd: 'on / off / toggle', desc: 'Switch lamp power' },
  { cmd: 'mode N / next / prev', desc: 'Select pattern' },
  { cmd: 'auto on/off', desc: 'Enable auto-cycle' },
  { cmd: 'quick <csv|default>', desc: 'Set quick-list' },
  { cmd: 'bri X / bri min/max X', desc: 'Brightness and limits' },
  { cmd: 'ramp <ms> / ramp on/off <ms>', desc: 'Ramp durations' },
  { cmd: 'ramp ease on|off <type> [pow]', desc: 'Ramp easing curve' },
  { cmd: 'pat scale X / pat fade on|off / pat fade amt X', desc: 'Pattern speed & fade' },
  { cmd: 'idleoff <min>', desc: 'Idle auto-off timer' },
  { cmd: 'wake [soft] [mode=n bri=x] <s>', desc: 'Wake fade' },
  { cmd: 'sleep <min> / sleep stop', desc: 'Sleep fade' },
  { cmd: 'sos [stop]', desc: 'SOS alert' },
  { cmd: 'notify ... / morse <text>', desc: 'Blink/morse' },
  { cmd: 'demo [s] / demo off', desc: 'Cycle quick-list with dwell' },
  { cmd: 'touch tune on off / touchdim on/off / touch hold ms', desc: 'Touch config' },
  { cmd: 'clap on/off / clap thr / cool / train', desc: 'Clap/audio control' },
  { cmd: 'presence on/off/set/clear/grace', desc: 'Presence auto on/off' },
  { cmd: 'light on/off/calib/gain/clamp', desc: 'Ambient light (if built)' },
  { cmd: 'music sens / music smooth / music auto on|off|thr', desc: 'Music params (use patterns Music Direct/Beat)' },
  { cmd: 'poti on/off/alpha/delta/off/sample', desc: 'Brightness knob (if built)' },
  { cmd: 'push on/off/debounce/double/hold/step_ms/step', desc: 'Push button (if built)' },
  { cmd: 'custom v1,v2.. / custom step ms', desc: 'Custom pattern' },
  { cmd: 'profile save/load', desc: 'User profiles' },
  { cmd: 'cfg export / cfg import ...', desc: 'Backup/restore settings' },
  { cmd: 'status / status json', desc: 'Print status snapshot' },
  { cmd: 'factory / help', desc: 'Reset or list commands' }
];

const midiMapping = [
  { msg: 'CC 7', action: 'Set brightness 0–100%' },
  { msg: 'CC 20', action: 'Set pattern/mode 1–8 (scaled)' },
  { msg: 'Note 59 (B3)', action: 'Toggle lamp' },
  { msg: 'Note 60 (C4)', action: 'Previous pattern' },
  { msg: 'Note 62 (D4)', action: 'Next pattern' },
  { msg: 'Notes 70–77 (F#4..C5)', action: 'Select pattern/mode 1–8' },
];

export default function App() {
  const { t, lang, setLang } = useI18n();
  const {
    status,
    deviceName,
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
    setLog,
  } = useConnection();
  const [activeTab, setActiveTab] = useState<'home' | 'settings' | 'advanced' | 'actions' | 'filters' | 'help'>('home');
  const [logOpen, setLogOpen] = useState(false);
  const [commandInput, setCommandInput] = useState('');
  const [commandHistory, setCommandHistory] = useState<string[]>([]);
  const [showConnectOverlay, setShowConnectOverlay] = useState(true);

  const iconHref = `${import.meta.env.BASE_URL}icon-lamp.svg`;
  const headerTitle =
    status.connected && (status.deviceName || deviceName)
      ? (() => {
          const n = status.deviceName || deviceName;
          if (!n) return 'Quarzlampe';
          const lower = n.toLowerCase();
          const uuidLike = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(n.trim());
          if (n.trim().startsWith('{') || lower.includes('bluetoothserviceclassid') || uuidLike) return 'Quarzlampe';
          return n;
        })()
      : undefined;
  const logLines = log.slice(-150);
  const logRef = useRef<HTMLDivElement | null>(null);
  const lastStatusAge = status.lastStatusAt ? Date.now() - status.lastStatusAt : null;
  const isStale = status.connected && lastStatusAge !== null && lastStatusAge > 25000; // 25s without update
  const [lastPulseTs, setLastPulseTs] = useState<number | null>(null);
  const lastPulseRef = useRef<number>(0);

  useEffect(() => {
    if (!status.connected) setShowConnectOverlay(true);
  }, [status.connected]);

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

  useEffect(() => {
    if (status.connected) setShowConnectOverlay(false);
  }, [status.connected]);

  useEffect(() => {
    if (status.lastStatusAt) {
      const now = Date.now();
      if (now - lastPulseRef.current > 1500) {
        lastPulseRef.current = now;
        setLastPulseTs(now);
      }
    }
  }, [status.lastStatusAt]);

  useEffect(() => {
    if (liveLog && logRef.current) {
      logRef.current.scrollTop = logRef.current.scrollHeight;
    }
  }, [logLines.length, liveLog]);

  const handleCommandSend = useCallback(
    (val: string) => {
      const trimmed = val.trim();
      if (!trimmed) return;
      sendCmd(trimmed);
      setCommandHistory((prev) => [...prev.slice(-99), trimmed]);
      // History position stays at "end" after send; next ArrowUp starts from latest
      setCommandInput('');
    },
    [sendCmd],
  );

  const tabs: { key: 'home' | 'settings' | 'advanced' | 'actions' | 'filters' | 'help'; label: string; icon: JSX.Element }[] = [
    { key: 'home', label: t('nav.home', 'Home'), icon: <Home className="h-4 w-4" /> },
    { key: 'settings', label: t('nav.settings', 'Settings'), icon: <Settings className="h-4 w-4" /> },
    { key: 'advanced', label: t('nav.hardware', 'Hardware'), icon: <Wrench className="h-4 w-4" /> },
    { key: 'filters', label: t('nav.filters', 'Filters'), icon: <Sliders className="h-4 w-4" /> },
    { key: 'actions', label: t('nav.actions', 'Extras'), icon: <Wand2 className="h-4 w-4" /> },
    { key: 'help', label: t('nav.help', 'Help'), icon: <HelpCircle className="h-4 w-4" /> },
  ];

  return (
    <>
      <div className="min-h-screen bg-bg text-text">
        <AnimatePresence>
          {showConnectOverlay && !status.connected && (
            <motion.div
              key="connect-overlay"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-md px-3"
            >
              <motion.div
                initial={{ scale: 0.9, opacity: 0 }}
                animate={{ scale: 1, opacity: 1 }}
                exit={{ scale: 0.95, opacity: 0 }}
                transition={{ duration: 0.3, ease: "easeOut" }}
                className="w-full max-w-lg rounded-3xl border border-border/80 bg-panel/90 p-6 shadow-2xl"
              >
                <h2 className="mb-4 text-center text-2xl font-semibold text-text drop-shadow">
                  <Trans k="title.app">Quarzlampe</Trans>
                </h2>
                <p className="mb-6 text-center text-muted text-sm">
                  Verbinden oder überspringen, um die App anzuschauen.
                </p>
                <div className="grid gap-3 sm:grid-cols-2">
                  <Button
                    size="md"
                    variant="primary"
                    className="h-16 text-base"
                    onClick={() => {
                      setShowConnectOverlay(false);
                      connectBle();
                    }}
                  >
                    <Bluetooth className="mr-2 h-6 w-6" /> BLE verbinden
                  </Button>
                  <Button
                    size="md"
                    className="h-16 text-base"
                    onClick={() => {
                      setShowConnectOverlay(false);
                      connectSerial();
                    }}
                  >
                    <Zap className="mr-2 h-6 w-6" /> BT/Serial verbinden
                  </Button>
                </div>
                <div className="mt-6 text-center">
                  <Button
                    variant="ghost"
                    onClick={() => setShowConnectOverlay(false)}
                  >
                    Skip
                  </Button>
                </div>
              </motion.div>
            </motion.div>
          )}
        </AnimatePresence>
        <header className="sticky top-0 z-10 bg-header backdrop-blur-sm px-4 py-3 shadow-lg shadow-black/20">
          <div className="mx-auto flex max-w-6xl flex-col gap-3">
            <div className="flex flex-wrap items-center gap-3">
              <img
                src={iconHref}
                alt="Lamp Icon"
                className="h-10 w-10 rounded-lg border border-border bg-panel p-1.5"
              />
              <div className="flex flex-1 flex-col">
                <h1 className="text-xl font-semibold tracking-wide">
                  {headerTitle || <Trans k="title.app">Quarzlampe</Trans>}
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
              {/* <div className="flex items-center gap-2">
                 <Button
  variant="ghost"
  size="sm"
  style={{
    borderRadius:"50%"
  }}
  className="rounded-full p-2 h-9 w-9 justify-center"
  onClick={refreshStatus}
>
   <span
                    className={`inline-flex h-3 w-3 rounded-full ${
                      status.patternCount > 0 &&
                      status.brightness !== undefined &&
                      status.rampOnMs !== undefined &&
                      status.rampOffMs !== undefined &&
                      status.customLen !== undefined &&
                      status.customStepMs !== undefined
                        ? "bg-sky-300 shadow-[0_0_12px_rgba(125,211,252,0.7)]"
                        : "bg-slate-500/60"
                    }`}
                    title="Settings loaded"
                  />
                </Button>
              </div> */}
            </div>
          </div>
        </header>

        <main className="relative mx-auto max-w-6xl px-4 py-4 pb-32">
          <AnimatePresence mode="wait">
            <motion.div
              key={activeTab}
              initial={{ opacity: 0, y: 18, scale: 0.96, rotate: -0.4 }}
              animate={{
                opacity: 1,
                y: 0,
                scale: 1,
                rotate: 0,
                transition: { duration: 0.55, ease: [0.19, 1, 0.22, 1] },
              }}
              exit={{
                opacity: 0,
                y: -14,
                scale: 0.985,
                rotate: 0.35,
                transition: { duration: 0.32, ease: [0.4, 0, 0.7, 0.2] },
              }}
              className="space-y-4"
            >
              {activeTab === "home" && <HomeSection />}
              {activeTab === "settings" && <SettingsSection />}
              {activeTab === "advanced" && <AdvancedSection />}
              {activeTab === "filters" && <FilterSection />}
              {activeTab === "actions" && <ActionsSection />}
              {activeTab === "help" && (
                <HelpSection
                  bleGuids={bleGuids}
                  commands={commands}
                  midi={midiMapping}
                />
              )}
            </motion.div>
          </AnimatePresence>
          <motion.div
            key={`${activeTab}-glow`}
            className="pointer-events-none absolute -inset-10 -z-10 hidden md:block"
            initial={{ opacity: 0, scale: 0.9, rotate: -2 }}
            animate={{
              opacity: 0.75,
              scale: 1,
              rotate: 0,
              transition: { duration: 0.8, ease: "easeOut" },
            }}
            exit={{
              opacity: 0,
              scale: 0.95,
              rotate: 1,
              transition: { duration: 0.35 },
            }}
            style={{
              background:
                "radial-gradient(65% 45% at 50% 35%, rgba(var(--accent-rgb),0.22), transparent 60%)," +
                "radial-gradient(40% 35% at 25% 65%, rgba(130,200,255,0.12), transparent 65%)," +
                "radial-gradient(35% 30% at 75% 70%, rgba(255,180,120,0.12), transparent 70%)",
              filter: "blur(46px)",
            }}
          />
          <motion.div
            key={`${activeTab}-facet`}
            className="pointer-events-none absolute -inset-12 -z-20 hidden md:block"
            initial={{ opacity: 0, scale: 1 }}
            animate={{
              opacity: 0.35,
              scale: 1,
              transition: { duration: 1.1, ease: "easeOut" },
            }}
            exit={{ opacity: 0, transition: { duration: 0.4 } }}
            style={{
              background:
                "conic-gradient(from 120deg at 50% 40%, rgba(255,255,255,0.08), rgba(var(--accent-rgb),0.05), rgba(255,200,150,0.06), rgba(0,0,0,0.04), rgba(255,255,255,0.08))",
              mixBlendMode: "screen",
              filter: "blur(30px)",
            }}
          />
        </main>

        <footer className="sticky bottom-0 z-20 border-t border-border/60 backdrop-blur-sm relative">
          <div
            className="pointer-events-none absolute -top-3 inset-x-0 h-4 bg-gradient-to-t from-bg/0 via-bg/0 to-transparent"
            aria-hidden
          />
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
                {log.length > 0 && (
                  <span className="text-xs text-muted">
                    ({log.length} {t("log.lines", "lines")})
                  </span>
                )}
              </div>
              <div className="flex items-center gap-2 text-xs text-muted">
                <span>
                  {status.connected
                    ? isStale
                      ? `${t("status.connected", "Connected")} (no status)`
                      : t("status.connected", "Connected")
                    : t("status.disconnected", "Not connected")}
                </span>
                <span>•</span>
                <button
                  type="button"
                  className="relative inline-flex h-7 w-7 items-center justify-center rounded-full border border-border/70 bg-panel/60 text-xs font-semibold overflow-hidden"
                  title={
                    status.lastStatusAt
                      ? new Date(status.lastStatusAt).toLocaleTimeString()
                      : t("status.last", "Last status")
                  }
                  onClick={(e) => {e.stopPropagation();e.preventDefault();refreshStatus()}}
                >
                  <span className="sr-only">Status</span>
                  <span
                    className={`absolute inset-[6px] rounded-full ${
                      status.connected
                        ? isStale
                          ? "bg-amber-500/40"
                          : "bg-green-500/40"
                        : status.connecting
                        ? "bg-yellow-500/30"
                        : "bg-red-500/30"
                    }`}
                  />
                  <span className="absolute inset-0 rounded-full bg-accent/10 blur" />
                  {lastPulseTs && lastStatusAge !== null && Date.now() - lastPulseTs < 1200 && (
                    <span className="absolute inset-0 animate-ping rounded-full bg-accent/35 blur-sm" />
                  )}
                </button>
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
                  <Button size="sm" variant="ghost" onClick={() => setLog([])}>
                    {t("btn.clear", "Clear")}
                  </Button>
                </div>
                <div
                  ref={logRef}
                  className="max-h-64 overflow-y-auto rounded-lg border border-border bg-bg p-3 font-mono text-sm text-accent space-y-1 shadow-inner"
                >
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
                        e.preventDefault();
                        handleCommandSend(commandInput);
                      } else if (e.key === "ArrowUp") {
                        e.preventDefault();
                        // up: walk backward through history
                        setCommandInput((current) => {
                          const idx = commandHistory.lastIndexOf(current);
                          const startIdx =
                            idx >= 0 ? idx - 1 : commandHistory.length - 1;
                          const nextIdx = Math.max(startIdx, 0);
                          return commandHistory[nextIdx] ?? current;
                        });
                      } else if (e.key === "ArrowDown") {
                        e.preventDefault();
                        // down: walk forward; if past the end, clear
                        const idx = commandHistory.indexOf(commandInput);
                        const nextIdx =
                          idx >= 0 ? idx + 1 : commandHistory.length;
                        setCommandInput(commandHistory[nextIdx] ?? "");
                      }
                    }}
                  />
                  <Button
                    variant="primary"
                    size="sm"
                    className="rounded-none border-l border-border"
                    onClick={() => handleCommandSend(commandInput)}
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
    </>
  );
}
