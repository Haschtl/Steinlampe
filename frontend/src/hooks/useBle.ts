import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { toast } from 'react-toastify';
import { BLE_UUIDS, connectDevice, disconnectDevice, requestDevice, subscribeToLines, writeLine } from './bleClient';
import { DeviceStatus, parseStatusLine } from './status';

type LogEntry = { ts: number; line: string };

type BleApi = {
  status: DeviceStatus;
  log: LogEntry[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  setLog: (v: LogEntry[] | ((prev: LogEntry[]) => LogEntry[])) => void;
  filterParsed: boolean;
  setFilterParsed: (v: boolean) => void;
  autoReconnect: boolean;
  setAutoReconnect: (v: boolean) => void;
  connect: () => Promise<void>;
  disconnect: () => void;
  refreshStatus: () => Promise<void>;
  sendCmd: (cmd: string) => Promise<void>;
};

export function useBle(): BleApi {
  const [status, setStatus] = useState<DeviceStatus>({
    connected: false,
    connecting: false,
    deviceName: '',
    lastStatusAt: null,
    patternCount: 0,
  });
  const [log, setLogState] = useState<LogEntry[]>([]);
  const setLog = useCallback((v: LogEntry[] | ((prev: LogEntry[]) => LogEntry[])) => setLogState(v), []);
  const [liveLog, setLiveLog] = useState(true);
  const [filterParsed, setFilterParsed] = useState<boolean>(() => {
    const stored = localStorage.getItem('ql-log-filter');
    return stored !== 'false';
  });
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
      if (!line) return;
      // Filter out noisy status-only lines when enabled
      if (
        filterParsed &&
        (
          /^Status[:=]/i.test(line) ||
          /^Lamp=/.test(line) ||
          line.startsWith('[Quick]') ||
          line.startsWith('[Touch]') ||
          line.startsWith('Ramp=') ||
          line.startsWith('Device=') ||
          line.startsWith('[Poti]') ||
          line.startsWith('[Push]') ||
          line.startsWith('STATUS|') ||
          line.startsWith('SENSORS|') ||
          line.startsWith('> custom export') ||
          line.startsWith('> status')
        )
      ) {
        return;
      }
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
    [filterParsed],
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

  useEffect(() => {
    localStorage.setItem('ql-log-filter', filterParsed ? 'true' : 'false');
  }, [filterParsed]);

  const parseStatus = useCallback((line: string) => parseStatusLine(line, setStatus), []);

  const lastToast = useRef<string>('');

  const sendCmd = useCallback(
    async (cmd: string) => {
      try {
        await writeLine(cmdCharRef.current, cmd);
        pushLog('> ' + cmd);
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        if (lastToast.current !== msg) {
          toast.error('Command failed: ' + msg);
          lastToast.current = msg;
        }
        throw e;
      }
    },
    [pushLog],
  );

  const refreshStatus = useCallback(async () => {
    try {
      await sendCmd('status');
      await sendCmd('custom export');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      pushLog('Status error: ' + msg);
      if (lastToast.current !== msg) {
        toast.error('Status error: ' + msg);
        lastToast.current = msg;
      }
      cleanup();
      if (autoReconnectRef.current && deviceRef.current) {
        setTimeout(() => {
          if (deviceRef.current && !deviceRef.current.gatt?.connected) {
            connectWithDevice(deviceRef.current, true).catch(() => undefined);
          }
        }, 800);
      }
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
      hasLight: undefined,
      hasMusic: undefined,
      hasPoti: undefined,
      hasPush: undefined,
      hasPresence: undefined,
    }));
  }, []);

  const attachListeners = useCallback(
    async (dev: BluetoothDevice, cmdChar: BluetoothRemoteGATTCharacteristic, statusChar: BluetoothRemoteGATTCharacteristic) => {
      const lineHandler = (line: string) => {
        const handled = parseStatus(line);
        if (!handled || !filterParsed) pushLog(line);
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
    [cleanup, parseStatus, pushLog, filterParsed],
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
      setLog,
      filterParsed,
      setFilterParsed,
      autoReconnect,
      setAutoReconnect,
      connect,
      disconnect: cleanup,
      refreshStatus,
      sendCmd,
    }),
    [autoReconnect, cleanup, connect, filterParsed, liveLog, log, refreshStatus, sendCmd, setAutoReconnect, setFilterParsed, setLiveLog, setLog, status],
  );
}
