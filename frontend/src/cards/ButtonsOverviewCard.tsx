import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { ButtonInfo, ButtonRoleId } from '@/hooks/status';
import { Trans, useI18n } from '@/i18n';

const roleKey: Record<ButtonRoleId, string> = {
  power: 'label.buttonRole.power',
  modenext: 'label.buttonRole.modenext',
  briup: 'label.buttonRole.briup',
  bridown: 'label.buttonRole.bridown',
};

function stateLabel(btn: ButtonInfo): string {
  if (btn.type === 'toggle') return btn.state === 'on' ? 'ON' : 'OFF';
  return btn.state === 'pressed' ? 'pressed' : 'released';
}

/**
 * Read-only overview of the physical buttons wired on this board: how many,
 * what type (toggle/momentary) and what role each one has been given at
 * build time (see BUTTONn_PIN/_TYPE/_ROLE in platformio.ini). Nothing here is
 * editable - button wiring is a flash-time decision, not a runtime setting.
 */
export function ButtonsOverviewCard() {
  const { status } = useConnection();
  const { t } = useI18n();
  const buttons: ButtonInfo[] = status.buttons ?? [];

  return (
    <Card>
      <CardHeader className="flex items-center justify-between">
        <CardTitle>
          <Trans k="title.buttonsOverview">Configured Buttons</Trans>
        </CardTitle>
        <span className="pill">{buttons.length}</span>
      </CardHeader>
      <CardContent className="space-y-2">
        <div className="text-xs text-muted-foreground">
          <Trans k="desc.buttonsOverview">Physical buttons wired on this board and what they do (set at build time)</Trans>
        </div>
        {buttons.length === 0 ? (
          <div className="text-sm text-muted-foreground">
            <Trans k="label.noButtons">No buttons configured on this board</Trans>
          </div>
        ) : (
          <div className="space-y-1">
            {buttons.map((btn, i) => (
              <div
                key={`${btn.label}-${i}`}
                className="flex items-center justify-between rounded-md border border-border bg-panel px-3 py-2 text-sm"
              >
                <div className="flex items-center gap-2">
                  <span className="font-medium">{btn.label}</span>
                  <span className="pill text-xs">
                    {t(btn.type === 'toggle' ? 'label.buttonType.toggle' : 'label.buttonType.momentary')}
                  </span>
                  <span className="pill text-xs">{t(roleKey[btn.role])}</span>
                </div>
                <span className="text-xs text-muted-foreground">{stateLabel(btn)}</span>
              </div>
            ))}
          </div>
        )}
      </CardContent>
    </Card>
  );
}
