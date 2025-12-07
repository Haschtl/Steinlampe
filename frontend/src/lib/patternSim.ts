// Lightweight TypeScript clones of the firmware patterns for UI previews/animations.
// Each function returns a normalized brightness 0..1 for a given elapsed ms.

const TWO_PI = Math.PI * 2;

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

function hash11(x: number): number {
  let n = x >>> 0;
  n ^= (n * 0x27d4eb2d) >>> 0;
  n ^= n >>> 15;
  n = Math.imul(n, 0x85ebca6b) >>> 0;
  n ^= n >>> 13;
  return (n & 0x00ffffff) / 16777215;
}

function smoothNoise(ms: number, stepMs: number, salt: number): number {
  const step = stepMs || 50;
  const aIdx = Math.floor(ms / step);
  const bIdx = aIdx + 1;
  let t = (ms % step) / step;
  const fa = hash11(aIdx ^ salt);
  const fb = hash11(bIdx ^ salt);
  t = t * t * (3 - 2 * t);
  return fa + (fb - fa) * t;
}

// -- Patterns --
const patternConstant = () => 1;

const patternBreathing = (ms: number) => {
  const phase = (ms % 7000) / 7000;
  const wave = (1 - Math.cos(TWO_PI * phase)) * 0.5;
  const eased = wave * wave * (3 - 2 * wave);
  return clamp01(0.25 + 0.7 * eased);
};

const patternBreathingWarm = (ms: number) => {
  const base = 0.28;
  const peak = 0.92;
  const risePortion = 0.6;
  const period = 8200;
  const phase = (ms % period) / period;
  let t = 0;
  if (phase < risePortion) {
    t = phase / risePortion;
    t = t * t * (3 - 2 * t);
  } else {
    t = 1 - ((phase - risePortion) / (1 - risePortion));
    t = 1 - t * t;
  }
  return clamp01(base + (peak - base) * t);
};

const patternBreathing2 = (ms: number) => {
  const base = 0.18;
  const peak = 0.92;
  const period = 7200;
  const phase = (ms % period) / period;
  let t: number;
  if (phase < 0.62) {
    t = phase / 0.62;
    t = t * t * (3 - 2 * t);
  } else {
    t = 1 - ((phase - 0.62) / 0.38);
    t = t * t * t;
  }
  const drift = (smoothNoise(ms, 1800, 0x19) - 0.5) * 0.06;
  const shimmer = (smoothNoise(ms, 120, 0x29) - 0.5) * 0.03;
  return clamp01(base + (peak - base) * t + drift + shimmer);
};

const patternSinus = (ms: number) => {
  const phase = (ms % 6500) / 6500;
  const wave = 0.5 + 0.5 * Math.sin(TWO_PI * phase);
  return clamp01(0.18 + 0.78 * wave);
};

const patternZigZag = (ms: number) => {
  const period = 5200;
  const phase = (ms % period) / period;
  const tri = phase < 0.5 ? phase * 2 : 1 - (phase - 0.5) * 2;
  const sparkle = 0.05 * Math.sin(TWO_PI * phase * 5);
  return clamp01(0.18 + 0.8 * tri + sparkle);
};

const patternSawtooth = (ms: number) => {
  const period = 4200;
  const phase = (ms % period) / period;
  return clamp01(0.1 + 0.9 * phase);
};

const patternPulse = (ms: number) => {
  const phase = (ms % 4200) / 4200;
  const wave = Math.sin(TWO_PI * phase);
  const env = Math.pow(Math.abs(wave), 1.6);
  return clamp01(0.25 + 0.7 * env);
};

const patternHeartbeat = (ms: number) => {
  const period = 1900;
  const t = ms % period;
  let level = 0.16 + (smoothNoise(ms, 850, 0x2a) - 0.5) * 0.02;
  const beat = (dt: number, width: number, peak: number) => {
    if (dt >= width) return 0;
    const x = dt / width;
    const rise = x < 0.18 ? x / 0.18 : 1;
    const decay = Math.exp(-(x > 0.18 ? x - 0.18 : 0) * 7);
    const snap = x < 0.12 ? x / 0.12 : 1;
    return peak * rise * decay * snap;
  };
  level += beat(t, 240, 1);
  if (t > 280) level += beat(t - 280, 210, 0.78);
  if (t > 520 && t < 700) level -= 0.05 * ((t - 520) / 180);
  level += (smoothNoise(ms, 420, 0x3b) - 0.5) * 0.04;
  return clamp01(level);
};

const patternHeartbeatAlarm = (ms: number) => {
  const period = 1700;
  const t = ms % period;
  let level = 0.1;
  const beat = (dt: number, width: number, peak: number) => {
    if (dt >= width) return 0;
    const x = dt / width;
    const rise = x < 0.12 ? x / 0.12 : 1;
    const decay = Math.exp(-(x > 0.12 ? x - 0.12 : 0) * 9);
    return peak * rise * decay;
  };
  level += beat(t, 180, 1);
  if (t > 250) level += beat(t - 250, 160, 0.8);
  if (t > 500 && t < 700) level = Math.max(level, 0.18);
  return clamp01(level);
};

const patternComet = (ms: number) => {
  const period = 5200;
  const phase = (ms % period) / period;
  const rise = phase < 0.82 ? phase / 0.82 : 1;
  const fall = phase > 0.82 ? Math.exp(-(phase - 0.82) * 22) : 1;
  const tail = rise * fall;
  const shimmer = (smoothNoise(ms, 90, 0x5a) - 0.5) * 0.08;
  const trail = (smoothNoise(ms, 220, 0x6a) - 0.5) * 0.05;
  return clamp01(0.14 + 0.86 * tail + shimmer + trail);
};

const patternAurora = (ms: number) => {
  const t = ms / 1000;
  const slow = 0.35 + 0.2 * Math.sin(t * 0.18 * TWO_PI + 0.7);
  const mid = 0.18 * Math.sin(t * 0.42 * TWO_PI + Math.sin(t * 0.07 * TWO_PI));
  const noise = (smoothNoise(ms, 900, 0x4c) - 0.5) * 0.1;
  const shimmer = (smoothNoise(ms, 140, 0x5c) - 0.5) * 0.05;
  return clamp01(slow + mid + noise + shimmer);
};

const patternPoliceDE = (ms: number) => {
  const durations = [160, 160, 240, 160, 160, 320];
  const levels = [1, 0, 0.6, 1, 0, 0.4];
  const total = durations.reduce((a, b) => a + b, 0);
  const t = ms % total;
  let acc = 0;
  for (let i = 0; i < durations.length; i += 1) {
    acc += durations[i];
    if (t < acc) return levels[i];
  }
  return 0;
};

const patternCameraFlash = (ms: number) => {
  const period = 5200;
  const t = ms % period;
  const base = 0.08;
  let flash = 0;
  const dbl = hash11(Math.floor(ms / period)) > 0.72;
  const firstDur = 140;
  if (t < firstDur) flash = 1;
  else {
    const decay = Math.exp(-(t - firstDur) / 380);
    flash = 0.8 * decay;
    if (dbl && t > 260 && t < 260 + firstDur) flash = Math.max(flash, 0.9);
  }
  const afterglow = Math.exp(-t / 2200) * 0.15;
  return clamp01(base + flash + afterglow);
};

const patternTVStatic = (ms: number) => {
  const base = 0.4 + (smoothNoise(ms, 45, 0x71) - 0.5) * 0.15;
  const mid = (smoothNoise(ms, 18, 0x72) - 0.5) * 0.22;
  const blip = hash11(ms * 3) > 0.93 ? 0.5 : 0;
  return clamp01(base + mid + blip);
};

const patternHal9000 = (ms: number) => {
  const t = ms / 1000;
  const slow = 0.35 + 0.22 * Math.sin(t * 0.22 * TWO_PI);
  const pulse = 0.25 * Math.sin(t * 1.3 * TWO_PI + 0.6) * Math.sin(t * 0.45 * TWO_PI + 0.9);
  let spike = 0;
  if (hash11(Math.floor(ms / 900)) > 0.88) {
    const x = (ms % 900) / 900;
    spike = 0.35 * Math.exp(-x * 8);
  }
  return clamp01(slow + pulse + spike);
};

const patternSparkle = (ms: number) => {
  const t = ms / 1000;
  const slow = 0.55 + 0.18 * Math.sin(t * 0.35 * TWO_PI);
  const ripple = 0.15 * Math.sin(t * 3.6 * TWO_PI) + 0.1 * Math.sin(t * 5.9 * TWO_PI + 1.1) + 0.05 * Math.sin(t * 11 * TWO_PI + 2);
  return clamp01(slow + ripple);
};

const patternCandle = (ms: number) => {
  const base = 0.36;
  const slow = (smoothNoise(ms, 1000, 0x11) - 0.5) * 0.22;
  const mid = (smoothNoise(ms, 200, 0x22) - 0.5) * 0.26;
  const fast = (smoothNoise(ms, 70, 0x33) - 0.5) * 0.14;
  const spark = (smoothNoise(ms, 40, 0x44) - 0.5) * 0.07;
  let pop = 0;
  if (hash11(Math.floor(ms / 220)) > 0.92) {
    const x = (ms % 220) / 220;
    pop = 0.12 * Math.exp(-x * 10);
  }
  return clamp01(base + slow + mid + fast + spark + pop);
};

const patternCandleSoft = (ms: number) => {
  const base = 0.42;
  const slow = (smoothNoise(ms, 1200, 0x55) - 0.5) * 0.18;
  const mid = (smoothNoise(ms, 260, 0x66) - 0.5) * 0.18;
  const fast = (smoothNoise(ms, 95, 0x77) - 0.5) * 0.08;
  return clamp01(base + slow + mid + fast);
};

const patternCampfire = (ms: number) => {
  const base = 0.45;
  const embers = (smoothNoise(ms, 1500, 0x88) - 0.5) * 0.23;
  const tongues = (smoothNoise(ms, 340, 0x99) - 0.5) * 0.28;
  const sparks = (smoothNoise(ms, 130, 0xaa) - 0.5) * 0.16;
  const crackle = (smoothNoise(ms, 55, 0xbb) - 0.5) * 0.1;
  let burst = 0;
  if (hash11(Math.floor(ms / 180)) > 0.94) {
    const x = (ms % 180) / 180;
    burst = 0.18 * Math.exp(-x * 9);
  }
  return clamp01(base + embers + tongues + sparks + crackle + burst);
};

const patternStepFade = (ms: number) => {
  const stops = [0.15, 0.45, 0.9, 0.35];
  const segmentMs = 2500;
  const seg = Math.floor(ms / segmentMs) % stops.length;
  const progress = (ms % segmentMs) / segmentMs;
  const start = stops[seg];
  const end = stops[(seg + 1) % stops.length];
  return start + (end - start) * progress;
};

const patternTwinkle = (ms: number) => {
  const t = ms / 1000;
  const slow = 0.3 + 0.2 * Math.sin(t * 0.25 * TWO_PI);
  const wave = 0.5 + 0.25 * Math.sin(t * 0.9 * TWO_PI + Math.sin(t * 0.15 * TWO_PI));
  const flicker = 0.08 * Math.sin(t * 7.3 * TWO_PI + 1.7) + 0.05 * Math.sin(t * 12.1 * TWO_PI);
  return clamp01(slow + wave + flicker);
};

const patternDistantStorm = (ms: number) => {
  const base = 0.03 + (smoothNoise(ms, 1400, 0x1a) - 0.5) * 0.03;
  const window = 9000;
  const idx = Math.floor(ms / window);
  const start = idx * window;
  let flash = 0;
  if (hash11(idx * 0xc7) > 0.6) {
    const offset = Math.floor(hash11(idx * 0x31) * (window - 1100));
    const t = ms - start;
    if (t >= offset && t < offset + 1100) {
      const dt = t - offset;
      if (dt < 120) flash = 0.9;
      else flash = 0.7 * Math.exp(-(dt - 120) / 420);
    }
  }
  return clamp01(base + flash);
};

const patternRollingThunder = (ms: number) => {
  const swell = 0.05 + 0.05 * Math.sin((ms / 1000) * 0.35 * TWO_PI);
  const window = 7500;
  const idx = Math.floor(ms / window);
  const start = idx * window;
  let flash = 0;
  if (hash11(idx * 0x99) > 0.5) {
    const baseOff = Math.floor(hash11(idx * 0x21) * (window - 900));
    const t = ms - start;
    if (t >= baseOff && t < baseOff + 400) flash = 1;
    else if (t >= baseOff + 420 && t < baseOff + 900) {
      const dt = t - (baseOff + 420);
      flash = 0.9 * Math.exp(-dt / 250);
    }
  }
  return clamp01(swell + flash);
};

const patternHeatLightning = (ms: number) => {
  const base = 0.04 + (smoothNoise(ms, 1200, 0xa1) - 0.5) * 0.02;
  const period = 6200;
  const phase = (ms % period) / period;
  let env = 0;
  if (phase < 0.3) env = (phase / 0.3) ** 2;
  else if (phase < 0.8) {
    const t = 1 - (phase - 0.3) / 0.5;
    env = t * t;
  }
  const shimmer = (smoothNoise(ms, 90, 0xb2) - 0.5) * 0.08;
  return clamp01(base + 0.55 * env + shimmer);
};

const patternStrobeFront = (ms: number) => {
  const cycle = 9500;
  const t = ms % cycle;
  if (t < 1200) {
    const mod = t % 180;
    return mod < 60 ? 1 : 0.15;
  }
  return 0.05 + (smoothNoise(ms, 800, 0x5e) - 0.5) * 0.03;
};

const patternSheetLightning = (ms: number) => {
  const period = 5200;
  const phase = (ms % period) / period;
  let pulse = 0;
  if (phase < 0.5) {
    const t = phase / 0.5;
    pulse = t * t * 0.9;
  } else {
    const t = 1 - ((phase - 0.5) / 0.5);
    pulse = t * 0.9;
  }
  const flicker = (smoothNoise(ms, 55, 0x7c) - 0.5) * 0.12;
  return clamp01(0.08 + pulse + flicker);
};

const patternThunder = (ms: number) => {
  // reuse rolling thunder flashes for a strong strike
  return Math.max(patternRollingThunder(ms), patternStrobeFront(ms));
};

const patternMixedStorm = (ms: number) => {
  const idx = Math.floor(ms / 5000);
  const choice = hash11(idx * 0xef);
  if (choice < 0.2) return patternDistantStorm(ms);
  if (choice < 0.4) return patternRollingThunder(ms);
  if (choice < 0.6) return patternHeatLightning(ms);
  if (choice < 0.8) return patternStrobeFront(ms);
  return patternThunder(ms);
};

const patternFireflies = (ms: number) => {
  const base = 0.05 + (smoothNoise(ms, 900, 0xd1) - 0.5) * 0.04;
  const window = 1100;
  const idx = Math.floor(ms / window);
  const start = idx * window;
  let flash = 0;
  for (let k = 0; k < 2; k += 1) {
    const salt = idx * 0x9e37 + k * 0x45;
    if (hash11(salt) < 0.35) continue;
    const offset = Math.floor(hash11(salt ^ 0xaa) * (window - 280));
    const t = ms - start;
    if (t >= offset && t < offset + 280) {
      const dt = t - offset;
      const env = Math.exp(-dt / 140);
      flash += 0.75 * env * (0.7 + 0.3 * hash11(salt ^ 0x11));
    }
  }
  return clamp01(base + flash);
};

const patternPopcorn = (ms: number) => {
  const base = 0.04 + (smoothNoise(ms, 800, 0xc5) - 0.5) * 0.03;
  const window = 900;
  const idx = Math.floor(ms / window);
  const start = idx * window;
  let pop = 0;
  for (let k = 0; k < 3; k += 1) {
    const salt = idx * 0x6d + k * 0x23;
    if (hash11(salt) < 0.4 - k * 0.12) continue;
    const offset = Math.floor(hash11(salt ^ 0x77) * (window - 180));
    const t = ms - start;
    if (t >= offset && t < offset + 140) {
      const dt = t - offset;
      pop += Math.exp(-dt / 70) * (0.7 + 0.3 * hash11(salt ^ 0x99));
    }
  }
  return clamp01(base + pop);
};

const patternSunset = (ms: number) => {
  const period = 12000;
  const phase = (ms % period) / period;
  const warm = 0.2 + 0.7 * (1 - Math.cos(phase * Math.PI));
  const flicker = (smoothNoise(ms, 600, 0x123) - 0.5) * 0.05;
  return clamp01(warm + flicker);
};

const patternAlert = (ms: number) => {
  const t = (ms % 900) / 900;
  return t < 0.5 ? 1 : 0.15;
};

const patternSOS = (ms: number) => {
  const unit = 150;
  const seq = [1, 1, 1, 3, 3, 3, 1, 1, 1];
  const total = seq.reduce((a, b) => a + b, 0) * unit * 2;
  const t = ms % total;
  let acc = 0;
  for (const s of seq) {
    const on = unit * s;
    if (t < acc + on) return 1;
    acc += on + unit;
    if (t < acc) return 0.2;
  }
  return 0;
};

const patternEmergencyBridge = (ms: number) => {
  const period = 2600;
  const phase = (ms % period) / period;
  return clamp01(0.2 + Math.sin(phase * TWO_PI * 3) * 0.6 + (smoothNoise(ms, 120, 0x45) - 0.5) * 0.1);
};

const patternArcReactor = (ms: number) => {
  const t = ms / 1000;
  const core = 0.6 + 0.15 * Math.sin(t * 1.2 * TWO_PI);
  const hum = 0.08 * Math.sin(t * 12 * TWO_PI);
  return clamp01(core + hum);
};

const patternWarpCore = (ms: number) => {
  const t = ms / 1000;
  const swell = 0.4 + 0.25 * Math.sin(t * 0.35 * TWO_PI);
  const pulse = 0.18 * Math.sin(t * 4 * TWO_PI + Math.sin(t * 0.4 * TWO_PI));
  return clamp01(swell + pulse);
};

const patternKitt = (ms: number) => {
  const t = ms / 1000;
  const sweep = 0.5 + 0.45 * Math.sin(t * 0.8 * TWO_PI);
  const blink = (smoothNoise(ms, 80, 0x88) - 0.5) * 0.08;
  return clamp01(0.2 + sweep + blink);
};

const patternTronGrid = (ms: number) => {
  const t = ms / 1000;
  const wave = 0.5 + 0.4 * Math.sin(t * 1.8 * TWO_PI);
  const glitch = (smoothNoise(ms, 40, 0x66) - 0.5) * 0.12;
  return clamp01(0.15 + wave + glitch);
};

const patternSaberIdle = (ms: number) => {
  const t = ms / 1000;
  const hum = 0.65 + 0.08 * Math.sin(t * 5 * TWO_PI);
  const jitter = (smoothNoise(ms, 70, 0x35) - 0.5) * 0.05;
  return clamp01(hum + jitter);
};

const patternSaberClash = (ms: number) => {
  const period = 1600;
  const t = ms % period;
  if (t < 120) return 1;
  const decay = Math.exp(-(t - 120) / 260);
  const after = 0.3 + 0.2 * Math.sin(t / 400);
  return clamp01(decay + after);
};

const patternHoliday = (ms: number) => {
  const wave = 0.4 + 0.3 * Math.sin((ms / 1000) * TWO_PI);
  const flicker = (smoothNoise(ms, 120, 0x12) - 0.5) * 0.08;
  return clamp01(wave + flicker);
};

const patternCustom = (ms: number) => {
  return clamp01(0.5 + 0.4 * Math.sin((ms / 1000) * TWO_PI));
};

const patternMusic = (ms: number) => {
  return clamp01(0.5 + 0.25 * Math.sin((ms / 400) * TWO_PI));
};

export const patternFns: ((ms: number) => number)[] = [
  patternConstant,
  patternBreathing,
  patternBreathingWarm,
  patternBreathing2,
  patternSinus,
  patternZigZag,
  patternSawtooth,
  patternPulse,
  patternComet,
  patternAurora,
  patternSparkle,
  patternFireflies,
  patternSunset,
  patternHeartbeat,
  patternHeartbeatAlarm,
  patternAlert,
  patternSOS,
  patternStepFade,
  patternTwinkle,
  patternPopcorn,
  patternCandleSoft,
  patternCandle,
  patternCampfire,
  patternTVStatic,
  patternHal9000,
  patternEmergencyBridge,
  patternArcReactor,
  patternWarpCore,
  patternKitt,
  patternTronGrid,
  patternSaberIdle,
  patternSaberClash,
  patternThunder,
  patternDistantStorm,
  patternRollingThunder,
  patternHeatLightning,
  patternStrobeFront,
  patternSheetLightning,
  patternMixedStorm,
  patternPoliceDE,
  patternCameraFlash,
  patternHoliday,
  patternCustom,
  patternMusic,
];

export function getPatternBrightness(idx: number, ms: number): number {
  const fn = patternFns[idx - 1];
  if (!fn) return 0;
  return fn(ms);
}

