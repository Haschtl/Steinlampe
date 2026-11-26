// Mirrors gestures.cpp's grammar exactly - see that file's own header comment
// for the authoritative spec. Single source of truth for parsing/serializing
// the gesture-binding CSV on the frontend side (shared by GestureMappingCard's
// full editor and PotiCard's read-only "po" summary), not re-derived per call site.

export type InputKind = 'sw' | 'bt' | 'po';
export type GestureKind = 'tap' | 'swing' | 'lp' | 'dl';
export type ActionKind = 'quick' | 'toggle' | 'on' | 'off' | 'bri' | 'mesh_on' | 'mesh_off' | 'mesh_bri';

export type GestureRow = {
  id: string;
  inputKind: InputKind;
  inputIndex: number; // ignored for 'po'
  gestureKind: GestureKind;
  count: number; // tap/swing only
  state: string; // '' (either) | 'on' | 'off' (tap) | 'up' | 'dn' (swing)
  action: ActionKind;
  param1: string; // brightness pct, or mesh address (decimal or 0x-hex)
  param2: string; // mesh brightness pct
};

// row.param1 is stored as whatever text the user typed/picked (decimal or
// 0x-hex, matching gestures.cpp's own strtoul(..., 0) parsing) - compare by
// parsed numeric value so "0xc001", "0xC001" and "49153" all match the same
// known node's address.
export function matchesMeshAddr(param1: string, addr: number): boolean {
  const n = parseInt(param1, param1.trim().toLowerCase().startsWith('0x') ? 16 : 10);
  return Number.isFinite(n) && n === addr;
}

let rowIdCounter = 0;
export function newRowId() {
  rowIdCounter += 1;
  return `gest-row-${rowIdCounter}`;
}

export function parseGestureKey(key: string): Omit<GestureRow, 'id' | 'action' | 'param1' | 'param2'> | null {
  let inputKind: InputKind;
  let pos = 0;
  if (key.startsWith('sw')) {
    inputKind = 'sw';
    pos = 2;
  } else if (key.startsWith('bt')) {
    inputKind = 'bt';
    pos = 2;
  } else if (key.startsWith('po')) {
    inputKind = 'po';
    pos = 2;
  } else {
    return null;
  }
  let inputIndex = 0;
  if (inputKind !== 'po') {
    const m = key.slice(pos).match(/^(\d+)/);
    if (!m) return null;
    inputIndex = parseInt(m[1], 10);
    pos += m[1].length;
  }
  const rest = key.slice(pos);
  if (rest === 'lp') return { inputKind, inputIndex, gestureKind: 'lp', count: 0, state: '' };
  if (rest === 'dl') return { inputKind, inputIndex, gestureKind: 'dl', count: 0, state: '' };
  const tapMatch = rest.match(/^t(\d+)(on|off)?$/);
  if (tapMatch) return { inputKind, inputIndex, gestureKind: 'tap', count: parseInt(tapMatch[1], 10), state: tapMatch[2] ?? '' };
  const swingMatch = rest.match(/^s(\d+)(up|dn)?$/);
  if (swingMatch) return { inputKind, inputIndex, gestureKind: 'swing', count: parseInt(swingMatch[1], 10), state: swingMatch[2] ?? '' };
  return null;
}

export function parseGestureAction(val: string): Pick<GestureRow, 'action' | 'param1' | 'param2'> | null {
  const colon = val.indexOf(':');
  const head = colon >= 0 ? val.slice(0, colon) : val;
  const rest = colon >= 0 ? val.slice(colon + 1) : '';
  switch (head) {
    case 'quick':
    case 'toggle':
    case 'on':
    case 'off':
      return { action: head, param1: '', param2: '' };
    case 'bri':
      return { action: 'bri', param1: rest, param2: '' };
    case 'mesh_on':
    case 'mesh_off':
      return { action: head, param1: rest, param2: '' };
    case 'mesh_bri': {
      const idx = rest.indexOf(':');
      if (idx < 0) return null;
      return { action: 'mesh_bri', param1: rest.slice(0, idx), param2: rest.slice(idx + 1) };
    }
    default:
      return null;
  }
}

export function parseGestureCsv(csv: string): GestureRow[] {
  if (!csv) return [];
  return csv
    .split(';')
    .map((entry) => {
      const eq = entry.indexOf('=');
      if (eq <= 0) return null;
      const key = parseGestureKey(entry.slice(0, eq));
      const action = parseGestureAction(entry.slice(eq + 1));
      if (!key || !action) return null;
      return { id: newRowId(), ...key, ...action };
    })
    .filter((r): r is GestureRow => r !== null);
}

export function serializeRow(row: GestureRow): string {
  let key: string = row.inputKind;
  if (row.inputKind !== 'po') key += String(row.inputIndex);
  if (row.gestureKind === 'lp') key += 'lp';
  else if (row.gestureKind === 'dl') key += 'dl';
  else if (row.gestureKind === 'tap') key += `t${row.count}${row.state}`;
  else key += `s${row.count}${row.state}`;

  let action: string = row.action;
  if (row.action === 'bri' || row.action === 'mesh_on' || row.action === 'mesh_off') action += `:${row.param1}`;
  else if (row.action === 'mesh_bri') action += `:${row.param1}:${row.param2}`;

  return `${key}=${action}`;
}

export function defaultRow(): GestureRow {
  return {
    id: newRowId(),
    inputKind: 'bt',
    inputIndex: 0,
    gestureKind: 'tap',
    count: 3,
    state: '',
    action: 'toggle',
    param1: '',
    param2: '',
  };
}
