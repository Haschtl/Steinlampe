import { cn } from '@/lib/utils';
import type { PropsWithChildren, HTMLAttributes } from 'react';

export function Card({ className, ...props }: PropsWithChildren<HTMLAttributes<HTMLDivElement>>) {
  return <div className={cn('card p-4', className)} {...props} />;
}

export function CardHeader({ className, ...props }: PropsWithChildren<HTMLAttributes<HTMLDivElement>>) {
  return <div className={cn('mb-3 flex items-center justify-between gap-2', className)} {...props} />;
}

export function CardTitle({ className, ...props }: PropsWithChildren<HTMLAttributes<HTMLHeadingElement>>) {
  return <h3 className={cn('text-lg font-semibold text-accent', className)} {...props} />;
}

export function CardContent({ className, ...props }: PropsWithChildren<HTMLAttributes<HTMLDivElement>>) {
  return <div className={cn('space-y-3', className)} {...props} />;
}
