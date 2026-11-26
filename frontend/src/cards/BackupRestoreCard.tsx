import { Link } from '@tanstack/react-router';
import { ClipboardPaste, Copy } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Trans } from '@/i18n';

/**
 * Raw device config backup/restore via text or QR code (`cfg export`/`cfg import`).
 * Unrelated to the three numbered scene slots on the Home tab (see ProfileSlotsCard) -
 * this covers the device's full settings, not a quick-switchable scene. The actual
 * import/export UI (QR scanning, generated links, etc.) lives on its own subpage -
 * this card is just the entry point into each.
 */
export function BackupRestoreCard() {
  return (
    <Card>
      <CardHeader>
        <CardTitle><Trans k="title.import">Backup / Restore</Trans></CardTitle>
      </CardHeader>
      <CardContent className="flex flex-wrap gap-2">
        <Link to="/settings/backup/import" className="btn">
          <ClipboardPaste className="mr-1 h-4 w-4" /> <Trans k="btn.import">Import</Trans>
        </Link>
        <Link to="/settings/backup/export" className="btn">
          <Copy className="mr-1 h-4 w-4" /> <Trans k="btn.exportQr">Export to text/QR</Trans>
        </Link>
      </CardContent>
    </Card>
  );
}
