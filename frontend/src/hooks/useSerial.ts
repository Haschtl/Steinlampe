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
  const portRef = useRef<SerialPort | null>(null);
  const readerCancelRef = useRef<() => void>();
  const writerRef = useRef<WritableStreamDefaultWriter<string> | null>(null);

  const pushLog = useCallback(
    (line: string) => {
      if (!line) return;
      console.debug('[Serial]', line);
      if (
        filterParsed &&
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
    [filterParsed],
  );

  useEffect(() => {
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
      await port.open({ baudRate: 115200 });
      portRef.current = port;

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

      setStatus((s) => ({
        ...s,
        connected: true,
        connecting: false,
        deviceName: port.getInfo ? JSON.stringify(port.getInfo()) : 'Serial',
      }));
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
      filterParsed,
      setFilterParsed,
      connect,
      disconnect,
      refreshStatus,
      sendCmd,
    }),
    [connect, disconnect, filterParsed, liveLog, log, refreshStatus, sendCmd, setFilterParsed, setLogPublic, status],
  );
}
