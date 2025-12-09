declare module 'react-rotary-knob' {
  import { CSSProperties, ComponentType } from 'react';
  export interface KnobProps {
    min?: number;
    max?: number;
    step?: number;
    value: number;
    onChange: (v: number) => void;
    unlockDistance?: number;
    preciseMode?: boolean;
    skin?: unknown;
    className?: string;
    style?: CSSProperties;
  }
  export const Knob: ComponentType<KnobProps>;
  const DefaultKnob: ComponentType<KnobProps>;
  export default DefaultKnob;
}
