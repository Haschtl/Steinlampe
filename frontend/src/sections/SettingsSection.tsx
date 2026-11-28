import { useState } from 'react';
import { UISettingsCard } from '@/cards/UISettingsCard';
import { MeshProvisioningCard } from '@/cards/MeshProvisioningCard';
import { BackupRestoreCard } from '@/cards/BackupRestoreCard';
import { OtaUpdateCard } from '@/cards/OtaUpdateCard';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle, DialogTrigger } from '@/components/ui/dialog';
import { Trans } from '@/i18n';
import { useConnection } from '@/context/connection';
import { useStatusGroupFetch } from '@/hooks/useStatusGroupFetch';
import { DeviceBTCard } from '@/cards/DeviceBTCard';

function DevOptionsCard() {
  const { devLogVisible, setDevLogVisible } = useConnection();

  return (
    <Card>
      <CardHeader>
        <CardTitle>
          <Trans k="title.devOptions">Dev options</Trans>
        </CardTitle>
      </CardHeader>
      <CardContent>
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={devLogVisible}
            onChange={(e) => setDevLogVisible(e.target.checked)}
          />{' '}
          <Trans k="toggle.devLogOverlay">Show log overlay</Trans>
        </label>
      </CardContent>
    </Card>
  );
}

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
  // MeshProvisioningCard reads mesh_net/mesh_nodes/mesh_role (MESHSTATUS|);
  // OtaUpdateCard reads fw_ver/fw_env (MISC|) - both moved off the general
  // status payload 2026-11-28, see memory: status protocol redesign.
  useStatusGroupFetch('mesh');
  useStatusGroupFetch('misc');
  return (
    <div className="space-y-4">
      <DeviceBTCard />
      <MeshProvisioningCard />
      <UISettingsCard />
      <BackupRestoreCard />
      <OtaUpdateCard />
      <FactoryResetCard />
      <DevOptionsCard />
    </div>
  );
}
