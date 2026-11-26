import { useState } from 'react';
import { Send, StopCircle } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Trans } from '@/i18n';

type Props = {
  sendCmd: (cmd: string) => Promise<void>;
};

export function MorseCard({ sendCmd }: Props) {
  const [text, setText] = useState('SOS');

  const handleSend = () => {
    if (!text.trim()) return;
    sendCmd(`morse ${text.trim()}`).catch((e) => console.warn(e));
  };

  const quickSend = (value: string) => {
    setText(value);
    sendCmd(`morse ${value}`).catch((e) => console.warn(e));
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
            <Input value={text} onChange={(e) => setText(e.target.value)} placeholder="SOS" className="flex-1" />
            {['SOS', 'HELLO', 'QUARZ'].map((p) => (
              <Button key={p} size="sm" variant="ghost" onClick={() => quickSend(p)}>
                {p}
              </Button>
            ))}
          </div>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="primary" onClick={handleSend}>
            <Send className="mr-1 h-4 w-4" /> <Trans k="btn.send">Send</Trans>
          </Button>
          <Button variant="ghost" onClick={() => sendCmd('notify stop').catch((e) => console.warn(e))} title="Stop">
            <StopCircle className="h-4 w-4" />
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}
