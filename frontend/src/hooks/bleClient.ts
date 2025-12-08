const SERVICE = 'd94d86d7-1eaf-47a4-9d1e-7a90bf34e66b';
const CMD_CHAR = '4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7';
const STATUS_CHAR = 'c5ad78b6-9b77-4a96-9a42-8e6e9a40c123';

export const BLE_UUIDS = { service: SERVICE, cmd: CMD_CHAR, status: STATUS_CHAR };

export type BleHandles = {
  device: BluetoothDevice;
  cmdChar: BluetoothRemoteGATTCharacteristic;
  statusChar: BluetoothRemoteGATTCharacteristic;
};

export type LineHandler = (line: string) => void;

export async function requestDevice(): Promise<BluetoothDevice> {
  try {
    return await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE] }],
      optionalServices: [SERVICE],
    });
  } catch (e) {
    // Some devices might not advertise the UUID in the scan response; fallback to acceptAllDevices
    return navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [SERVICE],
    });
  }
}

export async function connectDevice(device: BluetoothDevice): Promise<BleHandles> {
  console.log('BLE connecting to', device.name || device.id);
  // ensure fresh session
  if (device.gatt?.connected) {
    try { device.gatt.disconnect(); } catch (e) { console.warn('BLE disconnect before connect failed', e); }
  }
  let server: BluetoothRemoteGATTServer;
  try {
    server = await device.gatt!.connect();
  } catch (e) {
    console.error('BLE GATT connect failed', e);
    throw new Error(`GATT connect failed: ${e instanceof Error ? e.message : String(e)}`);
  }
  const fetchService = async (): Promise<BluetoothRemoteGATTService> => {
    try {
      return await server.getPrimaryService(SERVICE);
    } catch (e) {
      // fallback: scan all services for a partial match
      const services = await server.getPrimaryServices();
      const targetNoDash = SERVICE.replace(/-/g, '').toLowerCase();
      const match =
        services.find((s) => {
          const u = (s.uuid || '').toLowerCase();
          return u.includes(SERVICE.toLowerCase()) || u.replace(/-/g, '').includes(targetNoDash);
        }) || null;
      if (match) return match;
      console.warn('BLE: no matching service; available:', services.map((s) => s.uuid));
      throw e;
    }
  };

  let svc: BluetoothRemoteGATTService | null = null;
  try {
    svc = await fetchService();
  } catch (e) {
    // If discovery failed due to a stale/disconnected server, retry a fresh connect once.
    if (!server.connected) {
      try {
        server = await device.gatt!.connect();
        svc = await fetchService();
      } catch (e2) {
        throw e2;
      }
    } else {
      throw e;
    }
  }
  const cmdChar = await svc.getCharacteristic(CMD_CHAR);
  const statusChar = await svc.getCharacteristic(STATUS_CHAR);
  return { device, cmdChar, statusChar };
}

export function decodeLines(ev: Event): string[] {
  const target = ev.target as BluetoothRemoteGATTCharacteristic;
  const value = (target.value as DataView) || new DataView(new ArrayBuffer(0));
  const txt = new TextDecoder().decode(value);
  return txt.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
}

export async function subscribeToLines(
  char: BluetoothRemoteGATTCharacteristic,
  handler: LineHandler,
): Promise<() => void> {
  let buffer = '';
  const onNotify = (ev: Event) => {
    const target = ev.target as BluetoothRemoteGATTCharacteristic;
    const value = (target.value as DataView) || new DataView(new ArrayBuffer(0));
    buffer += new TextDecoder().decode(value);
    const parts = buffer.split(/\r?\n/);
    buffer = parts.pop() || '';
    parts.forEach((line) => {
      const trimmed = line.trim();
      if (trimmed) handler(trimmed);
    });
  };
  char.addEventListener('characteristicvaluechanged', onNotify as EventListener);
  try {
    await char.startNotifications();
  } catch (e) {
    // noop; some firmwares may not support notify, caller can fallback
    console.warn('Notifications not available', e);
  }

  return () => {
    try {
      char.removeEventListener('characteristicvaluechanged', onNotify as EventListener);
    } catch {
      /* ignore */
    }
  };
}

export async function writeLine(
  char: BluetoothRemoteGATTCharacteristic | null,
  line: string,
): Promise<void> {
  if (!char) throw new Error('Not connected');
  await char.writeValueWithoutResponse(new TextEncoder().encode(line.endsWith('\n') ? line : line + '\n'));
}

export function disconnectDevice(device: BluetoothDevice | null) {
  if (device?.gatt?.connected) device.gatt.disconnect();
}
