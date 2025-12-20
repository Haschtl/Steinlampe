import { useEffect, useRef, useState } from 'react';
import { Camera, ClipboardPaste, Copy } from 'lucide-react';
import { QRCodeSVG } from 'qrcode.react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function ProfilesCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { sendCmd, log } = useConnection();
  const [cfgText, setCfgText] = useState('');
  const [exportText, setExportText] = useState('');
  const [scanOpen, setScanOpen] = useState(false);
  const [scanError, setScanError] = useState<string | null>(null);
  const [scanning, setScanning] = useState(false);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const rafRef = useRef<number | null>(null);
  type QRDetector = { detect: (input: any) => Promise<Array<{ rawValue: string }>> };
  const detectorRef = useRef<QRDetector | null>(null);

  const grabLatestCfg = () => {
    const line = [...log].reverse().find((l) => l.line.includes('cfg import'));
    if (line) setExportText(line.line.trim());
  };

  const runCfgExport = () => {
    sendCmd('cfg export')
      .then(() => setTimeout(grabLatestCfg, 200))
      .catch((e) => console.warn(e));
  };

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

  const buildCfgLink = (cfg: string) => {
    const base = `${window.location.origin}${window.location.pathname}`;
    return `${base}?cfg=${encodeURIComponent(cfg)}`;
  };

  return (
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.import">Import / Export</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex flex-wrap gap-2">
            <div className="flex min-w-[240px] flex-1 gap-2">
              <Input placeholder="cfg import key=val ..." value={cfgText} onChange={(e) => setCfgText(e.target.value)} />
              <Button onClick={() => cfgText.trim() && sendCmd(cfgText)}>
                <ClipboardPaste className="mr-1 h-4 w-4" /> <Trans k="btn.import">Import</Trans>
              </Button>
            </div>
            <Dialog open={scanOpen} onOpenChange={setScanOpen}>
              <DialogTrigger asChild>
                <Button variant="secondary">
                  <Camera className="mr-1 h-4 w-4" /> <Trans k="btn.scanQr">Scan QR</Trans>
                </Button>
              </DialogTrigger>
              <DialogContent>
                <DialogHeader>
                  <DialogTitle><Trans k="title.scanQr">Scan cfg import QR</Trans></DialogTitle>
                  <DialogDescription><Trans k="desc.scanQr">Richte die Kamera auf den QR-Code mit der cfg import Zeile.</Trans></DialogDescription>
                </DialogHeader>
                <div className="mt-3 space-y-3">
                  {scanError && <div className="rounded-md bg-destructive/20 px-3 py-2 text-sm text-destructive">{scanError}</div>}
                  <div className="overflow-hidden rounded-lg border border-border">
                    <video ref={videoRef} className="aspect-video w-full bg-black/50" muted playsInline />
                  </div>
                  <div className="flex items-center justify-between text-sm text-muted">
                    <span>{scanning ? <Trans k="status.scanning">Scanning…</Trans> : <Trans k="status.ready">Bereit</Trans>}</span>
                    <Button size="sm" variant="outline" onClick={startScan} disabled={scanning}>
                      <Camera className="mr-1 h-4 w-4" /> <Trans k="btn.startScan">Start</Trans>
                    </Button>
                  </div>
                </div>
              </DialogContent>
            </Dialog>
          </div>
          <div className="flex flex-col gap-3">
            <div className="flex flex-wrap items-center gap-3">
              <Button onClick={runCfgExport}>
                <Copy className="mr-1 h-4 w-4" /> <Trans k="btn.exportQr">Export to text/QR</Trans>
              </Button>
              <Button variant="ghost" onClick={grabLatestCfg}>
                <ClipboardPaste className="mr-1 h-4 w-4" /> <Trans k="btn.reload">Reload</Trans>
              </Button>
            </div>
            {exportText && (
              <div className="flex flex-col gap-2">
                <div className="flex items-center gap-2">
                  <Input className="w-full font-mono" value={exportText} readOnly />
                  <Button size="sm" onClick={() => navigator.clipboard.writeText(exportText)}>
                    <Copy className="mr-1 h-4 w-4" /> Copy
                  </Button>
                </div>
                <div className="flex items-center gap-2">
                  <Input className="w-full font-mono" value={buildCfgLink(exportText)} readOnly />
                  <Button size="sm" onClick={() => navigator.clipboard.writeText(buildCfgLink(exportText))}>
                    <Copy className="mr-1 h-4 w-4" /> Link
                  </Button>
                </div>
                <div className="w-full rounded-lg border border-border bg-panel p-3">
                  <QRCodeSVG value={buildCfgLink(exportText)} width={200} height={200} className="mx-auto" />
                </div>
              </div>
            )}
          </div>
        </CardContent>
      </Card>
  );
}
