import { createContext, ReactNode, useContext, useMemo, useState } from 'react';

type Lang = 'en' | 'de';

const translations: Record<Lang, Record<string, string>> = {
  en: {
    'nav.home': 'Home',
    'nav.settings': 'Settings',
    'nav.actions': 'Actions',
    'nav.help': 'Help',
    'nav.log': 'Log',
    'btn.connect': 'Connect',
    'btn.disconnect': 'Disconnect',
    'btn.connectSerial': 'Connect Serial',
    'toggle.autoReconnect': 'Auto-reconnect',
    'title.app': 'Quarzlampe Control',
    'title.lamp': 'Lamp & Ramps',
    'title.notify': 'Notify',
    'title.wake': 'Wake',
    'title.sleep': 'Sleep',
    'title.settings': 'Settings',
    'title.log': 'Log',
    'title.quick': 'Quick Tap Modes',
    'title.custom': 'Custom Pattern',
    'title.presence': 'Presence & Touch',
    'title.lightmusic': 'Light & Music',
    'title.import': 'Import / Export',
    'title.profiles': 'Profiles',
    'title.help': 'Command Reference & Links',
    'label.profile': 'Profile',
    'label.brightness': 'Brightness',
    'label.speed': 'Speed',
    'label.fade': 'Fade',
    'label.sequence': 'Sequence',
    'label.duration': 'Duration',
    'label.mode': 'Mode',
    'label.briPct': 'Brightness (%)',
    'label.cap': 'Brightness cap (%)',
    'label.idle': 'Idle off (min)',
    'label.pwm': 'PWM Curve',
    'btn.load': 'Load',
    'btn.save': 'Save',
    'btn.sync': 'Sync',
    'btn.on': 'On',
    'btn.off': 'Off',
  },
  de: {
    'nav.home': 'Start',
    'nav.settings': 'Einstellungen',
    'nav.actions': 'Funktionen',
    'nav.help': 'Hilfe',
    'nav.log': 'Log',
    'btn.connect': 'Verbinden',
    'btn.disconnect': 'Trennen',
    'btn.connectSerial': 'Seriell verbinden',
    'toggle.autoReconnect': 'Auto-Reconnect',
    'title.app': 'Quarzlampe Steuerung',
    'title.lamp': 'Lampe & Rampen',
    'title.notify': 'Benachrichtigung',
    'title.wake': 'Wecken',
    'title.sleep': 'Schlafen',
    'title.settings': 'Einstellungen',
    'title.log': 'Protokoll',
    'title.quick': 'Schnell-Modi',
    'title.custom': 'Custom-Muster',
    'title.presence': 'Presence & Touch',
    'title.lightmusic': 'Licht & Musik',
    'title.import': 'Import / Export',
    'title.profiles': 'Profile',
    'title.help': 'Befehle & Links',
    'label.profile': 'Profil',
    'label.brightness': 'Helligkeit',
    'label.speed': 'Geschwindigkeit',
    'label.fade': 'Fade',
    'label.sequence': 'Sequenz',
    'label.duration': 'Dauer',
    'label.mode': 'Modus',
    'label.briPct': 'Helligkeit (%)',
    'label.cap': 'Max. Helligkeit (%)',
    'label.idle': 'Idle aus (Min)',
    'label.pwm': 'PWM Kurve',
    'btn.load': 'Laden',
    'btn.save': 'Speichern',
    'btn.sync': 'Sync',
    'btn.on': 'An',
    'btn.off': 'Aus',
  },
};

const I18nContext = createContext<{ lang: Lang; setLang: (l: Lang) => void; t: (key: string) => string }>({
  lang: 'en',
  setLang: () => undefined,
  t: (k) => k,
});

export function I18nProvider({ children }: { children: ReactNode }) {
  const [lang, setLang] = useState<Lang>('en');
  const t = useMemo(() => (key: string) => translations[lang][key] || translations.en[key] || key, [lang]);
  return <I18nContext.Provider value={{ lang, setLang, t }}>{children}</I18nContext.Provider>;
}

export function useI18n() {
  return useContext(I18nContext);
}

export function Trans({ k, children }: { k: string; children?: ReactNode }) {
  const { t } = useI18n();
  return <>{t(k) || children}</>;
}
