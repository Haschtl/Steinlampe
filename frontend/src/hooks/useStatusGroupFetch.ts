import { useEffect } from 'react';
import { useConnection } from '@/context/connection';

/**
 * Fetches one on-demand status group when the component mounts, instead of
 * that group's fields riding along on every general status broadcast - see
 * memory: status protocol redesign. Mirrors the lazy-fetch pattern already
 * used by CustomPatternCard.tsx. Shared across sections/pages rather than
 * living in one of them, since several otherwise-unrelated pages each need
 * their own single on-demand group.
 */
export function useStatusGroupFetch(group: string) {
  const { status, sendCmd } = useConnection();
  useEffect(() => {
    if (status.connected && status.statusReady) sendCmd(`status ${group}`).catch((e) => console.warn(e));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status.connected, status.statusReady]);
}
