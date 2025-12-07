import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { useI18n, Trans } from '@/i18n';

export function UISettingsCard() {
  const { autoReconnect, setAutoReconnect } = useConnection();
  const { lang, setLang } = useI18n();
  const [theme, setTheme] = useState(() => {
    const prefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
    const fallback = prefersDark ? 'fancy' : 'fancy-light';
    return localStorage.getItem('ql-theme') || fallback;
  });

  useEffect(() => {
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('ql-theme', theme);
  }, [theme]);

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
          <span className="text-sm text-muted"><Trans k="label.language">Language</Trans></span>
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
        <div className="flex items-center gap-2">
          <span className="text-sm text-muted"><Trans k="label.theme">Theme</Trans></span>
          <select className="input w-40" value={theme} onChange={(e) => setTheme(e.target.value)}>
            <option value="dark">Dark</option>
            <option value="light">Light</option>
            <option value="fancy">Fancy Dark</option>
            <option value="fancy-light">Fancy Light</option>
          </select>
        </div>
      </CardContent>
    </Card>
  );
}
