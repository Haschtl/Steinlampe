import { createContext, ReactNode, useContext, useEffect, useMemo, useState } from 'react';

type Lang = 'en' | 'de';

const translations: Record<Lang, Record<string, string>> = {
  en: {
    "nav.home": "Home",
    "nav.settings": "Settings",
    "nav.actions": "Actions",
    "nav.help": "Help",
    "nav.log": "Log",
    "btn.connect": "Connect",
    "btn.disconnect": "Disconnect",
    "btn.connectSerial": "Connect Serial",
    "btn.send": "Send",
    "toggle.autoReconnect": "Auto-reconnect",
    "title.app": "Quarzlampe Control",
    "title.lamp": "Lamp",
    "title.ramps": "Ramps",
    "title.notify": "Notify",
    "title.wake": "Wake",
    "title.sleep": "Sleep",
    "title.settings": "Settings",
    "title.log": "Log",
    "title.quick": "Quick Tap Modes",
    "title.custom": "Custom Pattern",
    "title.presence": "Presence",
    "title.touch": "Touch",
    "title.light": "Light",
    "title.music": "Music",
    "title.modes": "Modes",
    "title.ui": "UI Settings",
    "title.import": "Import / Export",
    "title.profiles": "Profiles",
    "title.help": "Command Reference & Links",
    "label.profile": "Profile",
    "label.brightness": "Brightness",
    "label.speed": "Speed",
    "label.fade": "Fade",
    "label.fadeMul": "Pattern Fade",
    "label.sequence": "Sequence",
    "label.duration": "Duration",
    "label.mode": "Mode",
    "label.briPct": "Brightness (%)",
    "label.cap": "Brightness cap (%)",
    "label.idle": "Idle off (min)",
    "label.pwm": "PWM Curve",
    "label.rampOn": "Ramp On",
    "label.rampOff": "Ramp Off",
    "label.values": "Values (0..1 CSV)",
    "label.step": "Step",
    "log.show": "Show",
    "log.hide": "Hide",
    "status.connected": "Connected",
    "status.disconnected": "Not connected",
    "status.last": "Last status",
    "btn.load": "Load",
    "btn.save": "Save",
    "btn.sync": "Sync",
    "btn.on": "On",
    "btn.off": "Off",
    "btn.apply": "Apply",
    "btn.clear": "Clear",
    "btn.reload": "Reload",
    "btn.prev": "Prev",
    "btn.next": "Next",
  },
  de: {
    "nav.home": "Start",
    "nav.settings": "Einstellungen",
    "nav.actions": "Funktionen",
    "nav.help": "Hilfe",
    "nav.log": "Log",
    "btn.connect": "Verbinden",
    "btn.disconnect": "Trennen",
    "btn.connectSerial": "Seriell verbinden",
    "btn.send": "Senden",
    "toggle.autoReconnect": "Auto-Reconnect",
    "title.app": "Quarzlampe Steuerung",
    "title.lamp": "Lampe",
    "title.ramps": "Rampen",
    "title.notify": "Benachrichtigung",
    "title.wake": "Wecken",
    "title.sleep": "Schlafen",
    "title.settings": "Einstellungen",
    "title.log": "Protokoll",
    "title.quick": "Schnell-Modi",
    "title.custom": "Custom-Muster",
    "title.presence": "Anwesenheit",
    "title.touch": "Touch",
    "title.light": "Licht",
    "title.music": "Musik",
    "title.modes": "Modi",
    "title.ui": "UI-Einstellungen",
    "title.import": "Import / Export",
    "title.profiles": "Profile",
    "title.help": "Befehle & Links",
    "label.profile": "Profil",
    "label.brightness": "Helligkeit",
    "label.speed": "Geschwindigkeit",
    "label.fade": "Fade",
    "label.fadeMul": "Pattern Fade",
    "label.sequence": "Sequenz",
    "label.duration": "Dauer",
    "label.mode": "Modus",
    "label.briPct": "Helligkeit (%)",
    "label.cap": "Max. Helligkeit (%)",
    "label.idle": "Idle aus (Min)",
    "label.pwm": "PWM Kurve",
    "label.rampOn": "Ramp On",
    "label.rampOff": "Ramp Off",
    "label.values": "Werte (0..1 CSV)",
    "label.step": "Schritt",
    "log.show": "Anzeigen",
    "log.hide": "Verbergen",
    "status.connected": "Verbunden",
    "status.disconnected": "Nicht verbunden",
    "status.last": "Letzter Status",
    "btn.load": "Laden",
    "btn.save": "Speichern",
    "btn.sync": "Sync",
    "btn.on": "An",
    "btn.off": "Aus",
    "btn.apply": "Anwenden",
    "btn.clear": "Leeren",
    "btn.reload": "Neu laden",
    "btn.prev": "Zurück",
    "btn.next": "Weiter",
  },
};

const I18nContext = createContext<{ lang: Lang; setLang: (l: Lang) => void; t: (key: string, fallback?: string) => string }>({
  lang: 'en',
  setLang: () => undefined,
  t: (k, fb) => fb ?? k,
});

export function I18nProvider({ children }: { children: ReactNode }) {
  const [lang, setLang] = useState<Lang>('en');

  useEffect(() => {
    if (typeof navigator === 'undefined') return;
    const preferred = navigator.language || navigator.languages?.[0];
    if (preferred?.toLowerCase().startsWith('de')) setLang('de');
  }, []);

  const t = useMemo(
    () => (key: string, fallback?: string) => translations[lang][key] || translations.en[key] || fallback || key,
    [lang],
  );
  return <I18nContext.Provider value={{ lang, setLang, t }}>{children}</I18nContext.Provider>;
}

export function useI18n() {
  return useContext(I18nContext);
}

export function Trans({ k, children }: { k: string; children?: ReactNode }) {
  const { t } = useI18n();
  return <>{t(k) || children}</>;
}
