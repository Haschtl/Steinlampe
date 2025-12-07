import { useState } from 'react';
import { Download, Save } from 'lucide-react';
import { LampPowerCard } from '@/cards/LampPowerCard';
import { ModesCard } from '@/cards/ModesCard';
import { RampCard } from '@/cards/RampCard';
import { Button } from '@/components/ui/button';
import { Label } from '@/components/ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Trans, useI18n } from '@/i18n';
import { useConnection } from '@/context/connection';

export function HomeSection() {
  const [profileSlot, setProfileSlot] = useState('1');
  const { sendCmd } = useConnection();
  const { t } = useI18n();

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center gap-2 rounded-lg border border-border bg-panel/10 backdrop-blur-xl px-3 py-2">
        <Label className="m-0">
          <Trans k="label.profile">Profile</Trans>
        </Label>
        <div className="flex items-center gap-1">
          <Select value={profileSlot} onValueChange={(v) => setProfileSlot(v)}>
            <SelectTrigger variant="ghost" className="w-24">
              <SelectValue placeholder="Profile" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="1">1</SelectItem>
              <SelectItem value="2">2</SelectItem>
              <SelectItem value="3">3</SelectItem>
            </SelectContent>
          </Select>
          <Button
            size="sm"
            variant="ghost"
            title={t("btn.load", "Load")}
            aria-label={t("btn.load", "Load")}
            onClick={() => sendCmd(`profile load ${profileSlot}`)}
          >
            <Download className="h-4 w-4" />
          </Button>
          <Button
            size="sm"
            variant="ghost"
            title={t("btn.save", "Save")}
            aria-label={t("btn.save", "Save")}
            onClick={() => sendCmd(`profile save ${profileSlot}`)}
          >
            <Save className="h-4 w-4" />
          </Button>
        </div>
      </div>
      <LampPowerCard />
      <ModesCard />
      <RampCard />
    </div>
  );
}
