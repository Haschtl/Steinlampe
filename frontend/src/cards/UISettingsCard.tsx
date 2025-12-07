import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { Trans } from '@/i18n';

export function UISettingsCard() {
  const { autoReconnect, setAutoReconnect } = useConnection();

  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.ui">UI Settings</Trans></CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <label className="pill cursor-pointer">
          <input
            type="checkbox"
            className="accent-accent"
            checked={autoReconnect}
            onChange={(e) => setAutoReconnect(e.target.checked)}
          />{' '}
          <Trans k="toggle.autoReconnect">Auto-reconnect</Trans>
        </label>
      </CardContent>
    </Card>
  );
}
