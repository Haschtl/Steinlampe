import { cn } from '@/lib/utils';
import type { ButtonHTMLAttributes, PropsWithChildren } from 'react';

type Variant = 'primary' | 'ghost' | 'danger';
const variantClasses: Record<Variant, string> = {
  primary: 'border-accent text-accent hover:border-accent hover:text-accent',
  ghost: 'border-border text-text hover:border-accent hover:text-accent',
  danger: 'border-red-500 text-red-400 hover:border-red-400 hover:text-red-300',
};

export function Button({
  variant = 'ghost',
  className,
  ...props
}: PropsWithChildren<ButtonHTMLAttributes<HTMLButtonElement>> & { variant?: Variant }) {
  return (
    <button
      className={cn(
        'btn',
        variantClasses[variant],
        className,
      )}
      {...props}
    />
  );
}
