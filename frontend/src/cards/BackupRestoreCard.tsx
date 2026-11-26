import { Link } from '@tanstack/react-router';
import { ClipboardPaste, Copy } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans, useI18n } from '@/i18n';

/**
 * Raw device config backup/restore via text or QR code (`cfg export`/`cfg import`).
 * Unrelated to the three numbered scene slots on the Home tab (see ProfileSlotsCard) -
 * this covers the device's full settings, not a quick-switchable scene. The actual
 * import/export UI (QR scanning, generated links, etc.) lives on its own subpage -
 * this card is just the entry point into each. Same icon+title+description tile
 * style as the Advanced-tab hardware-feature tiles (AdvancedOverview.tsx), rather
 * than two bare buttons with no context.
 */
export function BackupRestoreCard() {
  const { t } = useI18n();
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.import">Backup / Restore</Trans></CardTitle>
      </CardHeader>
      <CardContent>
        <div className="grid grid-cols-2 gap-3">
          <Link
            to="/settings/backup/import"
            className="card flex flex-col items-center gap-1.5 p-4 text-center transition hover:border-accent hover:text-accent"
          >
            <ClipboardPaste className="h-6 w-6" />
            <span className="text-sm font-medium"><Trans k="btn.import">Import</Trans></span>
            <span className="text-xs text-muted">
              {t('desc.backupImport', 'Restore from a text line or QR code')}
            </span>
          </Link>
          <Link
            to="/settings/backup/export"
            className="card flex flex-col items-center gap-1.5 p-4 text-center transition hover:border-accent hover:text-accent"
          >
            <Copy className="h-6 w-6" />
            <span className="text-sm font-medium"><Trans k="btn.exportQr">Export</Trans></span>
            <span className="text-xs text-muted">
              {t('desc.backupExport', 'Save current settings as text or QR code')}
            </span>
          </Link>
        </div>
      </CardContent>
    </Card>
  );
}
