import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { BLE_UUIDS, connectDevice, disconnectDevice, requestDevice, subscribeToLines, writeLine } from './bleClient';

type BleStatus = {
  connected: boolean;
  connecting: boolean;
  deviceName: string;
  lastStatusAt: number | null;
  patternCount: number;
  currentPattern?: number;
  brightness?: number;
  cap?: number;
  lampState?: string;
  switchState?: string;
  touchState?: string;
};

type LogEntry = { ts: number; line: string };

type BleApi = {
  status: BleStatus;
  log: LogEntry[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
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

  const cmdCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const statusCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const deviceRef = useRef<BluetoothDevice | null>(null);
  const liveLogRef = useRef(true);
  const lastLogRef = useRef<{ line: string; ms: number }>({ line: '', ms: 0 });
  const pendingLogRef = useRef<LogEntry[]>([]);
  const unsubRef = useRef<(() => void)[]>([]);

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
        const modeMatch = line.match(/Pattern\s+(\d+)\/(\d+)/);
        if (modeMatch) {
          const idx = parseInt(modeMatch[1], 10);
          const total = parseInt(modeMatch[2], 10);
          setStatus((s) => ({
            ...s,
            currentPattern: !Number.isNaN(idx) ? idx : s.currentPattern,
            patternCount: !Number.isNaN(total) ? total : s.patternCount,
            lastStatusAt: Date.now(),
          }));
        }
      }
      if (line.startsWith('[Touch]')) {
        const active = line.includes('active=1');
        setStatus((s) => ({ ...s, touchState: active ? 'TOUCH' : 'idle', lastStatusAt: Date.now() }));
      }
    },
    [],
  );

  const sendCmd = useCallback(
    async (cmd: string) => {
      await writeLine(cmdCharRef.current, cmd);
      pushLog('> ' + cmd);
    },
    [pushLog],
  );

  const refreshStatus = useCallback(async () => {
    try {
      await sendCmd('status');
    } catch (e) {
      pushLog('Status error: ' + e);
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

  const connect = useCallback(async () => {
    if (!navigator.bluetooth) {
      pushLog('Web Bluetooth not available');
      return;
    }
    setStatus((s) => ({ ...s, connecting: true }));
    try {
      const dev = await requestDevice();
      deviceRef.current = dev;
      const { cmdChar, statusChar } = await connectDevice(dev);
      cmdCharRef.current = cmdChar;
      statusCharRef.current = statusChar;

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
      };
      dev.addEventListener('gattserverdisconnected', onDisc);
      unsubRef.current.push(() => dev.removeEventListener('gattserverdisconnected', onDisc));

      setStatus((s) => ({
        ...s,
        connected: true,
        connecting: false,
        deviceName: dev.name || 'BLE Device',
      }));
      pushLog('Connected to ' + (dev.name || 'BLE'));
      await refreshStatus();
    } catch (e) {
      pushLog('Connect error: ' + e);
      setStatus((s) => ({ ...s, connecting: false }));
      cleanup();
    }
  }, [cleanup, parseStatus, pushLog, refreshStatus]);

  useEffect(() => {
    return () => {
      cleanup();
    };
  }, [cleanup]);

  return useMemo(
    () => ({
      status,
      log,
      liveLog,
      setLiveLog,
      connect,
      disconnect: cleanup,
      refreshStatus,
      sendCmd,
    }),
    [cleanup, connect, liveLog, log, refreshStatus, sendCmd, setLiveLog, status],
  );
}
