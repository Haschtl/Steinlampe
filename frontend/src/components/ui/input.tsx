import { forwardRef, InputHTMLAttributes, ReactNode } from 'react';
import { cn } from '@/lib/utils';

type InputProps = InputHTMLAttributes<HTMLInputElement> & {
  prefix?: ReactNode;
  suffix?: ReactNode;
  description?: string;
  label?: string;
};

export const Input = forwardRef<HTMLInputElement, InputProps>(
  ({ className, prefix, suffix, description, label, id, ...props }, ref) => {
    const inputId = id || (label ? `${label.replace(/\s+/g, '-')}-${Math.random().toString(36).slice(2, 6)}` : undefined);
    const content = (
      <div className={cn('flex w-full items-center gap-2 rounded-md border border-border bg-[#0f1525] px-2', className)}>
        {prefix && <span className="text-muted text-sm">{prefix}</span>}
        <input
          ref={ref}
          id={inputId}
          className="input border-none bg-transparent px-0"
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
