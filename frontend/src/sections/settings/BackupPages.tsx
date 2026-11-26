import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Camera, ClipboardPaste, Copy, Download, Loader2, RefreshCw } from 'lucide-react';
import { QRCodeSVG } from 'qrcode.react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';
import { createLineWaiter } from '@/lib/ota';
import { SettingsSubpage } from './SettingsSubpage';

export function BackupImportPage() {
  const { sendCmd } = useConnection();
  const [cfgText, setCfgText] = useState('');
  const [scanOpen, setScanOpen] = useState(false);
  const [scanError, setScanError] = useState<string | null>(null);
  const [scanning, setScanning] = useState(false);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const rafRef = useRef<number | null>(null);
  type QRDetector = { detect: (input: any) => Promise<Array<{ rawValue: string }>> };
  const detectorRef = useRef<QRDetector | null>(null);

  const stopScan = () => {
    if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    rafRef.current = null;
    if (streamRef.current) {
      streamRef.current.getTracks().forEach((t) => t.stop());
      streamRef.current = null;
    }
    setScanning(false);
  };

  useEffect(() => {
    if (!scanOpen) stopScan();
    return () => stopScan();
  }, [scanOpen]);

  const startScan = async () => {
    setScanError(null);
    const Detector = typeof window !== 'undefined' ? (window as any).BarcodeDetector : undefined;
    if (!Detector || !(navigator.mediaDevices && navigator.mediaDevices.getUserMedia)) {
      setScanError('QR scanning not supported in this browser.');
      return;
    }
    try {
      detectorRef.current = new Detector({ formats: ['qr_code'] });
    } catch (e) {
      setScanError('QR scanning not supported in this browser.');
      return;
    }
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' } });
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await videoRef.current.play();
      }
      setScanning(true);
      const tick = async () => {
        if (!videoRef.current || !detectorRef.current) return;
        try {
          const codes = await detectorRef.current.detect(videoRef.current);
          if (codes.length > 0) {
            const val = codes[0].rawValue.trim();
            setCfgText(val);
            setScanOpen(false);
            stopScan();
            return;
          }
        } catch (e) {
          // ignore transient detection errors
        }
        rafRef.current = requestAnimationFrame(tick);
      };
      rafRef.current = requestAnimationFrame(tick);
    } catch (err) {
      console.warn(err);
      setScanError('Kamera-Zugriff fehlgeschlagen.');
      stopScan();
    }
  };

  return (
    <SettingsSubpage>
      <Card>
        <CardHeader>
          <CardTitle><Trans k="btn.import">Import</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex flex-wrap gap-2">
            <Input
              placeholder="cfg import key=val ..."
              className="min-w-[240px] flex-1"
              value={cfgText}
              onChange={(e) => setCfgText(e.target.value)}
            />
            <Button onClick={() => cfgText.trim() && sendCmd(cfgText).catch((e) => console.warn(e))}>
              <ClipboardPaste className="mr-1 h-4 w-4" /> <Trans k="btn.import">Import</Trans>
            </Button>
            <Dialog open={scanOpen} onOpenChange={setScanOpen}>
              <DialogTrigger asChild>
                <Button variant="ghost">
                  <Camera className="mr-1 h-4 w-4" /> <Trans k="btn.scanQr">Scan QR</Trans>
                </Button>
              </DialogTrigger>
              <DialogContent>
                <DialogHeader>
                  <DialogTitle><Trans k="title.scanQr">Scan cfg import QR</Trans></DialogTitle>
                  <DialogDescription><Trans k="desc.scanQr">Richte die Kamera auf den QR-Code mit der cfg import Zeile.</Trans></DialogDescription>
                </DialogHeader>
                <div className="mt-3 space-y-3">
                  {scanError && <div className="rounded-md bg-red-500/20 px-3 py-2 text-sm text-red-400">{scanError}</div>}
                  <div className="overflow-hidden rounded-lg border border-border">
                    <video ref={videoRef} className="aspect-video w-full bg-black/50" muted playsInline />
                  </div>
                  <div className="flex items-center justify-between text-sm text-muted">
                    <span>{scanning ? <Trans k="status.scanning">Scanning…</Trans> : <Trans k="status.ready">Bereit</Trans>}</span>
                    <Button size="sm" variant="ghost" onClick={startScan} disabled={scanning}>
                      <Camera className="mr-1 h-4 w-4" /> <Trans k="btn.startScan">Start</Trans>
                    </Button>
                  </div>
                </div>
              </DialogContent>
            </Dialog>
          </div>
        </CardContent>
      </Card>
    </SettingsSubpage>
  );
}

export function BackupExportPage() {
  const { t } = useI18n();
  const { sendCmd, log } = useConnection();
  const [exportText, setExportText] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  // The old version read `log` directly inside a setTimeout callback fired
  // from runCfgExport() - that callback closed over whatever `log` was AT
  // THE MOMENT runCfgExport ran (before the device's reply had arrived), so
  // it always searched a stale, reply-less log and silently found nothing.
  // A ref always reads the latest log instead, and createLineWaiter (already
  // used by OTA for the same "wait for a specific reply line" problem) polls
  // it properly instead of guessing a fixed delay.
  const logRef = useRef(log);
  logRef.current = log;
  const waitForLine = useMemo(() => createLineWaiter(() => logRef.current), []);

  const runCfgExport = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const sinceIndex = logRef.current.length;
      await sendCmd('cfg export');
      const line = await waitForLine((l) => l.includes('cfg import'), sinceIndex, 6000);
      setExportText(line.trim());
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }, [sendCmd, waitForLine]);

  // Load immediately on opening this page, instead of waiting for a click.
  useEffect(() => {
    runCfgExport();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const buildCfgLink = (cfg: string) => {
    const base = `${window.location.origin}${window.location.pathname}`;
    return `${base}?cfg=${encodeURIComponent(cfg)}`;
  };

  const saveToDisk = () => {
    const blob = new Blob([exportText], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `quarzlampe-backup-${new Date().toISOString().slice(0, 10)}.txt`;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <SettingsSubpage>
      <Card>
        <CardHeader>
          <CardTitle><Trans k="btn.exportQr">Export to text/QR</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          {loading && (
            <div className="flex items-center gap-2 text-sm text-muted">
              <Loader2 className="h-4 w-4 animate-spin" />
              {t('status.loadingExport', 'Lade Export-Daten…')}
            </div>
          )}
          {!loading && error && (
            <div className="flex items-center justify-between gap-2 rounded-md bg-red-500/20 px-3 py-2 text-sm text-red-400">
              <span>{error}</span>
              <Button size="sm" variant="ghost" onClick={runCfgExport}>
                <RefreshCw className="h-4 w-4" />
              </Button>
            </div>
          )}
          {!loading && !error && exportText && (
            <div className="flex flex-col gap-3">
              <div className="w-full rounded-lg border border-border bg-panel p-3">
                <QRCodeSVG value={buildCfgLink(exportText)} width={200} height={200} className="mx-auto" />
              </div>
              <div className="flex flex-wrap gap-2">
                <Button onClick={saveToDisk}>
                  <Download className="mr-1 h-4 w-4" /> {t('btn.saveToDisk', 'Save to disk')}
                </Button>
                <Button variant="ghost" onClick={() => navigator.clipboard.writeText(exportText)}>
                  <Copy className="mr-1 h-4 w-4" /> Copy
                </Button>
                <Button variant="ghost" onClick={() => navigator.clipboard.writeText(buildCfgLink(exportText))}>
                  <Copy className="mr-1 h-4 w-4" /> Link
                </Button>
                <Button variant="ghost" onClick={runCfgExport}>
                  <RefreshCw className="mr-1 h-4 w-4" /> <Trans k="btn.reload">Reload</Trans>
                </Button>
              </div>
              <Input className="w-full font-mono" value={exportText} readOnly />
            </div>
          )}
        </CardContent>
      </Card>
    </SettingsSubpage>
  );
}
