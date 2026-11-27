// Parses/drives the BLE Mesh self-provisioning firmware feature (src/mesh.cpp)
// from its text-feedback responses. There's no dedicated response channel or
// request/response correlation in this firmware (see useBle.ts/bleClient.ts -
// every sendCmd() reply, like every other log line, lands on the shared
// status/notify characteristic and is just appended to the Log tab's array) -
// same constraint OTA already works around via lib/ota.ts's `waitForLine`.
// This module generalizes that one-line waiter into also collecting the
// multi-line "[Mesh] <Thing> (<n>):" + n fixed-shape follow-on lines that
// mesh.cpp's meshKnownNodesText()/meshProvisionListText() produce (see those
// functions' own comments for the exact wire shape this depends on).

export type MeshNode = { addr: number; uuidShort: string; name: string };
export type MeshDiscovered = { idx: number; uuidShort: string; rssi: number };

type LogLine = { line: string };
type GetLog = () => LogLine[];

function pollUntil<T>(check: () => T | undefined, timeoutMs: number, intervalMs = 80): Promise<T> {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const tick = () => {
      const result = check();
      if (result !== undefined) {
        resolve(result);
        return;
      }
      if (Date.now() - start > timeoutMs) {
        reject(new Error('Timed out waiting for a response from the lamp'));
        return;
      }
      setTimeout(tick, intervalMs);
    };
    tick();
  });
}

/** Waits for a "[Mesh] <headerWord> (<n>):" header after `since`, then for all n
 * follow-on lines matching `lineRe` to show up, and maps each match with `map`.
 * Resolves with an empty array immediately once the header reports n=0. */
function collectList<T>(
  getLog: GetLog,
  since: number,
  headerRe: RegExp,
  lineRe: RegExp,
  map: (m: RegExpMatchArray) => T,
  timeoutMs: number,
): Promise<T[]> {
  return pollUntil(() => {
    const log = getLog();
    for (let i = since; i < log.length; i++) {
      const header = log[i].line.match(headerRe);
      if (!header) continue;
      const count = parseInt(header[1], 10);
      if (count === 0) return [];
      const items: T[] = [];
      for (let j = i + 1; j < log.length && items.length < count; j++) {
        const m = log[j].line.match(lineRe);
        if (m) items.push(map(m));
      }
      return items.length === count ? items : undefined;
    }
    return undefined;
  }, timeoutMs);
}

export async function fetchKnownNodes(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog): Promise<MeshNode[]> {
  const since = getLog().length;
  await sendCmd('mesh nodes');
  return collectList(
    getLog,
    since,
    /^\[Mesh\] Known nodes \((\d+)\):/,
    /^\s*NODE addr=0x([0-9a-f]+) uuid=([0-9a-f]+) name=(.*)$/i,
    (m) => ({ addr: parseInt(m[1], 16), uuidShort: m[2], name: m[3] ?? '' }),
    4000,
  );
}

export async function fetchDiscovered(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog): Promise<MeshDiscovered[]> {
  const since = getLog().length;
  await sendCmd('mesh provision list');
  return collectList(
    getLog,
    since,
    /^\[Mesh\] Discovered \((\d+)\):/,
    /^\s*DEV idx=(\d+) uuid=([0-9a-f]+) rssi=(-?\d+)$/i,
    (m) => ({ idx: parseInt(m[1], 10), uuidShort: m[2], rssi: parseInt(m[3], 10) }),
    4000,
  );
}

/** Starts a provisioner scan and waits for the firmware's immediate ack line
 * (not the scan's actual duration) - callers then poll fetchDiscovered()
 * themselves on an interval while the scan runs. */
export async function startScan(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog, seconds: number): Promise<void> {
  const since = getLog().length;
  await sendCmd(`mesh provision scan ${seconds}`);
  await pollUntil(() => {
    const log = getLog();
    for (let i = since; i < log.length; i++) {
      if (log[i].line.startsWith('[Mesh] Scanning') || log[i].line.startsWith('[Mesh] BLE scan busy') ||
          log[i].line.startsWith('[Mesh] Run \'mesh network create\'')) {
        if (log[i].line.startsWith('[Mesh] Scanning')) return true;
        throw new Error(log[i].line.replace(/^\[Mesh\]\s*/, ''));
      }
    }
    return undefined;
  }, 3000);
}

export async function stopScan(sendCmd: (cmd: string) => Promise<void>): Promise<void> {
  await sendCmd('mesh provision stop');
}

/** Provisions discovered-list entry `idx` (1-based, per fetchDiscovered()'s
 * own idx field) and follows the mesh.cpp provisioning state machine's own
 * [Mesh] progress lines through to success or failure. `onProgress` fires
 * once per new line (not re-fired on every poll tick). */
export function provisionDevice(
  sendCmd: (cmd: string) => Promise<void>,
  getLog: GetLog,
  idx: number,
  group: number | undefined,
  onProgress?: (line: string) => void,
): Promise<{ addr: number }> {
  return new Promise((resolve, reject) => {
    const since = getLog().length;
    let seen = since;
    const cmd = `mesh provision add ${idx}${group ? ` 0x${group.toString(16)}` : ''}`;
    sendCmd(cmd)
      .then(() => {
        const start = Date.now();
        const tick = () => {
          const log = getLog();
          while (seen < log.length) {
            const line = log[seen].line;
            seen++;
            if (!line.startsWith('[Mesh]')) continue;
            onProgress?.(line);
            const ok = line.match(/^\[Mesh\] Provisioned \+ configured 0x([0-9a-f]+)/i);
            if (ok) {
              resolve({ addr: parseInt(ok[1], 16) });
              return;
            }
            const fail = line.match(/^\[Mesh\] Provisioning (?:0x[0-9a-f]+ )?(?:start )?failed: (.*)$/i);
            if (fail) {
              reject(new Error(fail[1]));
              return;
            }
            if (/^\[Mesh\] (No such discovered device|A provisioning session is already in progress)/.test(line)) {
              reject(new Error(line.replace(/^\[Mesh\]\s*/, '')));
              return;
            }
          }
          if (Date.now() - start > 30000) {
            reject(new Error('Timed out waiting for provisioning to finish'));
            return;
          }
          setTimeout(tick, 100);
        };
        tick();
      })
      .catch(reject);
  });
}

async function sendAndWaitPrefix(
  sendCmd: (cmd: string) => Promise<void>,
  getLog: GetLog,
  cmd: string,
  prefix: string,
  timeoutMs = 4000,
): Promise<string> {
  const since = getLog().length;
  await sendCmd(cmd);
  return pollUntil(() => {
    const log = getLog();
    for (let i = since; i < log.length; i++) {
      if (log[i].line.startsWith(prefix)) return log[i].line;
    }
    return undefined;
  }, timeoutMs);
}

export async function renameNode(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog, addr: number, name: string): Promise<void> {
  const reply = await sendAndWaitPrefix(sendCmd, getLog, `mesh rename 0x${addr.toString(16)} ${name}`, '[Mesh]');
  if (!reply.startsWith('[Mesh] Renamed')) throw new Error(reply.replace(/^\[Mesh\]\s*/, ''));
}

export async function forgetNode(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog, addr: number): Promise<void> {
  const reply = await sendAndWaitPrefix(sendCmd, getLog, `mesh provision remove 0x${addr.toString(16)}`, '[Mesh]');
  if (!reply.startsWith('[Mesh] Forgot')) throw new Error(reply.replace(/^\[Mesh\]\s*/, ''));
}

export async function createOrJoinNetwork(
  sendCmd: (cmd: string) => Promise<void>,
  getLog: GetLog,
  netKeyHex?: string,
  appKeyHex?: string,
): Promise<string> {
  const cmd = netKeyHex && appKeyHex ? `mesh network create ${netKeyHex} ${appKeyHex}` : 'mesh network create';
  const reply = await sendAndWaitPrefix(sendCmd, getLog, cmd, '[Mesh]', 5000);
  if (!reply.startsWith('[Mesh] Network identity')) throw new Error(reply.replace(/^\[Mesh\]\s*/, ''));
  return reply;
}

/** This lamp's current NetKey/AppKey as hex, or throws if it has no network
 * identity yet ("mesh network create" never ran). */
export async function fetchKeys(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog): Promise<{ netKeyHex: string; appKeyHex: string }> {
  const reply = await sendAndWaitPrefix(sendCmd, getLog, 'mesh keys', '[Mesh]');
  const m = reply.match(/net=([0-9a-f]{32})\s+app=([0-9a-f]{32})/i);
  if (!m) throw new Error(reply.replace(/^\[Mesh\]\s*/, ''));
  return { netKeyHex: m[1], appKeyHex: m[2] };
}

/** The raw nRF Mesh (Mesh Configuration Database) JSON export text - see
 * meshExportNrfJson() in src/mesh.cpp for exactly what it contains and why.
 * Throws with the firmware's own error text if there's no network identity
 * yet (same "[Mesh] No network identity..." feedback fetchKeys() can hit). */
export async function fetchNrfExport(sendCmd: (cmd: string) => Promise<void>, getLog: GetLog): Promise<string> {
  const since = getLog().length;
  await sendCmd('mesh export nrf');
  const line = await pollUntil(() => {
    const log = getLog();
    for (let i = since; i < log.length; i++) {
      const l = log[i].line;
      if (l.startsWith('{') || l.startsWith('[Mesh]')) return l;
    }
    return undefined;
  }, 6000);
  if (!line.startsWith('{')) throw new Error(line.replace(/^\[Mesh\]\s*/, ''));
  return line;
}

/** `addr` as the same lowercase "0x1a2b" form the rest of this module reads
 * back from firmware log lines - used for display and for round-tripping a
 * node's address into a command. */
export function fmtAddr(addr: number): string {
  return `0x${addr.toString(16)}`;
}

/** Best-effort "lamp X" fallback label for a node with no user-given name. */
export function fallbackNodeLabel(addr: number): string {
  return `Lamp ${fmtAddr(addr)}`;
}
