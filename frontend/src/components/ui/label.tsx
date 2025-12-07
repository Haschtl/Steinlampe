import { LabelHTMLAttributes, PropsWithChildren } from 'react';
import { cn } from '@/lib/utils';

export function Label({ className, ...props }: PropsWithChildren<LabelHTMLAttributes<HTMLLabelElement>>) {
  return <label className={cn('label', className)} {...props} />;
}
