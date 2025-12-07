import { useEffect, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { useConnection } from '@/context/connection';
import { useI18n, Trans } from '@/i18n';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';

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
          <Select value={theme} onValueChange={(v) => setTheme(v)}>
            <SelectTrigger className="w-40">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="dark">Dark</SelectItem>
              <SelectItem value="light">Light</SelectItem>
              <SelectItem value="fancy">Fancy Dark</SelectItem>
              <SelectItem value="fancy-light">Fancy Light</SelectItem>
            </SelectContent>
          </Select>
        </div>
      </CardContent>
    </Card>
  );
}
