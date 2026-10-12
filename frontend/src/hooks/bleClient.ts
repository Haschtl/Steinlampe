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

// Thrown when the device accepts the link-layer connection but then immediately drops it
// before service discovery completes. In this firmware that's what an untrusted-device
// rejection looks like from the browser's side (the firmware has no BLE pairing-confirmation
// gate - it just disconnects outside the post-boot trust window), but the browser only ever
// surfaces a generic "GATT server is disconnected" DOMException for it. Callers can catch this
// specifically to show a message that actually explains what happened.
export class BleDroppedAfterConnectError extends Error {
  constructor(message = 'Device dropped the connection right after connecting, before service discovery completed.') {
    super(message);
    this.name = 'BleDroppedAfterConnectError';
  }
}

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
      } catch (reconnectError) {
        console.error('BLE GATT reconnect failed', reconnectError);
        throw new Error(`GATT connect failed: ${reconnectError instanceof Error ? reconnectError.message : String(reconnectError)}`);
      }
      try {
        svc = await fetchService();
      } catch (e2) {
        // Reconnected at the link layer but discovery dropped again right away: the
        // firmware is actively closing the link (most likely an untrusted-device
        // rejection), not a transient browser/stack hiccup.
        if (!server.connected) {
          throw new BleDroppedAfterConnectError();
        }
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

// Android's Web Bluetooth stack allows only one GATT operation in flight per
// device at a time; a second concurrent write/read throws "GATT operation
// already in progress" (desktop/BlueZ backends tolerate the overlap, which is
// why this only showed up on Android). Callers like the status refresh and
// the profile-preview fetch can legitimately fire several GATT ops close
// together, so serialize them all here instead of relying on every call site
// to coordinate.
let gattQueue: Promise<void> = Promise.resolve();

function enqueueGatt<T>(op: () => Promise<T>): Promise<T> {
  const run = gattQueue.then(op, op);
  gattQueue = run.then(
    () => undefined,
    () => undefined,
  );
  return run;
}

export function decodeLines(ev: Event): string[] {
  const target = ev.target as BluetoothRemoteGATTCharacteristic;
  const value = (target.value as DataView) || new DataView(new ArrayBuffer(0));
  const txt = new TextDecoder().decode(value);
  return txt.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
}

export async function readLines(char: BluetoothRemoteGATTCharacteristic): Promise<string[]> {
  return enqueueGatt(async () => {
    const value = await char.readValue();
    const text = new TextDecoder().decode(value);
    return text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  });
}

export async function subscribeToLines(
  char: BluetoothRemoteGATTCharacteristic,
  handler: LineHandler,
): Promise<() => void> {
  let buffer = '';
  const decoder = new TextDecoder();
  const onNotify = (ev: Event) => {
    const target = ev.target as BluetoothRemoteGATTCharacteristic;
    const value = (target.value as DataView) || new DataView(new ArrayBuffer(0));
    buffer += decoder.decode(value, { stream: true });
    const parts = buffer.split(/\r?\n/);
    buffer = parts.pop() || '';
    parts.forEach((line) => {
      const trimmed = line.trim();
      if (trimmed) handler(trimmed);
    });
  };
  char.addEventListener('characteristicvaluechanged', onNotify as EventListener);
  try {
    await enqueueGatt(() => char.startNotifications());
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
  await enqueueGatt(() =>
    char.writeValueWithoutResponse(new TextEncoder().encode(line.endsWith('\n') ? line : line + '\n')),
  );
}

/**
 * Same as writeLine, but waits for a GATT write response instead of firing-and-forgetting.
 * Used for OTA chunks: unlike normal commands, a silently-dropped chunk would only surface
 * as a checksum failure at the very end of the transfer, after sending everything for nothing.
 */
export async function writeLineWithResponse(
  char: BluetoothRemoteGATTCharacteristic | null,
  line: string,
): Promise<void> {
  if (!char) throw new Error('Not connected');
  await enqueueGatt(() =>
    char.writeValueWithResponse(new TextEncoder().encode(line.endsWith('\n') ? line : line + '\n')),
  );
}

export function disconnectDevice(device: BluetoothDevice | null) {
  if (device?.gatt?.connected) device.gatt.disconnect();
}
