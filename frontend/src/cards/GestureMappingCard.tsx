import { useEffect, useMemo, useRef, useState } from 'react';
import { PlusCircle, MinusCircle, RefreshCw, Send } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { fallbackNodeLabel, fetchKnownNodes, MeshNode } from '@/lib/meshProtocol';
import { Trans, useI18n } from '@/i18n';

const CUSTOM_MESH_ADDR = '__custom__';

// Mirrors gestures.cpp's grammar exactly - see that file's own header comment
// for the authoritative spec. Kept here as the single source of truth for
// parsing/serializing on the frontend side, not re-derived per call site.
type InputKind = 'sw' | 'bt' | 'po';
type GestureKind = 'tap' | 'swing' | 'lp' | 'dl';
type ActionKind = 'quick' | 'toggle' | 'on' | 'off' | 'bri' | 'mesh_on' | 'mesh_off' | 'mesh_bri';

type GestureRow = {
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
function matchesMeshAddr(param1: string, addr: number): boolean {
  const n = parseInt(param1, param1.trim().toLowerCase().startsWith('0x') ? 16 : 10);
  return Number.isFinite(n) && n === addr;
}

let rowIdCounter = 0;
function newRowId() {
  rowIdCounter += 1;
  return `gest-row-${rowIdCounter}`;
}

function parseGestureKey(key: string): Omit<GestureRow, 'id' | 'action' | 'param1' | 'param2'> | null {
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

function parseGestureAction(val: string): Pick<GestureRow, 'action' | 'param1' | 'param2'> | null {
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

function parseGestureCsv(csv: string): GestureRow[] {
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

function serializeRow(row: GestureRow): string {
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

function defaultRow(): GestureRow {
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

export function GestureMappingCard() {
  const { t } = useI18n();
  const { status, refreshStatus, sendCmd, log } = useConnection();
  const [rows, setRows] = useState<GestureRow[]>([]);
  const [meshNodes, setMeshNodes] = useState<MeshNode[]>([]);
  const logRef = useRef(log);
  logRef.current = log;

  useEffect(() => {
    if (status.gestCsv !== undefined) setRows(parseGestureCsv(status.gestCsv));
  }, [status.gestCsv]);

  // Lets the mesh-action rows below pick a lamp by name instead of typing a
  // raw address - best-effort only: if this build has no provisioner (no
  // `mesh_nodes` status field at all) or the fetch times out, the rows just
  // fall back to the free-text address input they always had.
  useEffect(() => {
    if (status.meshNodeCount === undefined) return;
    fetchKnownNodes(sendCmd, () => logRef.current)
      .then(setMeshNodes)
      .catch(() => undefined);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status.meshNodeCount]);

  // (type, slot index) options for the "Input" picker - only roles the
  // firmware's gesture detectors actually track (ROLE_POWER/ROLE_MODE_NEXT -
  // see inputs.cpp). A synthetic Poti entry is appended when the lamp
  // reports one.
  const inputOptions = useMemo(() => {
    const opts: { value: string; label: string; kind: InputKind; index: number }[] = [];
    (status.buttons ?? []).forEach((b, i) => {
      if (b.role !== 'power' && b.role !== 'modenext') return;
      const kind: InputKind = b.type === 'toggle' ? 'sw' : 'bt';
      opts.push({ value: `${kind}-${i}`, label: `${b.label} (${b.type})`, kind, index: i });
    });
    if (status.hasPoti) opts.push({ value: 'po-0', label: t('label.poti', 'Poti'), kind: 'po', index: 0 });
    return opts;
  }, [status.buttons, status.hasPoti, t]);

  const gestureOptionsFor = (kind: InputKind) => {
    if (kind === 'po') return [{ value: 'swing', label: t('label.gestSwing', 'N-swing') }];
    const base = [{ value: 'tap', label: t('label.gestTap', 'N-tap') }];
    if (kind === 'bt')
      base.push(
        { value: 'lp', label: t('label.gestLongPress', 'Long press') },
        { value: 'dl', label: t('label.gestDoubleLong', 'Double-long') },
      );
    return base;
  };

  const stateOptionsFor = (gestureKind: GestureKind) => {
    if (gestureKind === 'swing')
      return [
        { value: '', label: t('label.gestStateAny', 'Any') },
        { value: 'up', label: t('label.gestSwingUp', 'Up then down') },
        { value: 'dn', label: t('label.gestSwingDown', 'Down then up') },
      ];
    return [
      { value: '', label: t('label.gestStateAny', 'Any') },
      { value: 'on', label: t('label.gestStateOn', 'Started ON') },
      { value: 'off', label: t('label.gestStateOff', 'Started OFF') },
    ];
  };

  const updateRow = (id: string, patch: Partial<GestureRow>) => {
    setRows((prev) => prev.map((r) => (r.id === id ? { ...r, ...patch } : r)));
  };

  const setRowInput = (id: string, value: string) => {
    const opt = inputOptions.find((o) => o.value === value);
    if (!opt) return;
    const gestures = gestureOptionsFor(opt.kind);
    updateRow(id, {
      inputKind: opt.kind,
      inputIndex: opt.index,
      // Reset the gesture kind if the previous one isn't valid for the new input.
      gestureKind: gestures.some((g) => g.value === rows.find((r) => r.id === id)?.gestureKind)
        ? rows.find((r) => r.id === id)!.gestureKind
        : (gestures[0].value as GestureKind),
    });
  };

  const addRow = () => setRows((prev) => [...prev, defaultRow()]);
  const removeRow = (id: string) => setRows((prev) => prev.filter((r) => r.id !== id));

  const save = () => {
    const csv = rows.map(serializeRow).join(';');
    sendCmd(`gest set ${csv}`).catch((e) => console.warn(e));
  };

  const sendMeshTest = (row: GestureRow) => {
    if (row.action === 'mesh_on' && row.param1) sendCmd(`mesh on ${row.param1}`).catch((e) => console.warn(e));
    else if (row.action === 'mesh_off' && row.param1) sendCmd(`mesh off ${row.param1}`).catch((e) => console.warn(e));
    else if (row.action === 'mesh_bri' && row.param1) sendCmd(`mesh bri ${row.param1} ${row.param2 || '50'}`).catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.gestures">Input gestures</Trans>
        </CardTitle>
        <p className="text-sm text-muted">
          <Trans k="desc.gestures">
            Map a repeated/held input gesture to an action, including Mesh commands to other lamps in the mesh set up
            below.
          </Trans>
        </p>
      </CardHeader>
      <CardContent className="space-y-3">
        {rows.length === 0 ? (
          <p className="text-sm text-muted">
            <Trans k="label.gestNone">No gestures configured - every input uses its built-in default behavior.</Trans>
          </p>
        ) : null}
        {rows.map((row) => {
          const gestureOptions = gestureOptionsFor(row.inputKind);
          const hasCount = row.gestureKind === 'tap' || row.gestureKind === 'swing';
          const hasState = hasCount;
          const isMesh = row.action === 'mesh_on' || row.action === 'mesh_off' || row.action === 'mesh_bri';
          return (
            <div key={row.id} className="flex flex-wrap items-end gap-2 rounded-lg border border-border p-2">
              <label className="flex flex-col gap-1 text-xs">
                <span className="text-muted"><Trans k="label.gestInput">Input</Trans></span>
                <Select value={`${row.inputKind}-${row.inputIndex}`} onValueChange={(v) => setRowInput(row.id, v)}>
                  <SelectTrigger className="w-40"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    {inputOptions.map((o) => (
                      <SelectItem key={o.value} value={o.value}>{o.label}</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </label>

              <label className="flex flex-col gap-1 text-xs">
                <span className="text-muted"><Trans k="label.gestGesture">Gesture</Trans></span>
                <Select value={row.gestureKind} onValueChange={(v) => updateRow(row.id, { gestureKind: v as GestureKind })}>
                  <SelectTrigger className="w-36"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    {gestureOptions.map((o) => (
                      <SelectItem key={o.value} value={o.value}>{o.label}</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </label>

              {hasCount ? (
                <label className="flex flex-col gap-1 text-xs">
                  <span className="text-muted">N</span>
                  <Input
                    type="number"
                    min={1}
                    max={20}
                    className="w-16"
                    value={row.count}
                    onChange={(e) => updateRow(row.id, { count: Math.max(1, parseInt(e.target.value, 10) || 1) })}
                  />
                </label>
              ) : null}

              {hasState ? (
                <label className="flex flex-col gap-1 text-xs">
                  <span className="text-muted"><Trans k="label.gestStart">Start</Trans></span>
                  <Select value={row.state} onValueChange={(v) => updateRow(row.id, { state: v })}>
                    <SelectTrigger className="w-32"><SelectValue /></SelectTrigger>
                    <SelectContent>
                      {stateOptionsFor(row.gestureKind).map((o) => (
                        <SelectItem key={o.value || 'either'} value={o.value}>{o.label}</SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </label>
              ) : null}

              <label className="flex flex-col gap-1 text-xs">
                <span className="text-muted"><Trans k="label.gestAction">Action</Trans></span>
                <Select value={row.action} onValueChange={(v) => updateRow(row.id, { action: v as ActionKind })}>
                  <SelectTrigger className="w-40"><SelectValue /></SelectTrigger>
                  <SelectContent>
                    <SelectItem value="quick">{t('label.actionQuick', 'Cycle quick mode')}</SelectItem>
                    <SelectItem value="toggle">{t('label.actionToggle', 'Toggle on/off')}</SelectItem>
                    <SelectItem value="on">{t('label.actionOn', 'Turn on')}</SelectItem>
                    <SelectItem value="off">{t('label.actionOff', 'Turn off')}</SelectItem>
                    <SelectItem value="bri">{t('label.actionBri', 'Set brightness')}</SelectItem>
                    <SelectItem value="mesh_on">{t('label.actionMeshOn', 'Mesh: turn on')}</SelectItem>
                    <SelectItem value="mesh_off">{t('label.actionMeshOff', 'Mesh: turn off')}</SelectItem>
                    <SelectItem value="mesh_bri">{t('label.actionMeshBri', 'Mesh: set brightness')}</SelectItem>
                  </SelectContent>
                </Select>
              </label>

              {row.action === 'bri' ? (
                <label className="flex flex-col gap-1 text-xs">
                  <span className="text-muted">%</span>
                  <Input
                    type="number"
                    min={0}
                    max={100}
                    className="w-20"
                    value={row.param1}
                    onChange={(e) => updateRow(row.id, { param1: e.target.value })}
                  />
                </label>
              ) : null}

              {isMesh ? (
                <label className="flex flex-col gap-1 text-xs">
                  <span className="text-muted"><Trans k="label.gestMeshAddr">Lamp</Trans></span>
                  {meshNodes.length > 0 ? (
                    <>
                      <Select
                        value={meshNodes.some((n) => matchesMeshAddr(row.param1, n.addr)) ? row.param1 : CUSTOM_MESH_ADDR}
                        onValueChange={(v) => updateRow(row.id, { param1: v === CUSTOM_MESH_ADDR ? '' : v })}
                      >
                        <SelectTrigger className="w-40"><SelectValue /></SelectTrigger>
                        <SelectContent>
                          {meshNodes.map((n) => (
                            <SelectItem key={n.addr} value={`0x${n.addr.toString(16)}`}>
                              {n.name || fallbackNodeLabel(n.addr)}
                            </SelectItem>
                          ))}
                          <SelectItem value={CUSTOM_MESH_ADDR}>{t('label.gestMeshCustom', 'Custom address…')}</SelectItem>
                        </SelectContent>
                      </Select>
                      {!meshNodes.some((n) => matchesMeshAddr(row.param1, n.addr)) ? (
                        <Input
                          placeholder="0xC001"
                          className="w-24"
                          value={row.param1}
                          onChange={(e) => updateRow(row.id, { param1: e.target.value })}
                        />
                      ) : null}
                    </>
                  ) : (
                    <Input
                      placeholder="0xC001"
                      className="w-24"
                      value={row.param1}
                      onChange={(e) => updateRow(row.id, { param1: e.target.value })}
                    />
                  )}
                </label>
              ) : null}

              {row.action === 'mesh_bri' ? (
                <label className="flex flex-col gap-1 text-xs">
                  <span className="text-muted">%</span>
                  <Input
                    type="number"
                    min={0}
                    max={100}
                    className="w-20"
                    value={row.param2}
                    onChange={(e) => updateRow(row.id, { param2: e.target.value })}
                  />
                </label>
              ) : null}

              {isMesh ? (
                <Button variant="ghost" onClick={() => sendMeshTest(row)} title={t('btn.gestSendTest', 'Send test')}>
                  <Send className="h-4 w-4" />
                </Button>
              ) : null}

              <button
                className="text-muted hover:text-foreground"
                onClick={() => removeRow(row.id)}
                title={t('btn.gestRemove', 'Remove binding')}
              >
                <MinusCircle className="h-4 w-4" />
              </button>
            </div>
          );
        })}

        <div className="flex gap-2">
          <Button onClick={addRow}>
            <PlusCircle className="mr-1 h-4 w-4" /> <Trans k="btn.gestAdd">Add binding</Trans>
          </Button>
          <Button variant="primary" onClick={save}>
            <Trans k="btn.save">Save</Trans>
          </Button>
          <Button onClick={refreshStatus}>
            <RefreshCw className="mr-1 h-4 w-4" /> <Trans k="btn.reload">Reload</Trans>
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}
