import { useEffect, useRef, useState } from 'react';
import { AlertTriangle, CheckCircle2, Download, RefreshCw } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { useConnection } from '@/context/connection';
import { compareVersions, createLineWaiter, fetchOtaBinary, fetchOtaManifest, OtaManifest, runOtaUpdate } from '@/lib/ota';
import { Trans } from '@/i18n';

const BLE_CHUNK_BYTES = 180;
const SERIAL_CHUNK_BYTES = 4096;

type Phase = 'idle' | 'checking' | 'up-to-date' | 'available' | 'confirm' | 'updating' | 'success' | 'error';

export function OtaUpdateCard() {
  const { status, sendCmd, sendCmdReliable, transport, log } = useConnection();
  const [manifest, setManifest] = useState<OtaManifest | null>(null);
  const [phase, setPhase] = useState<Phase>('idle');
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState(0);
  const logRef = useRef(log);
  logRef.current = log;

  useEffect(() => {
    setPhase('checking');
    fetchOtaManifest().then((m) => {
      setManifest(m);
      setPhase('idle');
    });
  }, []);

  const env = status.firmwareEnv;
  const entry = manifest && env ? manifest.envs[env] : undefined;
  const isNewer = entry ? compareVersions(entry.version, status.firmwareVersion ?? '0.0.0') > 0 : false;

  useEffect(() => {
    if (phase !== 'idle' && phase !== 'available' && phase !== 'up-to-date') return;
    if (!manifest || !env) return;
    setPhase(entry ? (isNewer ? 'available' : 'up-to-date') : 'idle');
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [manifest, env, isNewer]);

  const startUpdate = async () => {
    if (!entry || !env) return;
    setPhase('updating');
    setProgress(0);
    setError(null);
    try {
      const bytes = await fetchOtaBinary(env);
      const waitForLine = createLineWaiter(() => logRef.current);
      const chunkSize = transport === 'ble' ? BLE_CHUNK_BYTES : SERIAL_CHUNK_BYTES;
      const sendChunk = transport === 'ble' ? sendCmdReliable : sendCmd;
      await runOtaUpdate({
        bytes,
        md5: entry.md5,
        version: entry.version,
        chunkSize,
        sendCmd,
        sendChunk,
        waitForLine,
        logLength: () => logRef.current.length,
        onProgress: ({ sentBytes, totalBytes }) => setProgress(Math.round((sentBytes / totalBytes) * 100)),
      });
      setPhase('success');
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setPhase('error');
    }
  };

  if (!status.connected) return null;

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.otaUpdate">Firmware Update</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="flex flex-wrap items-center justify-between gap-2 text-sm">
          <span className="text-muted">
            <Trans k="label.currentVersion">Current</Trans>: {status.firmwareVersion ?? '?'}
            {status.firmwareEnv ? ` (${status.firmwareEnv})` : ''}
          </span>
          {entry && (
            <span className="text-muted">
              <Trans k="label.availableVersion">Available</Trans>: {entry.version}
            </span>
          )}
        </div>

        {phase === 'checking' && (
          <p className="text-xs text-muted"><Trans k="label.otaChecking">Checking for updates…</Trans></p>
        )}

        {phase === 'idle' && !env && (
          <p className="text-xs text-muted">
            <AlertTriangle className="mr-1 inline h-3.5 w-3.5" />
            <Trans k="label.otaUnknownVariant">
              Can't determine which firmware variant this device is running - update unavailable.
            </Trans>
          </p>
        )}

        {phase === 'idle' && env && !entry && manifest && (
          <p className="text-xs text-muted">
            <Trans k="label.otaNoBundle">No bundled firmware found for this variant.</Trans>
          </p>
        )}

        {phase === 'idle' && !manifest && (
          <p className="text-xs text-muted">
            <Trans k="label.otaNoManifest">
              No firmware bundle found. Run "npm run ota:prepare" to generate one.
            </Trans>
          </p>
        )}

        {phase === 'up-to-date' && (
          <p className="flex items-center gap-1.5 text-xs text-emerald-400">
            <CheckCircle2 className="h-3.5 w-3.5" /> <Trans k="label.otaUpToDate">Up to date</Trans>
          </p>
        )}

        {phase === 'available' && (
          <Button onClick={() => setPhase('confirm')}>
            <Download className="mr-1 h-4 w-4" /> <Trans k="btn.otaUpdate">Update available - install</Trans>
          </Button>
        )}

        {phase === 'updating' && (
          <div className="space-y-1">
            <div className="h-2 w-full overflow-hidden rounded-full bg-panel">
              <div className="h-full rounded-full bg-accent transition-all" style={{ width: `${progress}%` }} />
            </div>
            <p className="flex items-center gap-1.5 text-xs text-muted">
              <RefreshCw className="h-3.5 w-3.5 animate-spin" />
              <Trans k="label.otaUploading">Uploading</Trans> {progress}%
            </p>
          </div>
        )}

        {phase === 'success' && (
          <p className="flex items-center gap-1.5 text-xs text-emerald-400">
            <CheckCircle2 className="h-3.5 w-3.5" />
            <Trans k="label.otaSuccess">Update successful - device is rebooting, it will auto-reconnect.</Trans>
          </p>
        )}

        {phase === 'error' && (
          <div className="space-y-2">
            <p className="flex items-center gap-1.5 text-xs text-destructive">
              <AlertTriangle className="h-3.5 w-3.5" /> {error}
            </p>
            <Button variant="ghost" onClick={() => setPhase(isNewer ? 'available' : 'idle')}>
              <Trans k="btn.dismiss">Dismiss</Trans>
            </Button>
          </div>
        )}
      </CardContent>

      <Dialog open={phase === 'confirm'} onOpenChange={(o) => !o && setPhase('available')}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle><Trans k="title.confirmOta">Install firmware update?</Trans></DialogTitle>
            <DialogDescription>
              <Trans k="desc.confirmOta">
                This uploads the new firmware over the current connection and reboots the device
                when done. Don't disconnect or power off the lamp during the update.
              </Trans>
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="ghost" onClick={() => setPhase('available')}>
              <Trans k="btn.cancel">Cancel</Trans>
            </Button>
            <Button onClick={() => startUpdate()}>
              <Download className="mr-1 h-4 w-4" /> <Trans k="btn.otaInstall">Install</Trans>
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </Card>
  );
}
