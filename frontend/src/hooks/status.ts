import { Dispatch, SetStateAction } from 'react';

export type DeviceStatus = {
  connected: boolean;
  connecting: boolean;
  deviceName: string;
  lastStatusAt: number | null;
  patternCount: number;
  patternName?: string;
  currentPattern?: number;
  brightness?: number;
  cap?: number;
  lampState?: string;
  switchState?: string;
  touchState?: string;
  autoCycle?: boolean;
  patternSpeed?: number;
  patternFade?: number;
  idleOffMin?: number;
  idleMinutes?: number;
  pwmCurve?: number;
  presence?: string;
  quickCsv?: string;
  rampOnMs?: number;
  rampOffMs?: number;
  customLen?: number;
  customStepMs?: number;
};

/**
 * Parses a single status/notify line and updates the DeviceStatus.
 * Returns true if the line was handled (and should generally be filtered from log).
 */
export function parseStatusLine(line: string, setStatus: Dispatch<SetStateAction<DeviceStatus>>): boolean {
  if (!line) return false;
  let handled = false;
  const swAny = line.match(/Switch[:=]\s*([A-Za-z0-9]+)/i);
  const touchAny = line.match(/Touch[:=]\s*([A-Za-z0-9]+)/i);
  if (swAny) {
    handled = true;
    setStatus((s) => ({ ...s, switchState: swAny[1].toUpperCase(), lastStatusAt: Date.now() }));
  }
  if (touchAny) {
    handled = true;
    setStatus((s) => ({ ...s, touchState: touchAny[1].toUpperCase(), lastStatusAt: Date.now() }));
  }
  if (line.includes('Lamp=')) {
    handled = true;
    const b = line.match(/Brightness=([0-9.]+)/);
    const c = line.match(/Cap=([0-9.]+)/);
    const lamp = line.match(/Lamp=([A-Z]+)/);
    const sw = line.match(/Switch=([A-Z]+)/);
    setStatus((s) => ({
      ...s,
      brightness: b ? parseFloat(b[1]) : s.brightness,
      cap: c ? parseFloat(c[1]) : s.cap,
      lampState: lamp ? lamp[1] : s.lampState,
      switchState: sw ? sw[1] : s.switchState,
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('Pattern ')) {
    const modeMatch = line.match(/Pattern\s+(\d+)\/(\d+).*?'([^']+)'/);
    const autoMatch = line.match(/AutoCycle=(ON|OFF)/);
    const speedMatch = line.match(/Speed=([0-9.]+)/);
    if (modeMatch) {
      handled = true;
      const idx = parseInt(modeMatch[1], 10);
      const total = parseInt(modeMatch[2], 10);
      setStatus((s) => ({
        ...s,
        currentPattern: !Number.isNaN(idx) ? idx : s.currentPattern,
        patternCount: !Number.isNaN(total) ? total : s.patternCount,
        patternName: modeMatch[3] || s.patternName,
        autoCycle: autoMatch ? autoMatch[1] === 'ON' : s.autoCycle,
        patternSpeed: speedMatch ? parseFloat(speedMatch[1]) : s.patternSpeed,
        lastStatusAt: Date.now(),
      }));
    }
  }
  if (line.startsWith('[Quick]')) {
    handled = true;
    const csv = line.replace('[Quick]', '').trim();
    setStatus((s) => ({ ...s, quickCsv: csv, lastStatusAt: Date.now() }));
  }
  if (line.startsWith('[Touch]')) {
    handled = true;
    const active = line.includes('active=1');
    setStatus((s) => ({ ...s, touchState: active ? 'TOUCH' : 'idle', lastStatusAt: Date.now() }));
  }
  if (line.startsWith('Ramp=')) {
    handled = true;
    const onMs = line.match(/Ramp=([0-9]+)/);
    const offMs = line.match(/\/\s*([0-9]+)ms/);
    const idle = line.match(/IdleOff=([0-9]+)m/);
    const patFade = line.match(/PatFade=ON\(([0-9.]+)\)/);
    const touchDim = line.match(/TouchDim=(ON|OFF)/);
    setStatus((s) => ({
      ...s,
      rampOnMs: onMs ? parseInt(onMs[1], 10) : s.rampOnMs,
      rampOffMs: offMs ? parseInt(offMs[1], 10) : s.rampOffMs,
      idleOffMin: idle ? parseInt(idle[1], 10) : s.idleOffMin,
      idleMinutes: idle ? parseInt(idle[1], 10) : s.idleMinutes,
      patternFade: patFade ? parseFloat(patFade[1]) : s.patternFade,
      touchState: touchDim ? (touchDim[1] === 'ON' ? 'TOUCHDIM' : s.touchState) : s.touchState,
      lastStatusAt: Date.now(),
    }));
  }
  if (/RampOn/i.test(line) || /RampOff/i.test(line)) {
    handled = true;
    const on = line.match(/RampOn[:=]?\s*([0-9]+)/i);
    const off = line.match(/RampOff[:=]?\s*([0-9]+)/i);
    setStatus((s) => ({
      ...s,
      rampOnMs: on ? parseInt(on[1], 10) : s.rampOnMs,
      rampOffMs: off ? parseInt(off[1], 10) : s.rampOffMs,
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('Presence=')) {
    handled = true;
    setStatus((s) => ({ ...s, presence: line.replace('Presence=', '').trim(), lastStatusAt: Date.now() }));
  }
  if (line.startsWith('[Custom]')) {
    handled = true;
    const len = line.match(/len=([0-9]+)/);
    const step = line.match(/stepMs=([0-9]+)/);
    setStatus((s) => ({
      ...s,
      customLen: len ? parseInt(len[1], 10) : s.customLen,
      customStepMs: step ? parseInt(step[1], 10) : s.customStepMs,
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('[Light]') || line.startsWith('[Music]')) {
    handled = true;
    setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
  }
  return handled;
}
