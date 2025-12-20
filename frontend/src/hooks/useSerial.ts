import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { toast } from 'react-toastify';
import { DeviceStatus, parseStatusLine } from './status';

type LogEntry = { ts: number; line: string };

type SerialApi = {
  status: DeviceStatus;
  log: LogEntry[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  setLog: (v: LogEntry[] | ((prev: LogEntry[]) => LogEntry[])) => void;
  filterParsed: boolean;
  setFilterParsed: (v: boolean) => void;
  knownSerials: Record<string, string>;
  forgetSerial: (id: string) => void;
  renameSerial: (id: string, name: string) => void;
  connect: () => Promise<void>;
  disconnect: () => void;
  refreshStatus: () => Promise<void>;
  sendCmd: (cmd: string) => Promise<void>;
};

export function useSerial(): SerialApi {
  const [status, setStatus] = useState<DeviceStatus>({
    connected: false,
    connecting: false,
    deviceName: '',
    lastStatusAt: null,
    patternCount: 0,
  });
  const [log, setLog] = useState<LogEntry[]>([]);
  const [liveLog, setLiveLog] = useState(true);
  const setLogPublic = useCallback((v: LogEntry[] | ((prev: LogEntry[]) => LogEntry[])) => setLog(v), []);
  const [filterParsed, setFilterParsed] = useState<boolean>(() => {
    const stored = localStorage.getItem('ql-log-filter');
    return stored !== 'false';
  });
  const filterParsedRef = useRef<boolean>(filterParsed);
  const knownSerialsRef = useRef<Record<string, string>>(JSON.parse(localStorage.getItem('ql-known-serials') || '{}'));
  const [knownSerials, setKnownSerials] = useState<Record<string, string>>(knownSerialsRef.current);
  const forgetSerial = useCallback((id: string) => {
    if (knownSerialsRef.current[id]) {
      delete knownSerialsRef.current[id];
      localStorage.setItem('ql-known-serials', JSON.stringify(knownSerialsRef.current));
      setKnownSerials({ ...knownSerialsRef.current });
    }
  }, []);
  const renameSerial = useCallback((id: string, name: string) => {
    if (!id) return;
    const clean = name && name.trim().length > 0 ? name.trim() : 'Quarzlampe';
    knownSerialsRef.current[id] = clean;
    localStorage.setItem('ql-known-serials', JSON.stringify(knownSerialsRef.current));
    setKnownSerials({ ...knownSerialsRef.current });
  }, []);
  const portRef = useRef<SerialPort | null>(null);
  const readerCancelRef = useRef<() => void>();
  const writerRef = useRef<WritableStreamDefaultWriter<string> | null>(null);

  const pushLog = useCallback(
    (line: string) => {
      if (!line) return;
      console.debug('[Serial]', line);
      if (
        filterParsedRef.current &&
        (/^Status[:=]/i.test(line) ||
          /^Lamp=/.test(line) ||
          /^Ramp=/.test(line) ||
          /^RampOn/i.test(line) ||
          /^RampOff/i.test(line) ||
          line.startsWith("[Quick]") ||
          line.startsWith("[Touch]") ||
          line.startsWith("Device=") ||
          line.startsWith("[Poti]") ||
          line.startsWith("[Push]") ||
          line.startsWith("STATUS|") ||
          line.startsWith("SENSORS|") ||
          line.startsWith("> custom export") ||
          line.startsWith("> status"))
      ) {
        return;
      }
      const entry = { ts: Date.now(), line };
      setLog((prev) => [...prev.slice(-400), entry]);
    },
    [],
  );

  useEffect(() => {
    filterParsedRef.current = filterParsed;
    localStorage.setItem('ql-log-filter', filterParsed ? 'true' : 'false');
  }, [filterParsed]);

  const disconnect = useCallback(() => {
    readerCancelRef.current?.();
    readerCancelRef.current = undefined;
    writerRef.current?.close().catch(() => undefined);
    writerRef.current = null;
    if (portRef.current) {
      portRef.current.close().catch(() => undefined);
    }
    portRef.current = null;
    setStatus((s) => ({ ...s, connected: false, connecting: false, deviceName: '' }));
  }, []);

  const sendCmd = useCallback(
    async (cmd: string) => {
      if (!writerRef.current) {
        const msg = 'Serial not connected';
        toast.error(msg, {"toastId":msg});
        throw new Error(msg);
      }
      await writerRef.current.write(cmd + '\n');
      pushLog('> ' + cmd);
    },
    [pushLog],
  );

  const refreshStatus = useCallback(async () => {
    try {
      await sendCmd('status');
      await sendCmd('custom export');
    } catch (e) {
      pushLog('Status error: ' + e);
      disconnect();
    }
  }, [disconnect, pushLog, sendCmd]);

  const connect = useCallback(async () => {
    if (!('serial' in navigator)) {
      pushLog('Web Serial not available');
      return;
    }
    setStatus((s) => ({ ...s, connecting: true }));
    try {
      const port = await navigator.serial.requestPort();
      const info = port.getInfo ? port.getInfo() : {};
      const id = port.getInfo ? JSON.stringify(info) : '';
      await port.open({ baudRate: 115200 });
      portRef.current = port;
      if (id) {
        knownSerialsRef.current[id] = knownSerialsRef.current[id] || 'Serial';
        localStorage.setItem('ql-known-serials', JSON.stringify(knownSerialsRef.current));
        setKnownSerials({ ...knownSerialsRef.current });
      }

      const textEncoder = new TextEncoderStream();
      // Cast to any to satisfy differing BufferSource/Uint8Array stream typings
      textEncoder.readable.pipeTo(port.writable as unknown as WritableStream<any>);
      writerRef.current = textEncoder.writable.getWriter();

      const textDecoder = new TextDecoderStream();
      (port.readable as ReadableStream<Uint8Array>).pipeTo(textDecoder.writable as unknown as WritableStream<any>);
      const reader = textDecoder.readable.getReader();

      readerCancelRef.current = () => reader.cancel().catch(() => undefined);

      let buffer = '';
      const pump = async () => {
        try {
          // eslint-disable-next-line no-constant-condition
          while (true) {
            const { value, done } = await reader.read();
            if (done) break;
            if (value !== undefined) {
              buffer += value;
              const parts = buffer.split(/\r?\n/);
              buffer = parts.pop() || '';
              parts.forEach((line) => {
                const trimmed = line.trim();
                if (!trimmed) return;
                const handled = parseStatusLine(trimmed, setStatus);
                if (!handled || !filterParsed) pushLog(trimmed);
                else setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
              });
            }
          }
          // flush remainder if any
          if (buffer.trim()) {
            const trimmed = buffer.trim();
            const handled = parseStatusLine(trimmed, setStatus);
            if (!handled || !filterParsed) pushLog(trimmed);
            buffer = '';
          }
        } catch (err) {
          pushLog('Serial read error: ' + err);
        }
      };
      pump();

      const label =
        (id && knownSerialsRef.current[id]) ||
        (() => {
          try {
            const parsed = port.getInfo ? port.getInfo() : {};
            if ((parsed as any).usbProductId) return 'Serial';
          } catch {
            /* ignore */
          }
          return 'Serial';
        })();
      setStatus((s) => ({
        ...s,
        connected: true,
        connecting: false,
        deviceName: label,
      }));
      localStorage.setItem('ql-last-device-name', label);
      await refreshStatus();
    } catch (e) {
      pushLog('Serial connect error: ' + e);
      setStatus((s) => ({ ...s, connecting: false }));
      disconnect();
    }
  }, [disconnect, pushLog, refreshStatus]);

  useEffect(() => () => disconnect(), [disconnect]);

  return useMemo(
    () => ({
      status,
      log,
      liveLog,
      setLiveLog,
      setLog: setLogPublic,
      knownSerials,
      renameSerial,
      filterParsed,
      setFilterParsed,
      forgetSerial,
      connect,
      disconnect,
      refreshStatus,
      sendCmd,
    }),
    [connect, disconnect, filterParsed, forgetSerial, knownSerials, liveLog, log, refreshStatus, sendCmd, setFilterParsed, setLogPublic, status],
  );
}
