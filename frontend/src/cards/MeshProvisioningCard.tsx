import { useEffect, useRef, useState } from 'react';
import { ChevronDown, ChevronRight, Download, Loader2, Pencil, PlusCircle, RefreshCw, Trash2, X } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import {
  createOrJoinNetwork,
  fallbackNodeLabel,
  fetchDiscovered,
  fetchKnownNodes,
  fetchNrfExport,
  forgetNode,
  MeshDiscovered,
  MeshNode,
  provisionDevice,
  renameNode,
  resetMeshIdentity,
  startScan,
  stopScan,
} from '@/lib/meshProtocol';
import { Trans, useI18n } from '@/i18n';
import { DeviceStatus } from '@/hooks/status';

// Plain, three-way status badge - this card's own "am I a provisioner, a
// provisioned node, or neither yet" display (see src/mesh.cpp's
// meshRoleText()/the mesh_role= status field). Kept dumb/obvious on purpose:
// the whole point is a user who has no idea what "provisioner" even means
// can still tell at a glance what state this exact lamp is in, since mixing
// the two roles up is the single most confusing thing about self-hosted
// mesh setup.
function MeshRoleBadge({ role }: { role: DeviceStatus['meshRole'] }) {
  const { t } = useI18n();
  if (role === 'provisioner') {
    return (
      <span className="chip-muted shrink-0 bg-accent/15 text-accent">
        {t('label.meshRoleProvisioner', 'Provisioner (manages other lamps)')}
      </span>
    );
  }
  if (role === 'node') {
    return (
      <span className="chip-muted shrink-0 bg-emerald-500/15 text-emerald-400">
        {t('label.meshRoleNode', 'Provisioned (controlled by another provisioner)')}
      </span>
    );
  }
  return (
    <span className="chip-muted shrink-0">
      {t('label.meshRoleNone', 'Not part of a mesh yet')}
    </span>
  );
}

// Real device-management UI on top of the self-provisioning firmware
// feature (src/mesh.cpp) - lamps by friendly name, scan results as clickable
// rows, inline progress - instead of the raw hex-address/numbered-index
// trigger buttons this card started as. There's still no shared-database or
// push notification for this: every list here is fetched on demand (see
// lib/meshProtocol.ts) by sending a command and parsing the plain-text
// [Mesh] reply lines that show up on the same log stream the Log tab reads.
export function MeshProvisioningCard() {
  const { t } = useI18n();
  const { status, sendCmd, log } = useConnection();
  const logRef = useRef(log);
  logRef.current = log;
  const getLog = () => logRef.current;

  const [netKeyInput, setNetKeyInput] = useState('');
  const [appKeyInput, setAppKeyInput] = useState('');
  const [networkBusy, setNetworkBusy] = useState(false);
  const [networkError, setNetworkError] = useState<string | null>(null);
  const [showAdvanced, setShowAdvanced] = useState(false);

  const [nodes, setNodes] = useState<MeshNode[]>([]);
  const [nodesLoading, setNodesLoading] = useState(false);
  const [editingAddr, setEditingAddr] = useState<number | null>(null);
  const [editingName, setEditingName] = useState('');
  const [busyAddr, setBusyAddr] = useState<number | null>(null);

  const [scanning, setScanning] = useState(false);
  const [scanSeconds, setScanSeconds] = useState(20);
  const [scanError, setScanError] = useState<string | null>(null);
  const [discovered, setDiscovered] = useState<MeshDiscovered[]>([]);
  const [provisioning, setProvisioning] = useState<{ idx: number; progress: string } | null>(null);
  const [provisionError, setProvisionError] = useState<string | null>(null);

  const [exportBusy, setExportBusy] = useState(false);
  const [exportError, setExportError] = useState<string | null>(null);

  const [resetOpen, setResetOpen] = useState(false);
  const [resetBusy, setResetBusy] = useState(false);
  const [resetError, setResetError] = useState<string | null>(null);
  const [resetDone, setResetDone] = useState(false);

  const refreshNodes = async () => {
    setNodesLoading(true);
    try {
      setNodes(await fetchKnownNodes(sendCmd, getLog));
    } catch {
      // Lamp didn't answer in time - leave the previous list showing rather
      // than flashing it empty; the user can hit Refresh again.
    } finally {
      setNodesLoading(false);
    }
  };

  useEffect(() => {
    if (status.meshNet) refreshNodes();
    // Re-fetch whenever the cheap status count changes (a provision/forget
    // elsewhere, e.g. the gesture card's own test, changed it) - avoids the
    // list silently going stale without the user remembering to hit Refresh.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status.meshNet, status.meshNodeCount]);

  useEffect(() => {
    if (!scanning) return;
    const id = setInterval(() => {
      fetchDiscovered(sendCmd, getLog)
        .then(setDiscovered)
        .catch(() => undefined);
    }, 1500);
    return () => clearInterval(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [scanning]);

  const createOrJoin = async () => {
    setNetworkBusy(true);
    setNetworkError(null);
    try {
      await createOrJoinNetwork(sendCmd, getLog, netKeyInput || undefined, appKeyInput || undefined);
      setNetKeyInput('');
      setAppKeyInput('');
      await refreshNodes();
    } catch (e) {
      setNetworkError(e instanceof Error ? e.message : String(e));
    } finally {
      setNetworkBusy(false);
    }
  };

  const startScanning = async () => {
    setScanError(null);
    setDiscovered([]);
    try {
      await startScan(sendCmd, getLog, scanSeconds);
      setScanning(true);
      setTimeout(() => setScanning(false), scanSeconds * 1000);
    } catch (e) {
      setScanError(e instanceof Error ? e.message : String(e));
    }
  };

  const stopScanning = async () => {
    setScanning(false);
    await stopScan(sendCmd).catch(() => undefined);
  };

  const addToMesh = async (dev: MeshDiscovered) => {
    setProvisionError(null);
    setProvisioning({ idx: dev.idx, progress: t('label.meshProvisioning', 'Provisioning…') });
    try {
      await provisionDevice(sendCmd, getLog, dev.idx, undefined, (line) =>
        setProvisioning({ idx: dev.idx, progress: line.replace(/^\[Mesh\]\s*/, '') }),
      );
      setDiscovered((prev) => prev.filter((d) => d.idx !== dev.idx));
      await refreshNodes();
    } catch (e) {
      setProvisionError(e instanceof Error ? e.message : String(e));
    } finally {
      setProvisioning(null);
    }
  };

  const startRename = (node: MeshNode) => {
    setEditingAddr(node.addr);
    setEditingName(node.name || '');
  };

  const commitRename = async () => {
    if (editingAddr === null) return;
    const addr = editingAddr;
    const name = editingName;
    setEditingAddr(null);
    setBusyAddr(addr);
    try {
      await renameNode(sendCmd, getLog, addr, name);
      setNodes((prev) => prev.map((n) => (n.addr === addr ? { ...n, name } : n)));
    } catch {
      refreshNodes();
    } finally {
      setBusyAddr(null);
    }
  };

  const removeNode = async (node: MeshNode) => {
    setBusyAddr(node.addr);
    try {
      await forgetNode(sendCmd, getLog, node.addr);
      setNodes((prev) => prev.filter((n) => n.addr !== node.addr));
    } catch {
      refreshNodes();
    } finally {
      setBusyAddr(null);
    }
  };

  // Downloads this lamp's network identity + known nodes as an nRF Mesh
  // (Mesh Configuration Database) compatible JSON file - lets an external
  // tool (e.g. Home Assistant's own mesh plugin, if it supports an
  // "import existing network" flow) pick up the same network without
  // actually re-provisioning anything through it. See meshExportNrfJson()
  // in src/mesh.cpp for the exact shape and what it deliberately omits.
  const exportNrf = async () => {
    setExportBusy(true);
    setExportError(null);
    try {
      const json = await fetchNrfExport(sendCmd, getLog);
      const blob = new Blob([json], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'quarzlampe-mesh-export.json';
      a.click();
      URL.revokeObjectURL(url);
    } catch (e) {
      setExportError(e instanceof Error ? e.message : String(e));
    } finally {
      setExportBusy(false);
    }
  };

  // Resets this lamp's own mesh identity (role, network, known nodes) back
  // to factory-fresh - see meshResetIdentity() in src/mesh.cpp. Does NOT
  // touch any other lamp in the mesh; they stay exactly as provisioned.
  const resetMesh = async () => {
    setResetBusy(true);
    setResetError(null);
    try {
      await resetMeshIdentity(sendCmd, getLog);
      setResetDone(true);
      setNodes([]);
    } catch (e) {
      setResetError(e instanceof Error ? e.message : String(e));
    } finally {
      setResetBusy(false);
    }
  };

  const rssiBars = (rssi: number) => (rssi > -60 ? 3 : rssi > -75 ? 2 : 1);

  return (
    <Card>
      <CardHeader className="items-start">
        <div>
          <CardTitle><Trans k="title.meshProv">Mesh Network</Trans></CardTitle>
          <p className="text-sm text-muted">
            <Trans k="desc.meshProv">
              Join multiple lamps into one network so they can relay to each other and be controlled together.
            </Trans>
          </p>
        </div>
        <MeshRoleBadge role={status.meshRole} />
      </CardHeader>
      <CardContent>
        {status.meshRole === 'node' ? (
          <div className="space-y-2 rounded-lg border border-border p-3">
            <p className="text-sm text-muted">
              <Trans k="desc.meshIsNode">
                This lamp was provisioned into someone else's mesh network (e.g. via another lamp's "Add to mesh",
                or an app like nRF Mesh) - it's controlled from there, not managed from this page.
              </Trans>
            </p>
          </div>
        ) : !status.meshNet ? (
          <div className="space-y-2 rounded-lg border border-border p-3">
            <div className="text-sm font-medium"><Trans k="label.meshNotSetUp">No mesh network yet</Trans></div>
            <p className="text-xs text-muted">
              <Trans k="desc.meshNetworkHint">
                First lamp: leave both fields blank to create a fresh network. Every other lamp: paste the same two
                hex values the first lamp's own feedback printed.
              </Trans>
            </p>
            <div className="flex flex-wrap items-end gap-2">
              <Input
                placeholder={t('label.meshNetKey', 'NetKey (32 hex, blank = generate)')}
                className="w-64"
                value={netKeyInput}
                onChange={(e) => setNetKeyInput(e.target.value)}
              />
              <Input
                placeholder={t('label.meshAppKey', 'AppKey (32 hex, blank = generate)')}
                className="w-64"
                value={appKeyInput}
                onChange={(e) => setAppKeyInput(e.target.value)}
              />
              <Button variant="primary" disabled={networkBusy} onClick={createOrJoin}>
                {networkBusy ? <Loader2 className="h-4 w-4 animate-spin" /> : null}
                {netKeyInput && appKeyInput ? (
                  <Trans k="btn.meshJoin">Join</Trans>
                ) : (
                  <Trans k="btn.meshCreate">Create network</Trans>
                )}
              </Button>
            </div>
            {networkError ? <p className="text-xs text-red-400">{networkError}</p> : null}
          </div>
        ) : (
          <>
            <div className="space-y-2">
              <div className="flex items-center justify-between">
                <div className="text-sm font-medium">
                  <Trans k="label.meshLampsInNetwork">Lamps in this mesh</Trans>
                  {' '}({nodes.length})
                </div>
                <Button variant="ghost" size="sm" onClick={refreshNodes} disabled={nodesLoading}>
                  <RefreshCw className={`h-4 w-4 ${nodesLoading ? 'animate-spin' : ''}`} />
                </Button>
              </div>
              {nodes.length === 0 ? (
                <p className="text-sm text-muted"><Trans k="label.meshNone">No lamps in this mesh yet - scan for one below.</Trans></p>
              ) : (
                <div className="space-y-1">
                  {nodes.map((node) => (
                    <div key={node.addr} className="flex items-center gap-2 rounded-lg border border-border px-2 py-1.5">
                      {editingAddr === node.addr ? (
                        <>
                          <Input
                            autoFocus
                            className="h-8 flex-1"
                            value={editingName}
                            onChange={(e) => setEditingName(e.target.value)}
                            onKeyDown={(e) => {
                              if (e.key === 'Enter') commitRename();
                              if (e.key === 'Escape') setEditingAddr(null);
                            }}
                          />
                          <Button size="sm" variant="primary" onClick={commitRename}>
                            <Trans k="btn.save">Save</Trans>
                          </Button>
                          <button className="text-muted hover:text-text" onClick={() => setEditingAddr(null)}>
                            <X className="h-4 w-4" />
                          </button>
                        </>
                      ) : (
                        <>
                          <div className="flex-1 min-w-0">
                            <div className="truncate text-sm">{node.name || fallbackNodeLabel(node.addr)}</div>
                            <div className="truncate text-xs text-muted">{`0x${node.addr.toString(16)}`}</div>
                          </div>
                          {busyAddr === node.addr ? (
                            <Loader2 className="h-4 w-4 animate-spin text-muted" />
                          ) : (
                            <>
                              <button
                                className="text-muted hover:text-text"
                                title={t('btn.meshRename', 'Rename')}
                                onClick={() => startRename(node)}
                              >
                                <Pencil className="h-4 w-4" />
                              </button>
                              <button
                                className="text-muted hover:text-red-400"
                                title={t('btn.meshForget', 'Forget')}
                                onClick={() => removeNode(node)}
                              >
                                <Trash2 className="h-4 w-4" />
                              </button>
                            </>
                          )}
                        </>
                      )}
                    </div>
                  ))}
                </div>
              )}
            </div>

            <div className="space-y-2">
              <div className="text-sm font-medium"><Trans k="label.meshFindNew">Find a new lamp</Trans></div>
              <div className="flex flex-wrap items-end gap-2">
                <Input
                  type="number"
                  min={5}
                  max={120}
                  className="w-24"
                  value={scanSeconds}
                  onChange={(e) => setScanSeconds(Number(e.target.value))}
                  suffix="s"
                  disabled={scanning}
                />
                {scanning ? (
                  <Button variant="ghost" onClick={stopScanning}><Trans k="btn.meshStop">Stop</Trans></Button>
                ) : (
                  <Button variant="primary" onClick={startScanning}><Trans k="btn.meshScan">Scan</Trans></Button>
                )}
                {scanning ? (
                  <span className="flex items-center gap-1 text-xs text-muted">
                    <Loader2 className="h-3 w-3 animate-spin" /> <Trans k="label.meshScanning">Scanning…</Trans>
                  </span>
                ) : null}
              </div>
              {scanError ? <p className="text-xs text-red-400">{scanError}</p> : null}
              {provisionError ? <p className="text-xs text-red-400">{provisionError}</p> : null}
              {discovered.length === 0 && scanning ? (
                <p className="text-sm text-muted"><Trans k="label.meshNoneFound">No lamps found yet…</Trans></p>
              ) : null}
              <div className="space-y-1">
                {discovered.map((dev) => {
                  const isBusy = provisioning?.idx === dev.idx;
                  return (
                    <div key={dev.idx} className="flex items-center gap-2 rounded-lg border border-border px-2 py-1.5">
                      <div className="flex gap-0.5" title={`RSSI ${dev.rssi}`}>
                        {[1, 2, 3].map((bar) => (
                          <span
                            key={bar}
                            className={`block w-1 rounded-sm ${bar <= rssiBars(dev.rssi) ? 'bg-accent' : 'bg-border'}`}
                            style={{ height: `${4 + bar * 3}px` }}
                          />
                        ))}
                      </div>
                      <div className="flex-1 min-w-0 truncate text-sm text-muted">
                        {isBusy ? provisioning!.progress : `uuid ${dev.uuidShort}`}
                      </div>
                      <Button size="sm" disabled={!!provisioning} onClick={() => addToMesh(dev)}>
                        {isBusy ? <Loader2 className="h-4 w-4 animate-spin" /> : <PlusCircle className="h-4 w-4" />}
                        {' '}<Trans k="btn.meshAddToMesh">Add to mesh</Trans>
                      </Button>
                    </div>
                  );
                })}
              </div>
            </div>

            <div>
              <button
                className="flex items-center gap-1 text-xs text-muted hover:text-text"
                onClick={() => setShowAdvanced((v) => !v)}
              >
                {showAdvanced ? <ChevronDown className="h-3 w-3" /> : <ChevronRight className="h-3 w-3" />}
                <Trans k="btn.meshAdvanced">Advanced</Trans>
              </button>
              {showAdvanced ? (
                <div className="mt-2 space-y-2 rounded-lg border border-border p-3">
                  <p className="text-xs text-muted">
                    <Trans k="desc.meshAdvancedJoin">
                      To let another lamp provision too, run the same network identity on it below.
                    </Trans>
                  </p>
                  <div className="flex flex-wrap items-end gap-2">
                    <Input
                      placeholder={t('label.meshNetKey', 'NetKey (32 hex, blank = generate)')}
                      className="w-64"
                      value={netKeyInput}
                      onChange={(e) => setNetKeyInput(e.target.value)}
                    />
                    <Input
                      placeholder={t('label.meshAppKey', 'AppKey (32 hex, blank = generate)')}
                      className="w-64"
                      value={appKeyInput}
                      onChange={(e) => setAppKeyInput(e.target.value)}
                    />
                    <Button disabled={networkBusy} onClick={createOrJoin}>
                      <Trans k="btn.meshCreate">Create / Join</Trans>
                    </Button>
                  </div>
                  {networkError ? <p className="text-xs text-red-400">{networkError}</p> : null}

                  <div className="border-t border-border pt-2 space-y-2">
                    <p className="text-xs text-muted">
                      <Trans k="desc.meshExportNrf">
                        Export this network (keys + known lamps) as an nRF Mesh file - for apps/integrations that can
                        import an existing network instead of provisioning through them directly.
                      </Trans>
                    </p>
                    <Button variant="ghost" size="sm" disabled={exportBusy} onClick={exportNrf}>
                      {exportBusy ? <Loader2 className="h-4 w-4 animate-spin" /> : <Download className="h-4 w-4" />}
                      {' '}<Trans k="btn.meshExportNrf">Export as nRF Mesh JSON</Trans>
                    </Button>
                    {exportError ? <p className="text-xs text-red-400">{exportError}</p> : null}
                  </div>

                  <div className="border-t border-border pt-2 space-y-2">
                    <p className="text-xs text-muted">
                      <Trans k="desc.meshResetThis">
                        Give up this lamp's role (Provisioner or whatever it currently is) and forget its network -
                        for starting over on this exact lamp. Other lamps already in the mesh are not affected.
                      </Trans>
                    </p>
                    <Dialog open={resetOpen} onOpenChange={(v) => { setResetOpen(v); if (v) { setResetError(null); setResetDone(false); } }}>
                      <DialogTrigger asChild>
                        <Button variant="danger" size="sm">
                          <Trash2 className="h-4 w-4" />
                          {' '}<Trans k="btn.meshResetThis">Reset this lamp's mesh identity</Trans>
                        </Button>
                      </DialogTrigger>
                      <DialogContent>
                        <DialogHeader>
                          <DialogTitle><Trans k="title.meshResetConfirm">Reset this lamp's mesh identity?</Trans></DialogTitle>
                          <DialogDescription>
                            <Trans k="desc.meshResetConfirm">
                              This clears this lamp's NetKey/AppKey, its role, and its list of known lamps - as if
                              "mesh network create" had never run. A reboot is needed afterwards for it to fully take
                              effect. Other lamps already provisioned into this mesh are NOT reset and keep working
                              with each other; this lamp just stops being their Provisioner (or whatever role it had).
                            </Trans>
                          </DialogDescription>
                        </DialogHeader>
                        {resetDone ? (
                          <p className="text-sm text-emerald-400">
                            <Trans k="label.meshResetDone">Done - reboot this lamp for it to take full effect.</Trans>
                          </p>
                        ) : (
                          <>
                            <DialogFooter className="flex justify-end gap-2">
                              <Button variant="ghost" onClick={() => setResetOpen(false)}>
                                <Trans k="btn.cancel">Cancel</Trans>
                              </Button>
                              <Button variant="danger" disabled={resetBusy} onClick={resetMesh}>
                                {resetBusy ? <Loader2 className="h-4 w-4 animate-spin" /> : null}
                                {' '}<Trans k="btn.confirm">Yes, reset</Trans>
                              </Button>
                            </DialogFooter>
                            {resetError ? <p className="text-xs text-red-400">{resetError}</p> : null}
                          </>
                        )}
                      </DialogContent>
                    </Dialog>
                  </div>
                </div>
              ) : null}
            </div>
          </>
        )}
      </CardContent>
    </Card>
  );
}
