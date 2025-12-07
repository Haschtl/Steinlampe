import { InputHTMLAttributes, ReactNode } from 'react';
import { Input } from './input';
import { Label } from './label';

type SliderRowProps = {
  label?: ReactNode;
  description?: ReactNode;
  valueLabel?: ReactNode;
  inputProps?: InputHTMLAttributes<HTMLInputElement>;
  onInputChange?: (val: number) => void;
};

export function SliderRow({ label, description, valueLabel, inputProps, onInputChange }: SliderRowProps) {
  return (
    <div className="space-y-1">
      {(label || description || valueLabel) && (
        <div className="flex items-center justify-between gap-2">
          <div>
            {label && <p className="text-sm font-semibold text-text"><Label className="m-0">{label}</Label></p>}
            {description && <p className="text-xs text-muted">{description}</p>}
          </div>
          {inputProps && (
            <Input
              type="number"
              className="w-20"
              variant="ghost"
              {...inputProps}
              onChange={(e) => {
                inputProps.onChange?.(e);
                if (onInputChange) onInputChange(Number(e.target.value));
              }}
            />
          )}
          {valueLabel && !inputProps && <span className="chip-muted">{valueLabel}</span>}
        </div>
      )}
      <input
        type="range"
        className="accent-accent w-full disabled:opacity-60 disabled:cursor-not-allowed"
        {...inputProps}
        onChange={(e) => {
          inputProps?.onChange?.(e);
          if (onInputChange) onInputChange(Number(e.target.value));
        }}
      />
    </div>
  );
}
