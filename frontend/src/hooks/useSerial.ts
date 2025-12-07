import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

type SerialStatus = {
  connected: boolean;
  connecting: boolean;
  deviceName: string;
  lastStatusAt: number | null;
};

type LogEntry = { ts: number; line: string };

type SerialApi = {
  status: SerialStatus;
  log: LogEntry[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  connect: () => Promise<void>;
  disconnect: () => void;
  refreshStatus: () => Promise<void>;
  sendCmd: (cmd: string) => Promise<void>;
};

export function useSerial(): SerialApi {
  const [status, setStatus] = useState<SerialStatus>({
    connected: false,
    connecting: false,
    deviceName: '',
    lastStatusAt: null,
  });
  const [log, setLog] = useState<LogEntry[]>([]);
  const [liveLog, setLiveLog] = useState(true);
  const portRef = useRef<SerialPort | null>(null);
  const readerCancelRef = useRef<() => void>();
  const writerRef = useRef<WritableStreamDefaultWriter<string> | null>(null);

  const pushLog = useCallback((line: string) => {
    const entry = { ts: Date.now(), line };
    setLog((prev) => [...prev.slice(-400), entry]);
  }, []);

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
      if (!writerRef.current) throw new Error('Serial not connected');
      await writerRef.current.write(cmd + '\n');
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
      textEncoder.readable.pipeTo(port.writable as WritableStream<Uint8Array>);
      writerRef.current = textEncoder.writable.getWriter();

      const textDecoder = new TextDecoderStream();
      (port.readable as ReadableStream<Uint8Array>).pipeTo(textDecoder.writable);
      const reader = textDecoder.readable
        .pipeThrough(
          new TransformStream<string, string>({
            transform(chunk, controller) {
              const parts = chunk.split(/\r?\n/);
              parts.forEach((p) => controller.enqueue(p));
            },
          }),
        )
        .getReader();

      readerCancelRef.current = () => reader.cancel().catch(() => undefined);

      const pump = async () => {
        try {
          // eslint-disable-next-line no-constant-condition
          while (true) {
            const { value, done } = await reader.read();
            if (done) break;
            if (value) {
              pushLog(value);
              setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
            }
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
      connect,
      disconnect,
      refreshStatus,
      sendCmd,
    }),
    [connect, disconnect, liveLog, log, refreshStatus, sendCmd, status],
  );
}
