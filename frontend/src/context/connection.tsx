import { createContext, ReactNode, useContext, useMemo, useState } from 'react';
import { useBle } from '@/hooks/useBle';

type TransportType = 'ble' | 'serial' | null;

type ConnectionContextValue = {
  transport: TransportType;
  setTransport: (t: TransportType) => void;
  deviceName: string;
  log: { ts: number; line: string }[];
  liveLog: boolean;
  setLiveLog: (v: boolean) => void;
  autoReconnect: boolean;
  setAutoReconnect: (v: boolean) => void;
  status: ReturnType<typeof useBle>['status'];
  connect: () => Promise<void>;
  disconnect: () => void;
  sendCmd: (cmd: string) => Promise<void>;
  refreshStatus: () => Promise<void>;
};

const ConnectionContext = createContext<ConnectionContextValue | null>(null);

export function ConnectionProvider({ children }: { children: ReactNode }) {
  const ble = useBle();
  const [transport, setTransportState] = useState<TransportType>(() => {
    const cached = localStorage.getItem('ql-last-transport');
    return cached === 'ble' || cached === 'serial' ? (cached as TransportType) : null;
  });

  const setTransport = (t: TransportType) => {
    setTransportState(t);
    if (t) localStorage.setItem('ql-last-transport', t);
  };

  const value = useMemo<ConnectionContextValue>(() => {
    const deviceName = ble.status.deviceName || localStorage.getItem('ql-last-device-name') || '';
    return {
      transport,
      setTransport,
      deviceName,
      log: ble.log,
      liveLog: ble.liveLog,
      setLiveLog: ble.setLiveLog,
      autoReconnect: ble.autoReconnect,
      setAutoReconnect: ble.setAutoReconnect,
      status: ble.status,
      connect: async () => {
        await ble.connect();
        setTransport('ble');
        if (ble.status.deviceName) localStorage.setItem('ql-last-device-name', ble.status.deviceName);
        localStorage.setItem('ql-last-transport', 'ble');
      },
      disconnect: () => {
        ble.disconnect();
        setTransport(null);
        localStorage.removeItem('ql-last-transport');
      },
      sendCmd: ble.sendCmd,
      refreshStatus: ble.refreshStatus,
    };
  }, [ble, transport]);

  return <ConnectionContext.Provider value={value}>{children}</ConnectionContext.Provider>;
}

export function useConnection() {
  const ctx = useContext(ConnectionContext);
  if (!ctx) throw new Error('useConnection must be used within ConnectionProvider');
  return ctx;
}
