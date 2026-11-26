import { useState } from 'react';
import { Send, StopCircle } from 'lucide-react';
import { ActiveToggleButton } from '@/components/ui/active-toggle-button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

const EXAMPLES = ['SOS', 'HELLO', 'QUARZ'];

export function MorseCard() {
  const { status, sendCmd } = useConnection();
  const [text, setText] = useState('SOS');
  const active = status.notifyActive ?? false;

  const handleSend = () => {
    if (!text.trim()) return;
    sendCmd(`morse ${text.trim()}`).catch((e) => console.warn(e));
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.morse">Morse</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="space-y-1">
          <Label>
            <Trans k="label.morseText">Text</Trans>
          </Label>
          <div className="flex items-center gap-2">
            <Input value={text} onChange={(e) => setText(e.target.value)} placeholder="SOS" className="flex-1" disabled={active} />
            <Select value="" onValueChange={(v) => setText(v)} disabled={active}>
              <SelectTrigger className="w-28">
                <SelectValue placeholder={<Trans k="label.examples">Beispiele</Trans>} />
              </SelectTrigger>
              <SelectContent>
                {EXAMPLES.map((p) => (
                  <SelectItem key={p} value={p}>
                    {p}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
        </div>
        <ActiveToggleButton
          active={active}
          onStart={handleSend}
          onStop={() => sendCmd('notify stop').catch((e) => console.warn(e))}
          startIcon={Send}
          stopIcon={StopCircle}
          startLabel={<Trans k="btn.send">Send</Trans>}
          stopLabel={<Trans k="btn.cancel">Abbrechen</Trans>}
        />
      </CardContent>
    </Card>
  );
}
