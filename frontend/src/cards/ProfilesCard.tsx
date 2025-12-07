import { useState } from 'react';
import { ClipboardPaste, Copy } from 'lucide-react';
import { QRCodeSVG } from 'qrcode.react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function ProfilesCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { sendCmd, log } = useConnection();
  const [cfgText, setCfgText] = useState('');
  const [exportText, setExportText] = useState('');

  const grabLatestCfg = () => {
    const line = [...log].reverse().find((l) => l.line.includes('cfg import'));
    if (line) setExportText(line.line.trim());
  };

  const runCfgExport = () => {
    sendCmd('cfg export')
      .then(() => setTimeout(grabLatestCfg, 200))
      .catch((e) => console.warn(e));
  };

  return (
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.import">Import / Export</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2">
            <Input placeholder="cfg import key=val ..." value={cfgText} onChange={(e) => setCfgText(e.target.value)} />
            <Button onClick={() => cfgText.trim() && sendCmd(cfgText)}>
              <ClipboardPaste className="mr-1 h-4 w-4" /> <Trans k="btn.import">Import</Trans>
            </Button>
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
                <div className="w-full rounded-lg border border-border bg-panel p-3">
                  <QRCodeSVG value={exportText} width={200} height={200} className="mx-auto" />
                </div>
              </div>
            )}
          </div>
        </CardContent>
      </Card>
  );
}
