import { useState } from 'react';
import { Send, StopCircle } from 'lucide-react';
import { ActiveToggleButton } from '@/components/ui/active-toggle-button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger } from '@/components/ui/select';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

const EXAMPLES = ['SOS', 'HELLO', 'QUARZ'];

export function MorseCard() {
  const { status, sendCmdReliable, refreshStatus } = useConnection();
  const [text, setText] = useState('SOS');
  const [pending, setPending] = useState(false);
  const active = status.notifyActive ?? false;

  // write-with-response + explicit refresh - see the identical comment in
  // WakeSleepCard.tsx (memory: firmware-status-fields-added-2026-11).
  const runAndRefresh = (cmd: string) => {
    setPending(true);
    sendCmdReliable(cmd)
      .then(() => refreshStatus())
      .catch((e) => console.warn(e))
      .finally(() => setPending(false));
  };

  const handleSend = () => {
    if (!text.trim()) return;
    runAndRefresh(`morse ${text.trim()}`);
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
          <Input
            value={text}
            onChange={(e) => setText(e.target.value)}
            placeholder="SOS"
            disabled={active}
            suffix={
              <Select value="" onValueChange={(v) => setText(v)} disabled={active}>
                <SelectTrigger
                  className="h-auto w-auto gap-0 border-none bg-transparent p-0 shadow-none hover:bg-transparent focus:outline-none focus:ring-0 focus:ring-offset-0"
                  aria-label="Beispiele"
                >
                  <span className="sr-only">
                    <Trans k="label.examples">Beispiele</Trans>
                  </span>
                </SelectTrigger>
                <SelectContent align="end">
                  {EXAMPLES.map((p) => (
                    <SelectItem key={p} value={p}>
                      {p}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            }
          />
        </div>
        <ActiveToggleButton
          active={active}
          pending={pending}
          onStart={handleSend}
          onStop={() => runAndRefresh('notify stop')}
          startIcon={Send}
          stopIcon={StopCircle}
          startLabel={<Trans k="btn.send">Send</Trans>}
          stopLabel={<Trans k="btn.cancel">Abbrechen</Trans>}
        />
      </CardContent>
    </Card>
  );
}
