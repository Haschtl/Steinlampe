import type { ReactNode } from 'react';
import { Link } from '@tanstack/react-router';
import { ChevronLeft } from 'lucide-react';
import { Trans } from '@/i18n';

export function HardwareSubpage({ available, children }: { available: boolean; children: ReactNode }) {
  return (
    <div className="space-y-4">
      <Link to="/advanced" className="inline-flex items-center gap-1 text-sm text-muted hover:text-text">
        <ChevronLeft className="h-4 w-4" />
        <Trans k="nav.hardware">Hardware</Trans>
      </Link>
      {available ? (
        children
      ) : (
        <p className="text-sm text-muted">
          <Trans k="label.featureUnavailable">This device doesn't report this feature.</Trans>
        </p>
      )}
    </div>
  );
}
