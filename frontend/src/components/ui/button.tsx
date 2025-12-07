import { cn } from '@/lib/utils';
import type { ButtonHTMLAttributes, PropsWithChildren } from 'react';

type Variant = 'primary' | 'ghost' | 'danger';
type Size = 'sm' | 'md';
const variantClasses: Record<Variant, string> = {
  primary: 'border-accent text-accent hover:border-accent hover:text-accent',
  ghost: 'border-border text-text hover:border-accent hover:text-accent',
  danger: 'border-red-500 text-red-400 hover:border-red-400 hover:text-red-300',
};
const sizeClasses: Record<Size, string> = {
  sm: 'px-2 py-1 text-sm',
  md: '',
};

export function Button({
  variant = 'ghost',
  size = 'md',
  className,
  ...props
}: PropsWithChildren<ButtonHTMLAttributes<HTMLButtonElement>> & { variant?: Variant; size?: Size }) {
  return (
    <button
      className={cn(
        'btn',
        variantClasses[variant],
        sizeClasses[size],
        className,
      )}
      {...props}
    />
  );
}
