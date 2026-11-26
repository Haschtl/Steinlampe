import { ReactNode, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ChevronDown } from 'lucide-react';
import { cn } from '@/lib/utils';

type CollapsibleProps = {
  title: ReactNode;
  children: ReactNode;
  defaultOpen?: boolean;
  className?: string;
  headerRight?: ReactNode;
};

/** Dezentes, chevron-getriggertes Aufklapp-Panel - für sekundäre Optionen, die nicht direkt sichtbar sein müssen. */
export function Collapsible({ title, children, defaultOpen = false, className, headerRight }: CollapsibleProps) {
  const [open, setOpen] = useState(defaultOpen);
  return (
    <div className={cn('rounded-lg border border-border/60', className)}>
      <button
        type="button"
        onClick={() => setOpen((v) => !v)}
        className="flex w-full items-center justify-between gap-2 px-3 py-2 text-left text-sm text-muted hover:text-text"
        aria-expanded={open}
      >
        <span className="flex items-center gap-2">
          <ChevronDown className={cn('h-4 w-4 transition-transform', open ? 'rotate-0' : '-rotate-90')} />
          {title}
        </span>
        {headerRight}
      </button>
      <AnimatePresence initial={false}>
        {open && (
          <motion.div
            key="content"
            initial={{ height: 0, opacity: 0 }}
            animate={{ height: 'auto', opacity: 1 }}
            exit={{ height: 0, opacity: 0 }}
            transition={{ duration: 0.2, ease: 'easeInOut' }}
            style={{ overflow: 'hidden' }}
          >
            <div className="space-y-3 px-3 pb-3 pt-1">{children}</div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}
