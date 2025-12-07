import { forwardRef, InputHTMLAttributes, ReactNode } from 'react';
import { cn } from '@/lib/utils';

type InputProps = InputHTMLAttributes<HTMLInputElement> & {
  prefix?: ReactNode;
  suffix?: ReactNode;
  description?: ReactNode;
  label?: string;
  variant?: 'solid' | 'ghost';
};

export const Input = forwardRef<HTMLInputElement, InputProps>(
  ({ className, prefix, suffix, description, label, id, variant = 'solid', ...props }, ref) => {
    const inputId = id || (label ? `${label.replace(/\s+/g, '-')}-${Math.random().toString(36).slice(2, 6)}` : undefined);
    const baseClasses = variant === 'ghost' ? 'bg-transparent border-transparent shadow-none px-0' : 'bg-panel';
    const content = (
      <div className={cn('flex items-center gap-2 rounded-md border border-border px-2', baseClasses, className)}>
        {prefix && <span className="text-muted text-sm">{prefix}</span>}
        <input
          ref={ref}
          id={inputId}
          className={cn('input border-none bg-transparent px-0', variant === 'ghost' ? 'w-auto' : 'w-full')}
          {...props}
        />
        {suffix && <span className="text-muted text-sm whitespace-nowrap">{suffix}</span>}
      </div>
    );

    if (!label && !description) return content;

    return (
      <div className="w-full space-y-1">
        {label && (
          <label htmlFor={inputId} className="text-sm text-muted">
            {label}
          </label>
        )}
        {content}
        {description && <p className="text-xs text-muted">{description}</p>}
      </div>
    );
  },
);
Input.displayName = 'Input';
