import { useState } from 'react';
import { ClipboardPaste, Copy } from 'lucide-react';
import { QRCodeSVG } from 'qrcode.react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function ProfilesCard({ profileSlot, setProfileSlot }: { profileSlot: string; setProfileSlot: (v: string) => void }) {
  const { sendCmd } = useConnection();
  const [cfgText, setCfgText] = useState('');
  const [exportText, setExportText] = useState('');

  return (
    <div className="grid gap-4 md:grid-cols-2">
      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.import">Import / Export</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2">
            <Button onClick={() => sendCmd('cfg export')}>
              <Copy className="mr-1 h-4 w-4" /> cfg export
            </Button>
            <Input placeholder="cfg import key=val ..." value={cfgText} onChange={(e) => setCfgText(e.target.value)} />
            <Button onClick={() => cfgText.trim() && sendCmd(cfgText)}>
              <ClipboardPaste className="mr-1 h-4 w-4" /> Import
            </Button>
          </div>
          <div className="flex flex-wrap items-center gap-3">
            <Button onClick={() => sendCmd('cfg export').then(() => setExportText(cfgText))}>
              <Copy className="mr-1 h-4 w-4" /> Export to text/QR
            </Button>
            {exportText && (
              <>
                <Input className="w-full" value={exportText} onChange={(e) => setExportText(e.target.value)} />
                <QRCodeSVG value={exportText} width={128} height={128} />
              </>
            )}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle><Trans k="title.profiles">Profiles</Trans></CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex gap-2 flex-wrap items-center">
            <Label className="m-0">Slot</Label>
            <select className="input w-20" value={profileSlot} onChange={(e) => setProfileSlot(e.target.value)}>
              <option value="1">1</option>
              <option value="2">2</option>
              <option value="3">3</option>
            </select>
            <Button onClick={() => sendCmd(`profile save ${profileSlot}`)}>Save Profile {profileSlot}</Button>
            <Button onClick={() => sendCmd(`profile load ${profileSlot}`)}>Load Profile {profileSlot}</Button>
            <Button onClick={() => sendCmd('cfg export')}>Backup Settings</Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
