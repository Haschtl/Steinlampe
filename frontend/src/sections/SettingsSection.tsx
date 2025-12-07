import { useState } from 'react';
import { SettingsCard } from '@/cards/SettingsCard';
import { UISettingsCard } from '@/cards/UISettingsCard';
import { QuickCustomCard } from '@/cards/QuickCustomCard';
import { ProfilesCard } from '@/cards/ProfilesCard';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Trans } from '@/i18n';
import { useConnection } from '@/context/connection';

function FactoryResetCard() {
  const { sendCmd } = useConnection();
  const [open, setOpen] = useState(false);

  const handleReset = () => {
    sendCmd('factory').catch((e) => console.warn(e));
    setOpen(false);
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.factory">Factory Defaults</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent className="flex items-center justify-between gap-3">
        <div className="text-sm text-muted max-w-md">
          <Trans k="desc.factory">Reset all settings to factory defaults (profiles, quick list, sensors).</Trans>
        </div>
        <Dialog open={open} onOpenChange={setOpen}>
          <DialogTrigger asChild>
            <Button variant="danger">
              <Trans k="btn.factory">Factory reset</Trans>
            </Button>
          </DialogTrigger>
          <DialogContent>
            <DialogHeader>
              <DialogTitle>
                <Trans k="title.factoryConfirm">Reset all settings?</Trans>
              </DialogTitle>
              <DialogDescription>
                <Trans k="desc.factoryConfirm">This clears stored profiles and preferences. Lamp will restart with defaults.</Trans>
              </DialogDescription>
            </DialogHeader>
            <DialogFooter className="flex justify-end gap-2">
              <Button variant="ghost" onClick={() => setOpen(false)}>
                <Trans k="btn.cancel">Cancel</Trans>
              </Button>
              <Button variant="danger" onClick={handleReset}>
                <Trans k="btn.confirm">Yes, reset</Trans>
              </Button>
            </DialogFooter>
          </DialogContent>
        </Dialog>
      </CardContent>
    </Card>
  );
}

export function SettingsSection() {
  const [profileSlot, setProfileSlot] = useState('1');

  return (
    <div className="space-y-4">
      <UISettingsCard />
      <SettingsCard />
      <QuickCustomCard />
      <ProfilesCard profileSlot={profileSlot} setProfileSlot={setProfileSlot} />
      <FactoryResetCard />
    </div>
  );
}
