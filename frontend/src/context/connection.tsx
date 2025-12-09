import { createContext, ReactNode, useContext, useMemo, useState } from 'react';
import { useBle } from '@/hooks/useBle';
import { useSerial } from '@/hooks/useSerial';

type TransportType = 'ble' | 'serial' | null;

type ConnectionContextValue = {
  transport: TransportType;
  setTransport: (t: TransportType) => void;
  deviceName: string;
  log: { ts: number; line: string }[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  setLog: (v: { ts: number; line: string }[]) => void;
  filterParsed: boolean;
  setFilterParsed: (v: boolean) => void;
  autoReconnect: boolean;
  setAutoReconnect: (v: boolean) => void;
  knownDevices: Record<string, string>;
  forgetDevice: (id: string) => void;
  knownSerials: Record<string, string>;
  forgetSerial: (id: string) => void;
  status: ReturnType<typeof useBle>['status'];
  connect: () => Promise<void>;
  connectBle: () => Promise<void>;
  connectSerial: () => Promise<void>;
  disconnect: () => void;
  sendCmd: (cmd: string) => Promise<void>;
  refreshStatus: () => Promise<void>;
};

const ConnectionContext = createContext<ConnectionContextValue | null>(null);

export function ConnectionProvider({ children }: { children: ReactNode }) {
  const ble = useBle();
  const serial = useSerial();
  const [transport, setTransportState] = useState<TransportType>(() => {
    const cached = localStorage.getItem('ql-last-transport');
    return cached === 'ble' || cached === 'serial' ? (cached as TransportType) : null;
  });

  const setTransport = (t: TransportType) => {
    setTransportState(t);
    if (t) localStorage.setItem('ql-last-transport', t);
  };

  const value = useMemo<ConnectionContextValue>(() => {
    const active = transport === 'serial' ? serial : ble;
    const deviceName = active.status.deviceName || localStorage.getItem('ql-last-device-name') || '';
    return {
      transport,
      setTransport,
      deviceName,
      log: active.log,
      liveLog: active.liveLog,
      setLiveLog: active.setLiveLog,
      setLog: active.setLog,
      filterParsed: active.filterParsed,
      setFilterParsed: active.setFilterParsed,
      autoReconnect: ble.autoReconnect,
      setAutoReconnect: ble.setAutoReconnect,
      knownDevices: ble.knownDevices,
      forgetDevice: ble.forgetDevice,
      knownSerials: serial.knownSerials ?? {},
      forgetSerial: serial.forgetSerial ?? (() => undefined),
      status: active.status,
      connect: async () => {
        if (transport === 'serial') {
          await serial.connect();
          setTransport('serial');
          localStorage.setItem('ql-last-transport', 'serial');
        } else {
          await ble.connect();
          setTransport('ble');
          if (ble.status.deviceName) localStorage.setItem('ql-last-device-name', ble.status.deviceName);
          localStorage.setItem('ql-last-transport', 'ble');
        }
      },
      connectBle: async () => {
        await ble.connect();
        setTransport('ble');
        if (ble.status.deviceName) localStorage.setItem('ql-last-device-name', ble.status.deviceName);
        localStorage.setItem('ql-last-transport', 'ble');
      },
      connectSerial: async () => {
        await serial.connect();
        setTransport('serial');
        localStorage.setItem('ql-last-transport', 'serial');
      },
      disconnect: () => {
        if (transport === 'serial') serial.disconnect();
        else ble.disconnect();
        setTransport(null);
        localStorage.removeItem('ql-last-transport');
      },
      sendCmd: active.sendCmd,
      refreshStatus: active.refreshStatus,
    };
  }, [ble, serial, transport]);

  return <ConnectionContext.Provider value={value}>{children}</ConnectionContext.Provider>;
}

export function useConnection() {
  const ctx = useContext(ConnectionContext);
  if (!ctx) throw new Error('useConnection must be used within ConnectionProvider');
  return ctx;
}
