// Mirrors the firmware's ease functions (src/lamp_state.cpp applyEase) for the
// ramp-mode previews. Randomized eases (flicker/fluorescent/loosecontact) are
// illustrative only - the device seeds pulse/cut timing randomly per
// activation, so a live run never looks exactly like this preview.
export type EaseType =
  | 'linear'
  | 'ease'
  | 'ease-in'
  | 'ease-out'
  | 'ease-in-out'
  | 'flash'
  | 'wave'
  | 'blink'
  | 'sine'
  | 'circ'
  | 'expo'
  | 'back'
  | 'elastic'
  | 'bounce'
  | 'stepped'
  | 'flicker'
  | 'fluorescent'
  | 'incandescent'
  | 'capacitor'
  | 'crt'
  | 'loosecontact'
  | 'firefly'
  | 'brownout'
  | 'strobe';

export const EASE_OPTIONS: EaseType[] = [
  'linear',
  'ease',
  'ease-in',
  'ease-out',
  'ease-in-out',
  'flash',
  'wave',
  'blink',
  'sine',
  'circ',
  'expo',
  'back',
  'elastic',
  'bounce',
  'stepped',
  'flicker',
  'fluorescent',
  'incandescent',
  'capacitor',
  'crt',
  'loosecontact',
  'firefly',
  'brownout',
  'strobe',
];

export const easeSample = (t: number, ease: EaseType, pow: number) => {
  switch (ease) {
    case 'ease-in':
      return Math.pow(t, pow);
    case 'ease-out':
      return 1 - Math.pow(1 - t, pow);
    case 'ease-in-out':
      return t < 0.5 ? 0.5 * Math.pow(2 * t, pow) : 1 - 0.5 * Math.pow(2 * (1 - t), pow);
    case 'ease':
      // simple smoothstep-ish
      return t * t * (3 - 2 * t);
    case 'flash':
      return Math.min(1, Math.pow(t, pow > 0 ? 1 / pow : 1));
    case 'wave': {
      if (t < 0.45) {
        const u = t / 0.45;
        const s = u * u * (3 - 2 * u);
        return s;
      }
      if (t < 0.75) {
        const u = (t - 0.45) / 0.3;
        const s = u * u * (3 - 2 * u);
        return 1 - 0.5 * s;
      }
      const u = (t - 0.75) / 0.25;
      const s = u * u * (3 - 2 * u);
      return 0.5 + 0.5 * s;
    }
    case 'blink': {
      const smooth = (u: number) => {
        const s = Math.max(0, Math.min(1, u));
        return s * s * (3 - 2 * s);
      };
      if (t < 0.1) return smooth(t / 0.1);
      if (t < 0.2) return 1 - smooth((t - 0.1) / 0.1);
      if (t < 0.3) return smooth((t - 0.2) / 0.1);
      if (t < 0.4) return 1 - smooth((t - 0.3) / 0.1);
      const u = (t - 0.4) / 0.6;
      const p = pow > 0.1 ? pow : 2;
      return Math.pow(Math.max(0, Math.min(1, u)), 1 / p);
    }
    case 'sine':
      return 0.5 - 0.5 * Math.cos(Math.PI * t);
    case 'circ': {
      if (t < 0.5) {
        const x = 2 * t;
        return 0.5 * (1 - Math.sqrt(1 - x * x));
      }
      const x = 2 * t - 2;
      return 0.5 * (Math.sqrt(1 - x * x) + 1);
    }
    case 'expo': {
      if (t <= 0) return 0;
      if (t >= 1) return 1;
      const k = pow > 0.1 ? pow * 2 : 20;
      if (t < 0.5) return 0.5 * Math.pow(2, k * t - k * 0.5);
      return 1 - 0.5 * Math.pow(2, -k * t + k * 0.5);
    }
    case 'back': {
      const c1 = pow > 0.1 ? pow : 1.70158;
      const c2 = c1 * 1.525;
      if (t < 0.5) {
        const x = 2 * t;
        return (x * x * ((c2 + 1) * x - c2)) * 0.5;
      }
      const x = 2 * t - 2;
      return (x * x * ((c2 + 1) * x + c2) + 2) * 0.5;
    }
    case 'elastic': {
      const n = pow > 0.1 ? pow : 3;
      const amp = 0.16;
      return t + amp * Math.sin(2 * Math.PI * n * t) * (1 - t);
    }
    case 'bounce': {
      const n1 = 7.5625;
      const d1 = 2.75;
      let x = t;
      if (x < 1 / d1) return n1 * x * x;
      if (x < 2 / d1) {
        x -= 1.5 / d1;
        return n1 * x * x + 0.75;
      }
      if (x < 2.5 / d1) {
        x -= 2.25 / d1;
        return n1 * x * x + 0.9375;
      }
      x -= 2.625 / d1;
      return n1 * x * x + 0.984375;
    }
    case 'stepped': {
      const n = Math.max(2, pow > 1.5 ? Math.floor(pow) : 8);
      let idx = Math.floor(t * n);
      if (idx >= n) idx = n - 1;
      return idx / (n - 1);
    }
    case 'flicker': {
      const amp = pow > 0.1 ? Math.min(1, pow / 10) : 0.25;
      const n1 = Math.sin(t * 71.7 + 1.3) * 0.5;
      const n2 = Math.sin(t * 133.1 + 4.7) * 0.5;
      const flicker = (n1 * 0.7 + n2 * 0.3) * amp * (1 - t);
      return t + flicker * t;
    }
    case 'fluorescent': {
      const pulses = pow > 1.5 ? Math.max(2, Math.min(8, Math.floor(pow))) : 4;
      const catchAt = 0.65;
      if (t >= catchAt) {
        const u = Math.max(0, Math.min(1, (t - catchAt) / (1 - catchAt)));
        return u * u * (3 - 2 * u);
      }
      const u = t / catchAt;
      let idx = Math.floor(u * pulses);
      if (idx >= pulses) idx = pulses - 1;
      const segPos = u * pulses - idx;
      const pulseHeight = 0.3 + 0.4 * (0.5 + 0.5 * Math.sin(idx * 2.7 + 1.1));
      const pulseWidth = 0.3 + 0.15 * (0.5 + 0.5 * Math.sin(idx * 5.3 + 2.9));
      if (segPos >= pulseWidth) return 0;
      const p = segPos / pulseWidth;
      const spike = p < 0.5 ? 2 * p : 2 - 2 * p;
      return pulseHeight * spike;
    }
    case 'incandescent': {
      const k = pow > 0.1 ? pow : 3;
      const shoulder = 0.18;
      const knee = 0.35;
      if (t < knee) {
        const u = t / knee;
        return shoulder * (u * u * (3 - 2 * u));
      }
      const u = (t - knee) / (1 - knee);
      const eased = 1 - Math.pow(1 - u, k);
      return shoulder + (1 - shoulder) * eased;
    }
    case 'capacitor': {
      const k = pow > 0.1 ? pow : 4;
      const raw = 1 - Math.exp(-k * t);
      const norm = 1 - Math.exp(-k);
      return norm > 0.0001 ? raw / norm : t;
    }
    case 'crt': {
      const overshoot = pow > 0.1 ? pow : 1.4;
      const popWidth = 0.12;
      if (t < popWidth) {
        const u = t / popWidth;
        const s = u * u * (3 - 2 * u);
        return s * overshoot;
      }
      const u = (t - popWidth) / (1 - popWidth);
      const s = u * u * (3 - 2 * u);
      return overshoot + (1 - overshoot) * s;
    }
    case 'loosecontact': {
      const cuts = pow > 1.5 ? Math.max(2, Math.min(10, Math.floor(pow))) : 6;
      const catchAt = 0.65;
      if (t >= catchAt) {
        const u = Math.max(0, Math.min(1, (t - catchAt) / (1 - catchAt)));
        return u * u * (3 - 2 * u);
      }
      const u = t / catchAt;
      let idx = Math.floor(u * cuts);
      if (idx >= cuts) idx = cuts - 1;
      const segPos = u * cuts - idx;
      const progress = cuts > 1 ? idx / (cuts - 1) : 1;
      const onFrac = 0.12 + 0.3 * progress;
      return Math.abs(segPos - 0.5) < onFrac * 0.5 ? 1 : 0;
    }
    case 'firefly': {
      const cycles = pow > 0.1 ? pow : 3;
      const mean = t;
      const amp = 0.35 * Math.sin(Math.PI * t);
      const phase = Math.sin(2 * Math.PI * cycles * t);
      return mean + amp * phase;
    }
    case 'brownout': {
      let base = t * t * (3 - 2 * t);
      const sagAt = 0.72;
      const sagWidth = 0.14;
      const sagDepth = pow > 0.1 ? Math.min(1, pow / 10) : 0.22;
      const u = (t - sagAt) / sagWidth;
      if (u > -1 && u < 1) {
        const bump = 1 - u * u;
        base -= sagDepth * bump * bump;
      }
      return base;
    }
    case 'strobe': {
      const strobes = pow > 1.5 ? Math.max(1, Math.min(6, Math.floor(pow))) : 3;
      const catchAt = 0.6;
      if (t >= catchAt) {
        const u = Math.max(0, Math.min(1, (t - catchAt) / (1 - catchAt)));
        return u * u * (3 - 2 * u);
      }
      const u = t / catchAt;
      const seg = 1 / strobes;
      const idxF = u / seg;
      const segPos = idxF - Math.floor(idxF);
      const onFrac = 0.35;
      return Math.abs(segPos - 0.5) < onFrac * 0.5 ? 1 : 0;
    }
    default:
      return t;
  }
};

export type RampPathSize = { w: number; h: number; pad: number };
export const DEFAULT_RAMP_PATH_SIZE: RampPathSize = { w: 240, h: 140, pad: 20 };

export const buildRampPaths = (
  duration: number,
  ease: EaseType,
  pow: number,
  reverse?: boolean,
  size: RampPathSize = DEFAULT_RAMP_PATH_SIZE,
  samples = 120,
) => {
  const dur = Math.max(50, Math.min(10000, duration || 400));
  const { w, h, pad } = size;
  const usableW = w - pad * 2;
  const usableH = h - pad * 2;
  const pts = Array.from({ length: samples }, (_, i) => {
    const t = i / (samples - 1);
    const easedBase = easeSample(t, ease, pow);
    const eased = reverse ? 1 - easedBase : easedBase;
    const x = pad + t * usableW;
    const y = h - pad - eased * usableH;
    return { x, y };
  });
  const path = `M ${pts.map((p) => `${p.x.toFixed(2)} ${p.y.toFixed(2)}`).join(' L ')}`;
  const area = `${path} L ${pad + usableW} ${h - pad} L ${pad} ${h - pad} Z`;
  return { path, area, dur, w, h, pad, usableW, usableH };
};
