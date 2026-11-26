import type { ReactNode } from 'react';
import { Link } from '@tanstack/react-router';
import { ChevronLeft } from 'lucide-react';
import { Trans } from '@/i18n';

export function SettingsSubpage({ children }: { children: ReactNode }) {
  return (
    <div className="space-y-4">
      <Link to="/settings" className="inline-flex items-center gap-1 text-sm text-muted hover:text-text">
        <ChevronLeft className="h-4 w-4" />
        <Trans k="title.settings">Settings</Trans>
      </Link>
      {children}
    </div>
  );
}
