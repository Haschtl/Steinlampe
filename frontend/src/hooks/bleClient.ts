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
  return navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE] }],
    optionalServices: [SERVICE],
  });
}

export async function connectDevice(device: BluetoothDevice): Promise<BleHandles> {
  const server = await device.gatt!.connect();
  const svc = await server.getPrimaryService(SERVICE);
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
  const onNotify = (ev: Event) => {
    decodeLines(ev).forEach(handler);
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
