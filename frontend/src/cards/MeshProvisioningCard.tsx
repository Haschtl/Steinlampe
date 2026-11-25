import { useState } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { useConnection } from '@/context/connection';
import { Trans, useI18n } from '@/i18n';

// Minimal trigger-only UI for the self-provisioning firmware feature (see
// the feat/ble-mesh plan) - every button here just fires a `mesh ...`
// command and relies on the existing Log tab to show the resulting
// [Mesh] feedback line, same as FactoryResetCard's `sendCmd('factory')`.
// A real device-picker/group-assignment UI is future work once this
// firmware foundation is proven on real hardware.
export function MeshProvisioningCard() {
  const { t } = useI18n();
  const { sendCmd } = useConnection();
  const [netKey, setNetKey] = useState('');
  const [appKey, setAppKey] = useState('');
  const [scanSecs, setScanSecs] = useState(20);
  const [addIndex, setAddIndex] = useState('');
  const [addGroup, setAddGroup] = useState('');
  const [removeAddr, setRemoveAddr] = useState('');

  const run = (cmd: string) => sendCmd(cmd).catch((e) => console.warn(e));

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.meshProv">Mesh Network (self-provisioning)</Trans></CardTitle>
        <p className="text-sm text-muted">
          <Trans k="desc.meshProv">
            Lets this lamp provision other lamps into the mesh itself - no nRF Mesh app needed. Results show up as
            [Mesh] lines in the Log tab.
          </Trans>
        </p>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="label.meshNetwork">Network identity</Trans></div>
          <div className="flex flex-wrap items-end gap-2">
            <Input
              placeholder={t('label.meshNetKey', 'NetKey (32 hex, blank = generate)')}
              className="w-64"
              value={netKey}
              onChange={(e) => setNetKey(e.target.value)}
            />
            <Input
              placeholder={t('label.meshAppKey', 'AppKey (32 hex, blank = generate)')}
              className="w-64"
              value={appKey}
              onChange={(e) => setAppKey(e.target.value)}
            />
            <Button
              onClick={() => run(netKey && appKey ? `mesh network create ${netKey} ${appKey}` : 'mesh network create')}
            >
              <Trans k="btn.meshCreate">Create / Join</Trans>
            </Button>
          </div>
          <p className="text-xs text-muted">
            <Trans k="desc.meshNetworkHint">
              First lamp: leave both blank to generate a fresh network. Every other lamp that should also be able to
              provision: paste the same two hex values the first lamp's own feedback printed.
            </Trans>
          </p>
        </div>

        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="label.meshScan">Scan for new lamps</Trans></div>
          <div className="flex flex-wrap items-end gap-2">
            <Input
              type="number"
              min={5}
              max={120}
              className="w-24"
              value={scanSecs}
              onChange={(e) => setScanSecs(Number(e.target.value))}
              suffix="s"
            />
            <Button onClick={() => run(`mesh provision scan ${scanSecs}`)}><Trans k="btn.meshScan">Scan</Trans></Button>
            <Button variant="ghost" onClick={() => run('mesh provision stop')}><Trans k="btn.meshStop">Stop</Trans></Button>
            <Button variant="ghost" onClick={() => run('mesh provision list')}><Trans k="btn.meshList">List found</Trans></Button>
          </div>
        </div>

        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="label.meshAdd">Provision a found lamp</Trans></div>
          <div className="flex flex-wrap items-end gap-2">
            <Input
              type="number"
              min={1}
              placeholder={t('label.meshIndex', '# from list')}
              className="w-32"
              value={addIndex}
              onChange={(e) => setAddIndex(e.target.value)}
            />
            <Input
              placeholder={t('label.meshGroup', 'Group addr (optional)')}
              className="w-44"
              value={addGroup}
              onChange={(e) => setAddGroup(e.target.value)}
            />
            <Button
              onClick={() => addIndex && run(`mesh provision add ${addIndex}${addGroup ? ` ${addGroup}` : ''}`)}
            >
              <Trans k="btn.meshAdd">Provision</Trans>
            </Button>
          </div>
        </div>

        <div className="space-y-2">
          <div className="text-sm font-medium"><Trans k="label.meshNodes">Known lamps</Trans></div>
          <div className="flex flex-wrap items-end gap-2">
            <Button onClick={() => run('mesh nodes')}><Trans k="btn.meshNodes">List known</Trans></Button>
            <Input
              placeholder={t('label.meshAddr', 'Address (0x...)')}
              className="w-36"
              value={removeAddr}
              onChange={(e) => setRemoveAddr(e.target.value)}
            />
            <Button variant="ghost" onClick={() => removeAddr && run(`mesh provision remove ${removeAddr}`)}>
              <Trans k="btn.meshForget">Forget</Trans>
            </Button>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
