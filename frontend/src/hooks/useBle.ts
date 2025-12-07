import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { toast } from 'react-toastify';
import { BLE_UUIDS, connectDevice, disconnectDevice, requestDevice, subscribeToLines, writeLine } from './bleClient';

type BleStatus = {
  connected: boolean;
  connecting: boolean;
  deviceName: string;
  lastStatusAt: number | null;
  patternCount: number;
  patternName?: string;
  currentPattern?: number;
  brightness?: number;
  cap?: number;
  lampState?: string;
  switchState?: string;
  touchState?: string;
  autoCycle?: boolean;
  patternSpeed?: number;
  patternFade?: number;
  idleOffMin?: number;
  presence?: string;
  quickCsv?: string;
  rampOnMs?: number;
  rampOffMs?: number;
  customLen?: number;
  customStepMs?: number;
};

type LogEntry = { ts: number; line: string };

type BleApi = {
  status: BleStatus;
  log: LogEntry[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  autoReconnect: boolean;
  setAutoReconnect: (v: boolean) => void;
  connect: () => Promise<void>;
  disconnect: () => void;
  refreshStatus: () => Promise<void>;
  sendCmd: (cmd: string) => Promise<void>;
};

export function useBle(): BleApi {
  const [status, setStatus] = useState<BleStatus>({
    connected: false,
    connecting: false,
    deviceName: '',
    lastStatusAt: null,
    patternCount: 0,
  });
  const [log, setLog] = useState<LogEntry[]>([]);
  const [liveLog, setLiveLog] = useState(true);
  const [autoReconnect, setAutoReconnect] = useState<boolean>(() => {
    const stored = localStorage.getItem('ql-auto-reconnect');
    return stored !== 'false';
  });

  const cmdCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const statusCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const deviceRef = useRef<BluetoothDevice | null>(null);
  const liveLogRef = useRef(true);
  const lastLogRef = useRef<{ line: string; ms: number }>({ line: '', ms: 0 });
  const pendingLogRef = useRef<LogEntry[]>([]);
  const unsubRef = useRef<(() => void)[]>([]);
  const autoReconnectRef = useRef<boolean>(autoReconnect);
  const lastDeviceIdRef = useRef<string | null>(localStorage.getItem('ql-last-device-id'));

  const pushLog = useCallback(
    (line: string) => {
      const now = Date.now();
      if (line === lastLogRef.current.line && now - lastLogRef.current.ms < 400) return;
      lastLogRef.current = { line, ms: now };
      const entry = { ts: now, line };
      if (liveLogRef.current) {
        setLog((prev) => [...prev.slice(-400), entry]);
      } else {
        pendingLogRef.current = [...pendingLogRef.current.slice(-400), entry];
      }
    },
    [],
  );

  useEffect(() => {
    liveLogRef.current = liveLog;
    if (!liveLog && pendingLogRef.current.length) return;
    if (liveLog && pendingLogRef.current.length) {
      setLog((prev) => [...prev.slice(-400), ...pendingLogRef.current].slice(-400));
      pendingLogRef.current = [];
    }
  }, [liveLog]);

  useEffect(() => {
    autoReconnectRef.current = autoReconnect;
    localStorage.setItem('ql-auto-reconnect', autoReconnect ? 'true' : 'false');
  }, [autoReconnect]);

  const parseStatus = useCallback(
    (line: string) => {
      if (!line) return;
      if (line.includes('Lamp=')) {
        const b = line.match(/Brightness=([0-9.]+)/);
        const c = line.match(/Cap=([0-9.]+)/);
        const lamp = line.match(/Lamp=([A-Z]+)/);
        const sw = line.match(/Switch=([A-Z]+)/);
        setStatus((s) => ({
          ...s,
          brightness: b ? parseFloat(b[1]) : s.brightness,
          cap: c ? parseFloat(c[1]) : s.cap,
          lampState: lamp ? lamp[1] : s.lampState,
          switchState: sw ? sw[1] : s.switchState,
          lastStatusAt: Date.now(),
        }));
      }
      if (line.startsWith('Pattern ')) {
        const modeMatch = line.match(/Pattern\s+(\d+)\/(\d+).*?'([^']+)'/);
        const autoMatch = line.match(/AutoCycle=(ON|OFF)/);
        const speedMatch = line.match(/Speed=([0-9.]+)/);
        if (modeMatch) {
          const idx = parseInt(modeMatch[1], 10);
          const total = parseInt(modeMatch[2], 10);
          setStatus((s) => ({
            ...s,
            currentPattern: !Number.isNaN(idx) ? idx : s.currentPattern,
            patternCount: !Number.isNaN(total) ? total : s.patternCount,
            patternName: modeMatch[3] || s.patternName,
            autoCycle: autoMatch ? autoMatch[1] === 'ON' : s.autoCycle,
            patternSpeed: speedMatch ? parseFloat(speedMatch[1]) : s.patternSpeed,
            lastStatusAt: Date.now(),
          }));
        }
      }
      if (line.startsWith('[Quick]')) {
        const csv = line.replace('[Quick]', '').trim();
        setStatus((s) => ({ ...s, quickCsv: csv, lastStatusAt: Date.now() }));
      }
      if (line.startsWith('[Touch]')) {
        const active = line.includes('active=1');
        setStatus((s) => ({ ...s, touchState: active ? 'TOUCH' : 'idle', lastStatusAt: Date.now() }));
      }
      if (line.startsWith('Ramp=')) {
        const onMs = line.match(/Ramp=([0-9]+)/);
        const offMs = line.match(/\/\s*([0-9]+)ms/);
        const idle = line.match(/IdleOff=([0-9]+)m/);
        const patFade = line.match(/PatFade=ON\(([0-9.]+)\)/);
        const touchDim = line.match(/TouchDim=(ON|OFF)/);
        setStatus((s) => ({
          ...s,
          rampOnMs: onMs ? parseInt(onMs[1], 10) : s.rampOnMs,
          rampOffMs: offMs ? parseInt(offMs[1], 10) : s.rampOffMs,
          idleOffMin: idle ? parseInt(idle[1], 10) : s.idleOffMin,
          patternFade: patFade ? parseFloat(patFade[1]) : s.patternFade,
          touchState: touchDim ? (touchDim[1] === 'ON' ? 'TOUCHDIM' : s.touchState) : s.touchState,
          lastStatusAt: Date.now(),
        }));
      }
      if (line.startsWith('Presence=')) {
        setStatus((s) => ({ ...s, presence: line.replace('Presence=', '').trim(), lastStatusAt: Date.now() }));
      }
      if (line.startsWith('[Custom]')) {
        const len = line.match(/len=([0-9]+)/);
        const step = line.match(/stepMs=([0-9]+)/);
        setStatus((s) => ({
          ...s,
          customLen: len ? parseInt(len[1], 10) : s.customLen,
          customStepMs: step ? parseInt(step[1], 10) : s.customStepMs,
          lastStatusAt: Date.now(),
        }));
      }
      if (line.startsWith('[Light]')) {
        setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
      }
      if (line.startsWith('[Music]')) {
        setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
      }
    },
    [],
  );

  const sendCmd = useCallback(
    async (cmd: string) => {
      try {
        await writeLine(cmdCharRef.current, cmd);
        pushLog('> ' + cmd);
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        toast.error('Command failed: ' + msg);
        throw e;
      }
    },
    [pushLog],
  );

  const refreshStatus = useCallback(async () => {
    try {
      await sendCmd('status');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      pushLog('Status error: ' + msg);
      toast.error('Status error: ' + msg);
    }
  }, [pushLog, sendCmd]);

  const cleanup = useCallback(() => {
    unsubRef.current.forEach((fn) => fn());
    unsubRef.current = [];
    if (deviceRef.current) {
      disconnectDevice(deviceRef.current);
    }
    deviceRef.current = null;
    cmdCharRef.current = null;
    statusCharRef.current = null;
    setStatus((s) => ({
      ...s,
      connected: false,
      connecting: false,
      deviceName: '',
      currentPattern: undefined,
      brightness: undefined,
      cap: undefined,
      lampState: undefined,
      switchState: undefined,
      touchState: undefined,
    }));
  }, []);

  const attachListeners = useCallback(
    async (dev: BluetoothDevice, cmdChar: BluetoothRemoteGATTCharacteristic, statusChar: BluetoothRemoteGATTCharacteristic) => {
      const lineHandler = (line: string) => {
        pushLog(line);
        parseStatus(line);
      };

      const unsubCmd = await subscribeToLines(cmdChar, lineHandler);
      const unsubStat = await subscribeToLines(statusChar, lineHandler);
      unsubRef.current = [unsubCmd, unsubStat];

      const onDisc = () => {
        pushLog('Disconnected');
        cleanup();
        if (autoReconnectRef.current) {
          setTimeout(() => {
            if (dev.gatt && !dev.gatt.connected) {
              connectWithDevice(dev, true);
            }
          }, 800);
        }
      };
      dev.addEventListener('gattserverdisconnected', onDisc);
      unsubRef.current.push(() => dev.removeEventListener('gattserverdisconnected', onDisc));
    },
    [cleanup, parseStatus, pushLog],
  );

  const connectWithDevice = useCallback(
    async (dev?: BluetoothDevice | null, silent = false) => {
      if (!dev) return;
      deviceRef.current = dev;
      try {
        const { cmdChar, statusChar } = await connectDevice(dev);
        cmdCharRef.current = cmdChar;
        statusCharRef.current = statusChar;
        await attachListeners(dev, cmdChar, statusChar);

        setStatus((s) => ({
          ...s,
          connected: true,
          connecting: false,
          deviceName: dev.name || 'BLE Device',
        }));
        lastDeviceIdRef.current = dev.id || null;
        if (dev.id) localStorage.setItem('ql-last-device-id', dev.id);
        if (dev.name) localStorage.setItem('ql-last-device-name', dev.name);
        if (!silent) pushLog('Connected to ' + (dev.name || 'BLE'));
        await refreshStatus();
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        pushLog('Connect error: ' + msg);
        setStatus((s) => ({ ...s, connecting: false }));
        cleanup();
        toast.error('Connect error: ' + msg);
        throw e;
      }
    },
    [attachListeners, cleanup, pushLog, refreshStatus],
  );

  const connect = useCallback(async () => {
    if (!navigator.bluetooth) {
      pushLog('Web Bluetooth not available');
      toast.error('Web Bluetooth not available in this browser');
      return;
    }
    setStatus((s) => ({ ...s, connecting: true }));
    try {
      const dev = await requestDevice();
      await connectWithDevice(dev);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      pushLog('Connect error: ' + msg);
      setStatus((s) => ({ ...s, connecting: false }));
      cleanup();
      toast.error('Connect error: ' + msg);
    }
  }, [cleanup, connectWithDevice, pushLog]);

  const tryReconnectLast = useCallback(async () => {
    if (!navigator.bluetooth || !navigator.bluetooth.getDevices) return;
    if (!autoReconnectRef.current) return;
    const lastId = lastDeviceIdRef.current;
    try {
      const devices = await navigator.bluetooth.getDevices();
      const match = devices.find((d: BluetoothDevice) => (lastId && d.id === lastId) || (!lastId && d.name));
      if (match) {
        setStatus((s) => ({ ...s, connecting: true }));
        await connectWithDevice(match, true);
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      pushLog('Auto-reconnect failed: ' + msg);
      toast.error('Auto-reconnect failed: ' + msg);
    }
  }, [connectWithDevice, pushLog, setStatus]);

  useEffect(() => {
    return () => {
      cleanup();
    };
  }, [cleanup]);

  useEffect(() => {
    tryReconnectLast();
  }, [tryReconnectLast]);

  return useMemo(
    () => ({
      status,
      log,
      liveLog,
      setLiveLog,
      autoReconnect,
      setAutoReconnect,
      connect,
      disconnect: cleanup,
      refreshStatus,
      sendCmd,
    }),
    [autoReconnect, cleanup, connect, liveLog, log, refreshStatus, sendCmd, setAutoReconnect, setLiveLog, status],
  );
}
