import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { toast } from 'react-toastify';
import { BLE_UUIDS, connectDevice, disconnectDevice, readLines, requestDevice, subscribeToLines, writeLine } from './bleClient';
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
  knownDevices: Record<string, string>;
  forgetDevice: (id: string) => void;
  renameDevice: (id: string, name: string) => void;
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
  const filterParsedRef = useRef<boolean>(filterParsed);
  const [autoReconnect, setAutoReconnect] = useState<boolean>(() => {
    const stored = localStorage.getItem('ql-auto-reconnect');
    return stored !== 'false';
  });

  const cmdCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const statusCharRef = useRef<BluetoothRemoteGATTCharacteristic | null>(null);
  const deviceRef = useRef<BluetoothDevice | null>(null);
  const connectingRef = useRef(false);
  const liveLogRef = useRef(true);
  const lastLogRef = useRef<{ line: string; ms: number }>({ line: '', ms: 0 });
  const pendingLogRef = useRef<LogEntry[]>([]);
  const unsubRef = useRef<(() => void)[]>([]);
  const statusEndWaiterRef = useRef<(() => void) | null>(null);
  const statusSnapshotPagesRef = useRef(0);
  const refreshInFlightRef = useRef<Promise<void> | null>(null);
  const autoReconnectRef = useRef<boolean>(autoReconnect);
  const lastDeviceIdRef = useRef<string | null>(localStorage.getItem('ql-last-device-id'));
  const knownDevicesRef = useRef<Record<string, string>>(JSON.parse(localStorage.getItem('ql-known-devices') || '{}'));
  const [knownDevices, setKnownDevices] = useState<Record<string, string>>(knownDevicesRef.current);
  const forgetDevice = useCallback((id: string) => {
    if (knownDevicesRef.current[id]) {
      delete knownDevicesRef.current[id];
      localStorage.setItem('ql-known-devices', JSON.stringify(knownDevicesRef.current));
      setKnownDevices({ ...knownDevicesRef.current });
    }
  }, []);
  const renameDevice = useCallback((id: string, name: string) => {
    if (!id) return;
    const clean = name && name.trim().length > 0 ? name.trim() : 'Quarzlampe';
    knownDevicesRef.current[id] = clean;
    localStorage.setItem('ql-known-devices', JSON.stringify(knownDevicesRef.current));
    setKnownDevices({ ...knownDevicesRef.current });
  }, []);

  const pushLog = useCallback(
    (line: string) => {
      if (!line) return;
      console.debug('[BLE]', line);
      // Filter out noisy status-only lines when enabled
      if (
        filterParsedRef.current &&
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

  useEffect(() => {
    filterParsedRef.current = filterParsed;
    localStorage.setItem('ql-log-filter', filterParsed ? 'true' : 'false');
  }, [filterParsed]);

  const parseStatus = useCallback((line: string) => parseStatusLine(line, setStatus), []);

  const handleLine = useCallback((line: string) => {
    const handled = parseStatus(line);
    if (!handled || !filterParsedRef.current) pushLog(line);
    if (handled) setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
    if (line.startsWith('STATUS|')) statusSnapshotPagesRef.current |= 1;
    else if (line.startsWith('STATUS1|')) statusSnapshotPagesRef.current |= 2;
    else if (line.startsWith('STATUS2|')) statusSnapshotPagesRef.current |= 4;
    if (line.startsWith('STATUS_END|')) {
      statusEndWaiterRef.current?.();
    }
  }, [parseStatus, pushLog]);

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
    if (refreshInFlightRef.current) return refreshInFlightRef.current;

    const refresh = (async () => {
      try {
        for (let attempt = 0; attempt < 2; attempt += 1) {
          statusSnapshotPagesRef.current = 0;
          let timer: ReturnType<typeof setTimeout> | undefined;
          const completed = new Promise<boolean>((resolve) => {
            statusEndWaiterRef.current = () => {
              if (timer) clearTimeout(timer);
              statusEndWaiterRef.current = null;
              resolve(true);
            };
            timer = setTimeout(() => {
              statusEndWaiterRef.current = null;
              resolve(false);
            }, 1800);
          });

          await sendCmd('status raw');
          if (await completed) return;

          // The firmware leaves STATUS_END (including core state) as the
          // readable value. This recovers initial power state when notify
          // setup failed and also distinguishes a complete snapshot.
          if (statusCharRef.current) {
            try {
              const lines = await readLines(statusCharRef.current);
              lines.forEach(handleLine);
              if (lines.some((line) => line.startsWith('STATUS_END|'))) return;
            } catch (readError) {
              pushLog('Status read fallback failed: ' + (readError instanceof Error ? readError.message : String(readError)));
            }
          }

          // Compatibility with firmware versions predating STATUS_END: all
          // three legacy status pages still prove that the snapshot arrived.
          if (statusSnapshotPagesRef.current === 7) return;
        }
        throw new Error('Incomplete status snapshot');
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
      } finally {
        statusEndWaiterRef.current = null;
      }
    })();

    refreshInFlightRef.current = refresh;
    try {
      await refresh;
    } finally {
      refreshInFlightRef.current = null;
    }
  }, [handleLine, pushLog, sendCmd]);

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
      // Only subscribe to the status characteristic; command char stays write-only.
      const unsubStat = await subscribeToLines(statusChar, handleLine);
      unsubRef.current = [unsubStat];

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
    [cleanup, handleLine, pushLog],
  );

  const connectWithDevice = useCallback(
    async (dev?: BluetoothDevice | null, silent = false) => {
      if (!dev) return;
      // Guard against concurrent gatt.connect() (manual connect racing the
      // auto-reconnect effect / StrictMode double-invoke) — BlueZ rejects
      // parallel connects on the same device with a generic NetworkError.
      if (connectingRef.current) return;
      // Already connected to this device (e.g. auto-reconnect firing again):
      // don't tear down the live link with a fresh connect.
      if (dev.gatt?.connected) return;
      connectingRef.current = true;
      deviceRef.current = dev;
      try {
        const { cmdChar, statusChar } = await connectDevice(dev);
        cmdCharRef.current = cmdChar;
        statusCharRef.current = statusChar;
        await attachListeners(dev, cmdChar, statusChar);
        const friendlyName = (dev.id && knownDevicesRef.current[dev.id]) || dev.name || 'Quarzlampe';
        if (dev.id && !knownDevicesRef.current[dev.id]) {
          knownDevicesRef.current[dev.id] = friendlyName;
          localStorage.setItem('ql-known-devices', JSON.stringify(knownDevicesRef.current));
          setKnownDevices({ ...knownDevicesRef.current });
        }

        setStatus((s) => ({
          ...s,
          connected: true,
          connecting: false,
          deviceName: friendlyName,
        }));
        lastDeviceIdRef.current = dev.id || null;
        if (dev.id) localStorage.setItem('ql-last-device-id', dev.id);
        if (dev.name) localStorage.setItem('ql-last-device-name', friendlyName);
        if (dev.id) {
          knownDevicesRef.current[dev.id] = dev.name || 'BLE';
          localStorage.setItem('ql-known-devices', JSON.stringify(knownDevicesRef.current));
          setKnownDevices({ ...knownDevicesRef.current });
        }
        if (!silent) pushLog('Connected to ' + (dev.name || 'BLE'));
        await refreshStatus();
        await sendCmd('custom export');
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        const lower = msg.toLowerCase();
        const untrusted = lower.includes('rejected unknown device') || lower.includes('unknown device');
        pushLog('Connect error: ' + msg);
        toast.error((untrusted ? 'Untrusted device: ' : 'Connect error: ') + msg);
        setStatus((s) => ({ ...s, connecting: false }));
        cleanup();
        throw e;
      } finally {
        connectingRef.current = false;
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
      if (dev?.id) {
        knownDevicesRef.current[dev.id] = dev.name || 'BLE';
        localStorage.setItem('ql-known-devices', JSON.stringify(knownDevicesRef.current));
      }
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
    // A `?bleId=` in the URL is an explicit connect intent (e.g. a per-lamp
    // bookmark), so honor it even when auto-reconnect is disabled. It takes
    // precedence over the last-used device. Without one, fall back to the
    // normal last-device reconnect, which only runs when auto-reconnect is on.
    const urlBleId = new URLSearchParams(window.location.search).get('bleId');
    const allowAuto = autoReconnectRef.current;
    if (!urlBleId && !allowAuto) return;
    const lastId = lastDeviceIdRef.current;
    try {
      const devices = await navigator.bluetooth.getDevices();
      const byId = (id: string | null) => (id ? devices.find((d) => d.id === id) : undefined);
      const match =
        byId(urlBleId) ||
        (allowAuto ? byId(lastId) : undefined) ||
        (allowAuto && !urlBleId && !lastId ? devices.find((d) => d.name) : undefined);
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
    // Delay auto-reconnect so it doesn't kick off a gatt.connect() during
    // React StrictMode's mount→unmount→mount churn: the unmount cleanup would
    // disconnect the in-flight connect and surface a spurious NetworkError.
    // The timer is cancelled on unmount, so only the settled mount reconnects.
    const t = setTimeout(() => {
      tryReconnectLast();
    }, 600);
    return () => clearTimeout(t);
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
      knownDevices,
      renameDevice,
      forgetDevice,
      connect,
      disconnect: cleanup,
      refreshStatus,
      sendCmd,
    }),
    [
      autoReconnect,
      cleanup,
      connect,
      filterParsed,
      forgetDevice,
      liveLog,
      log,
      refreshStatus,
      sendCmd,
      setAutoReconnect,
      setFilterParsed,
      setLiveLog,
      setLog,
      status,
    ],
  );
}
