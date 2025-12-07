import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { useI18n, Trans } from '@/i18n';

export function UISettingsCard() {
  const { autoReconnect, setAutoReconnect } = useConnection();
  const { lang, setLang } = useI18n();

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
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted">Language</span>
          <button
            type="button"
            className="pill"
            onClick={() => setLang(lang === 'en' ? 'de' : 'en')}
            aria-label={lang === 'en' ? 'Switch to German' : 'Switch to English'}
            title={lang === 'en' ? 'Switch to German' : 'Switch to English'}
          >
            <span className="text-lg" role="img" aria-hidden>
              {lang === 'en' ? '🇬🇧' : '🇩🇪'}
            </span>
          </button>
        </div>
      </CardContent>
    </Card>
  );
}
