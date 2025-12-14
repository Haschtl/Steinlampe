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
  hasSwitch?: boolean;
  touchState?: string;
  hasTouch?: boolean;
  touchBase?: number;
  touchRaw?: number;
  touchDelta?: number;
  touchThrOn?: number;
  touchThrOff?: number;
  touchActive?: boolean;
  lastTouchLine?: string;
  touchDimStep?: number;
  filterIir?: boolean;
  filterIirAlpha?: number;
  filterClip?: boolean;
  filterClipAmt?: number;
  filterClipCurve?: number;
  filterTrem?: boolean;
  filterTremRate?: number;
  filterTremDepth?: number;
  filterTremWave?: number;
  filterSpark?: boolean;
  filterSparkDens?: number;
  filterSparkInt?: number;
  filterSparkDecay?: number;
  filterDelay?: boolean;
  filterDelayMs?: number;
  filterDelayFb?: number;
  filterDelayMix?: number;
  filterComp?: boolean;
  filterCompThr?: number;
  filterCompRatio?: number;
  filterCompAttack?: number;
  filterCompRelease?: number;
  filterEnv?: boolean;
  filterEnvAttack?: number;
  filterEnvRelease?: number;
  autoCycle?: boolean;
  patternSpeed?: number;
  patternFade?: number;
  idleOffMin?: number;
  idleMinutes?: number;
  pwmCurve?: number;
  pwmRaw?: number;
  pwmMax?: number;
  patternMarginLow?: number;
  patternMarginHigh?: number;
  presence?: string;
  hasPresence?: boolean;
  quickCsv?: string;
  rampOnMs?: number;
  rampOffMs?: number;
  rampOnEase?: string;
  rampOffEase?: string;
  rampOnPow?: number;
  rampOffPow?: number;
  rampAmbient?: number;
  customLen?: number;
  customStepMs?: number;
  customCsv?: string;
  briMin?: number;
  briMax?: number;
  hasLight?: boolean;
  lightEnabled?: boolean;
  lightGain?: number;
  lightAlpha?: number;
  lightClampMin?: number;
  lightClampMax?: number;
  lightRaw?: number;
  lightRawMin?: number;
  lightRawMax?: number;
  lightAmbMult?: number;
  hasMusic?: boolean;
  musicEnabled?: boolean;
  musicGain?: number;
  musicSmooth?: number;
  musicAuto?: boolean;
  musicAutoThr?: number;
  musicMode?: string;
  musicMod?: number;
  musicEnv?: number;
  musicLevel?: number;
  musicKickMs?: number;
  clapEnabled?: boolean;
  clapThreshold?: number;
  clapCooldownMs?: number;
  clapCmd1?: string;
  clapCmd2?: string;
  clapCmd3?: string;
  patternElapsedMs?: number;
  hasPoti?: boolean;
  potiEnabled?: boolean;
  potiAlpha?: number;
  potiDelta?: number;
  potiOff?: number;
  potiSample?: number;
  hasPush?: boolean;
  pushEnabled?: boolean;
  pushDebounceMs?: number;
  pushDoubleMs?: number;
  pushHoldMs?: number;
  pushStepMs?: number;
  pushStep?: number;
  trustedBle?: string[];
  trustedBt?: string[];
  btSleepBootMs?: number;
  btSleepBleMs?: number;
};

/**
 * Parses a single status/notify line and updates the DeviceStatus.
 * Returns true if the line was handled (and should generally be filtered from log).
 */
export function parseStatusLine(line: string, setStatus: Dispatch<SetStateAction<DeviceStatus>>): boolean {
  if (!line) return false;
  // Parse clap summary lines: "[Clap] ON thr=0.35 cool=800"
  if (line.startsWith('[Clap]')) {
    const lower = line.toLowerCase();
    const enabled = lower.includes('on');
    const thrMatch = lower.match(/thr=([0-9.]+)/);
    const coolMatch = lower.match(/cool=([0-9]+)/);
    const thrVal = thrMatch ? parseFloat(thrMatch[1]) : undefined;
    const coolVal = coolMatch ? parseInt(coolMatch[1] ?? '', 10) : undefined;
    setStatus((s) => ({
      ...s,
      clapEnabled: enabled,
      clapThreshold: Number.isFinite(thrVal ?? NaN) ? thrVal : s.clapThreshold,
      clapCooldownMs: Number.isFinite(coolVal ?? NaN) ? coolVal : s.clapCooldownMs,
    }));
    return true;
  }
  if (line.startsWith('STATUS|')) {
    const parts = line.split('|').slice(1);
    const kv: Record<string, string> = {};
    parts.forEach((p) => {
      const eq = p.indexOf('=');
      if (eq > 0) {
        kv[p.slice(0, eq)] = p.slice(eq + 1);
      }
    });
    const asNum = (k: string) => {
      const v = parseFloat(kv[k]);
      return Number.isFinite(v) ? v : undefined;
    };
    const asInt = (k: string) => {
      const v = parseInt(kv[k] ?? '', 10);
      return Number.isFinite(v) ? v : undefined;
    };
    const isAvailable = (k: string) => (kv[k] ? kv[k].toUpperCase() !== 'N/A' : false);
    setStatus((s) => {
      const hasLight = kv.light ? isAvailable('light') : s.hasLight;
      const lightRaw = kv.light_raw ? asNum('light_raw') : s.hasLight ? s.lightRaw : undefined;
      const lightRawMin = kv.light_raw_min ? asNum('light_raw_min') : s.lightRawMin;
      const lightRawMax = kv.light_raw_max ? asNum('light_raw_max') : s.lightRawMax;
      const pwmRaw = kv.pwm_raw ? asInt('pwm_raw') : undefined;
      const pwmMax = kv.pwm_max ? asInt('pwm_max') : undefined;
      const hasMusic = kv.music ? isAvailable('music') : s.hasMusic;
      const hasPoti = kv.poti ? isAvailable('poti') : s.hasPoti;
      const hasPush = kv.push ? isAvailable('push') : s.hasPush;
      const hasPresence = kv.presence ? isAvailable('presence') : s.hasPresence;
      const hasSwitch = kv.switch ? kv.switch.toUpperCase() !== 'N/A' : s.hasSwitch;
      const hasTouch = kv.touch ? isAvailable('touch') : s.hasTouch;
      return {
        ...s,
        lastStatusAt: Date.now(),
        patternCount: asInt('pattern_total') ?? s.patternCount,
        currentPattern: asInt('pattern') ?? s.currentPattern,
        patternName: kv.pattern_name ?? s.patternName,
        patternElapsedMs: asInt('pat_ms') ?? s.patternElapsedMs,
        autoCycle: kv.auto ? kv.auto === '1' : s.autoCycle,
        brightness: asNum('bri') ?? s.brightness,
        cap: asNum('cap') ?? s.cap,
        lampState: kv.lamp ?? s.lampState,
        switchState: kv.switch && kv.switch.toUpperCase() !== 'N/A' ? kv.switch : s.switchState,
        hasSwitch,
        hasTouch,
        touchState: kv.touch
          ? kv.touch.toUpperCase()
          : kv.touch_dim
          ? ['1', 'ON'].includes(kv.touch_dim.toUpperCase())
            ? 'TOUCHDIM'
            : 'OFF'
          : s.touchState,
        touchDimStep: asNum('touch_dim_step') ?? s.touchDimStep,
        rampOnMs: asInt('ramp_on_ms') ?? s.rampOnMs,
        rampOffMs: asInt('ramp_off_ms') ?? s.rampOffMs,
        rampOnEase: kv.ramp_on_ease ?? s.rampOnEase,
        rampOffEase: kv.ramp_off_ease ?? s.rampOffEase,
        rampOnPow: asNum('ramp_on_pow') ?? s.rampOnPow,
        rampOffPow: asNum('ramp_off_pow') ?? s.rampOffPow,
        rampAmbient: asNum('ramp_amb') ?? s.rampAmbient,
        idleOffMin: asInt('idle_min') ?? s.idleOffMin,
        idleMinutes: asInt('idle_min') ?? s.idleMinutes,
        patternSpeed: asNum('pat_speed') ?? s.patternSpeed,
        patternFade: kv.pat_fade ? (kv.pat_fade === 'off' ? 0 : parseFloat(kv.pat_fade)) : s.patternFade,
        patternMarginLow: asNum('pat_lo') ?? s.patternMarginLow,
        patternMarginHigh: asNum('pat_hi') ?? s.patternMarginHigh,
        quickCsv: kv.quick ?? s.quickCsv,
        presence: kv.presence ?? s.presence,
        hasPresence,
        btSleepBootMs: asInt('bt_sleep_boot_ms') ?? s.btSleepBootMs,
        btSleepBleMs: asInt('bt_sleep_ble_ms') ?? s.btSleepBleMs,
        customLen: asInt('custom_len') ?? s.customLen,
        customStepMs: asInt('custom_step_ms') ?? s.customStepMs,
        pwmCurve: asNum('gamma') ?? s.pwmCurve,
        pwmRaw: pwmRaw ?? s.pwmRaw,
        pwmMax: pwmMax ?? s.pwmMax,
        briMin: asNum('bri_min') ?? s.briMin,
        briMax: asNum('bri_max') ?? s.briMax,
        filterIir: kv.filter_iir ? kv.filter_iir.toUpperCase() === 'ON' : s.filterIir,
        filterIirAlpha: asNum('filter_alpha') ?? s.filterIirAlpha,
        filterClip: kv.filter_clip ? kv.filter_clip.toUpperCase() === 'ON' : s.filterClip,
        filterClipAmt: asNum('filter_clip_amt') ?? s.filterClipAmt,
        filterClipCurve: asInt('filter_clip_curve') ?? s.filterClipCurve,
        filterTrem: kv.filter_trem ? kv.filter_trem.toUpperCase() === 'ON' : s.filterTrem,
        filterTremRate: asNum('filter_trem_rate') ?? s.filterTremRate,
        filterTremDepth: asNum('filter_trem_depth') ?? s.filterTremDepth,
        filterTremWave: asInt('filter_trem_wave') ?? s.filterTremWave,
        filterSpark: kv.filter_spark ? kv.filter_spark.toUpperCase() === 'ON' : s.filterSpark,
        filterSparkDens: asNum('filter_spark_dens') ?? s.filterSparkDens,
        filterSparkInt: asNum('filter_spark_int') ?? s.filterSparkInt,
        filterSparkDecay: asInt('filter_spark_decay') ?? s.filterSparkDecay,
        filterDelay: kv.filter_delay ? kv.filter_delay.toUpperCase() === 'ON' : s.filterDelay,
        filterDelayMs: asInt('filter_delay_ms') ?? s.filterDelayMs,
        filterDelayFb: asNum('filter_delay_fb') ?? s.filterDelayFb,
        filterDelayMix: asNum('filter_delay_mix') ?? s.filterDelayMix,
        filterComp: kv.filter_comp ? kv.filter_comp.toUpperCase() === 'ON' : s.filterComp,
        filterCompThr: asNum('filter_comp_thr') ?? s.filterCompThr,
        filterCompRatio: asNum('filter_comp_ratio') ?? s.filterCompRatio,
        filterCompAttack: asInt('filter_comp_att') ?? s.filterCompAttack,
        filterCompRelease: asInt('filter_comp_rel') ?? s.filterCompRelease,
        filterEnv: kv.filter_env ? kv.filter_env.toUpperCase() === 'ON' : s.filterEnv,
        filterEnvAttack: asInt('filter_env_att') ?? s.filterEnvAttack,
        filterEnvRelease: asInt('filter_env_rel') ?? s.filterEnvRelease,
        hasLight,
        lightEnabled: kv.light ? kv.light.toUpperCase() === 'ON' : hasLight === false ? false : s.lightEnabled,
        lightGain: asNum('light_gain') ?? s.lightGain,
        lightAlpha: asNum('light_alpha') ?? s.lightAlpha,
        lightClampMin: asNum('light_min') ?? s.lightClampMin,
        lightClampMax: asNum('light_max') ?? s.lightClampMax,
        lightRaw,
        lightRawMin,
        lightRawMax,
        hasMusic,
        musicEnabled: kv.music ? kv.music.toUpperCase() === 'ON' : hasMusic === false ? false : s.musicEnabled,
        musicGain: asNum('music_gain') ?? s.musicGain,
        musicSmooth: asNum('music_smooth') ?? s.musicSmooth,
        musicEnv: asNum('music_env') ?? s.musicEnv,
        musicLevel: asNum('music_level') ?? s.musicLevel,
        musicAuto: kv.music_auto ? kv.music_auto.toUpperCase() === 'ON' : s.musicAuto,
        musicAutoThr: asNum('music_thr') ?? s.musicAutoThr,
        musicMode: kv.music_mode ?? s.musicMode,
        musicMod: asNum('music_mod') ?? s.musicMod,
        musicKickMs: asNum('music_kick_ms') ?? s.musicKickMs,
        clapEnabled: kv.clap ? kv.clap.toUpperCase() === 'ON' : hasMusic === false ? false : s.clapEnabled,
        clapThreshold: asNum('clap_thr') ?? s.clapThreshold,
        clapCooldownMs: asInt('clap_cool') ?? s.clapCooldownMs,
        clapCmd1: kv.clap_cmd1 ?? s.clapCmd1,
        clapCmd2: kv.clap_cmd2 ?? s.clapCmd2,
        clapCmd3: kv.clap_cmd3 ?? s.clapCmd3,
        hasPoti,
        potiEnabled: kv.poti ? kv.poti.toUpperCase() === 'ON' : hasPoti === false ? false : s.potiEnabled,
        potiAlpha: asNum('poti_alpha') ?? s.potiAlpha,
        potiDelta: asNum('poti_delta') ?? s.potiDelta,
        potiOff: asNum('poti_off') ?? s.potiOff,
        potiSample: asInt('poti_sample') ?? s.potiSample,
        hasPush,
        pushEnabled: kv.push ? kv.push.toUpperCase() === 'ON' : hasPush === false ? false : s.pushEnabled,
        pushDebounceMs: asInt('push_db') ?? s.pushDebounceMs,
        pushDoubleMs: asInt('push_dbl') ?? s.pushDoubleMs,
        pushHoldMs: asInt('push_hold') ?? s.pushHoldMs,
        pushStepMs: asInt('push_step_ms') ?? s.pushStepMs,
        pushStep: asNum('push_step') ?? s.pushStep,
      };
    });
    return true;
  }
  if (line.startsWith('[Trust]')) {
    const lower = line.toLowerCase();
    const isBle = lower.includes('ble');
    const isBt = lower.includes('bt');
    const payload = line.includes(':') ? line.slice(line.indexOf(':') + 1) : '';
    const list = payload
      .split(',')
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
    setStatus((s) => ({
      ...s,
      trustedBle: isBle ? list : s.trustedBle,
      trustedBt: isBt ? list : s.trustedBt,
      lastStatusAt: Date.now(),
    }));
    return true;
  }
  if (line.startsWith('SENSORS|')) {
    const parts = line.split('|').slice(1);
    const kv: Record<string, string> = {};
    parts.forEach((p) => {
      const eq = p.indexOf('=');
      if (eq > 0) kv[p.slice(0, eq)] = p.slice(eq + 1);
    });
    const num = (k: string) => {
      const v = parseFloat(kv[k]);
      return Number.isFinite(v) ? v : undefined;
    };
    setStatus((s) => {
      const lightRaw = kv.light_raw;
      const hasLight = lightRaw ? lightRaw.toUpperCase() !== 'N/A' : s.hasLight;
      const lightRawVal = num('light_raw') ?? s.lightRaw;
      const lightMinVal = num('light_min') ?? s.lightRawMin;
      const lightMaxVal = num('light_max') ?? s.lightRawMax;
      const musicEnv = num('music_env') ?? s.musicEnv;
      const musicLevel = num('music_level') ?? s.musicLevel;
      const musicSmooth = num('music_smooth') ?? s.musicSmooth;
      const lightAmbMult = num('light_amb_mult') ?? s.lightAmbMult;
      const rampAmb = num('ramp_amb') ?? s.rampAmbient;
      const musicKickMs = num('music_kick_ms') ?? s.musicKickMs;
      return {
        ...s,
        lastStatusAt: Date.now(),
        touchBase: num('touch_base') ?? s.touchBase,
        touchRaw: num('touch_raw') ?? s.touchRaw,
        touchDelta: num('touch_delta') ?? s.touchDelta,
        touchActive: kv.touch_active ? kv.touch_active === '1' : s.touchActive,
        hasLight,
        lightRaw: lightRawVal,
        lightRawMin: lightMinVal,
        lightRawMax: lightMaxVal,
        hasPoti: kv.poti ? kv.poti.toUpperCase() !== 'N/A' : s.hasPoti,
        hasPush: kv.push ? kv.push.toUpperCase() !== 'N/A' : s.hasPush,
        hasMusic: kv.music ? kv.music.toUpperCase() !== 'N/A' : s.hasMusic,
        hasPresence: kv.presence ? kv.presence.toUpperCase() !== 'N/A' : s.hasPresence,
        touchState: kv.touch ? kv.touch.toUpperCase() : s.touchState,
        musicEnv,
        musicLevel,
        musicSmooth,
        musicKickMs,
        lightAmbMult,
        rampAmbient: rampAmb,
      };
    });
    return true;
  }
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
  if (line.startsWith('[Mode]')) {
    handled = true;
    const num = line.match(/Mode\]\s*([0-9]+)/i);
    const total = line.match(/\/\s*([0-9]+)/);
    const name = line.split('-').slice(1).join('-').trim();
    setStatus((s) => ({
      ...s,
      currentPattern: num ? parseInt(num[1], 10) : s.currentPattern,
      patternCount: total ? parseInt(total[1], 10) : s.patternCount,
      patternName: name || s.patternName,
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('[Presence]')) {
    handled = true;
    const enabled = line.toLowerCase().includes('enabled');
    // const disabled = line.toLowerCase().includes("disabled");
    const unavailable = line.toUpperCase().includes("N/A");
    setStatus((s) => ({
      ...s,
      hasPresence: unavailable ? false : true,
      presence: enabled ? 'ON' : 'OFF',
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('[Touch]')) {
    handled = true;
    const active = line.includes('active=1');
    const base = line.match(/base=([0-9]+)/);
    const raw = line.match(/raw=([0-9]+)/);
    const delta = line.match(/delta=([-0-9]+)/);
    const thrOn = line.match(/thrOn=([0-9]+)/);
    const thrOff = line.match(/thrOff=([0-9]+)/);
    setStatus((s) => ({
      ...s,
      touchState: active ? 'TOUCH' : 'idle',
      touchBase: base ? parseInt(base[1], 10) : s.touchBase,
      touchRaw: raw ? parseInt(raw[1], 10) : s.touchRaw,
        touchDelta: delta ? parseInt(delta[1], 10) : s.touchDelta,
        touchThrOn: thrOn ? parseInt(thrOn[1], 10) : s.touchThrOn,
        touchThrOff: thrOff ? parseInt(thrOff[1], 10) : s.touchThrOff,
        touchActive: active,
        lastTouchLine: line,
        touchDimStep: s.touchDimStep,
        lastStatusAt: Date.now(),
      }));
  }
  if (line.startsWith('[Filter]')) {
    handled = true;
    const lower = line.toLowerCase();
    const hasLbl = (lbl: string) =>
      lower.includes(`${lbl}=`) || new RegExp(`\\b${lbl}\\s+(on|off)\\b`).test(lower);
    const onOff = (lbl: string, prev?: boolean) => {
      const eqMatch = lower.match(new RegExp(`${lbl}=([a-z]+)`));
      if (eqMatch && eqMatch[1]) return eqMatch[1] === 'on';
      const plain = lower.match(new RegExp(`\\b${lbl}\\s+(on|off)\\b`));
      if (plain && plain[1]) return plain[1] === 'on';
      return prev ?? false;
    };
    const numInParens = (lbl: string) => {
      const match = line.match(new RegExp(`${lbl}=\\w+\\(([^)]+)\\)`));
      if (match && match[1]) {
        const parts = match[1].split(/[\\/]/).map((p) => parseFloat(p));
        return parts;
      }
      return [];
    };
    const parseNumAfter = (lbl: string) => {
      const match = line.match(new RegExp(`${lbl}=([0-9.+-]+)`));
      return match ? parseFloat(match[1]) : undefined;
    };
    setStatus((s) => {
      const iirParts = numInParens('iir');
      const clipParts = numInParens('clip');
      const tremParts = numInParens('trem');
      const sparkParts = numInParens('spark');
      const compParts = numInParens('comp');
      const envParts = numInParens('env');
      const alphaInline = parseNumAfter('alpha');
      const clipAmtInline = parseNumAfter('amt');
      const tremRateInline = parseNumAfter('rate');
      const tremDepthInline = parseNumAfter('depth');
      const sparkDensInline = parseNumAfter('dens');
      const sparkIntInline = parseNumAfter('intensity') ?? parseNumAfter('int');
      const sparkDecayInline = parseNumAfter('decay');
      const compThrInline = parseNumAfter('thr');
      const compRatioInline = parseNumAfter('ratio');
      const compAttInline = parseNumAfter('att');
      const compRelInline = parseNumAfter('comp_rel');
      const envAttInline = parseNumAfter('att');
      const envRelInline = parseNumAfter('env_rel');
      const delayMs = parseNumAfter('filter_delay_ms') ?? (numInParens('delay')[0] ?? undefined);
      return {
        ...s,
        filterIir: onOff('iir', s.filterIir),
        filterIirAlpha: iirParts[0] ?? alphaInline ?? s.filterIirAlpha,
        filterClip: onOff('clip', s.filterClip),
        filterClipAmt: clipParts[0] ?? clipAmtInline ?? s.filterClipAmt,
        filterTrem: onOff('trem', s.filterTrem),
        filterTremRate: tremParts[0] ?? tremRateInline ?? s.filterTremRate,
        filterTremDepth: tremParts[1] ?? tremDepthInline ?? s.filterTremDepth,
        filterSpark: onOff('spark', s.filterSpark),
        filterSparkDens: sparkParts[0] ?? sparkDensInline ?? s.filterSparkDens,
        filterSparkInt: sparkParts[1] ?? sparkIntInline ?? s.filterSparkInt,
        filterSparkDecay: sparkParts[2] ? Number(sparkParts[2]) : sparkDecayInline ?? s.filterSparkDecay,
        filterComp: onOff('comp', s.filterComp),
        filterCompThr: compParts[0] ?? compThrInline ?? s.filterCompThr,
        filterCompRatio: compParts[1] ?? compRatioInline ?? s.filterCompRatio,
        filterCompAttack: compParts[2] ? Number(compParts[2]) : compAttInline ?? s.filterCompAttack,
        filterCompRelease: compParts[3] ? Number(compParts[3]) : compRelInline ?? s.filterCompRelease,
        filterEnv: onOff('env', s.filterEnv),
        filterEnvAttack: envParts[0] ? Number(envParts[0]) : envAttInline ?? s.filterEnvAttack,
        filterEnvRelease: envParts[1] ? Number(envParts[1]) : envRelInline ?? s.filterEnvRelease,
        filterDelay: onOff('delay', s.filterDelay),
        filterDelayMs: delayMs ?? s.filterDelayMs,
      };
    });
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
  if (line.startsWith('CUSTOM|')) {
    handled = true;
    const parts = line.split('|').slice(1);
    const kv: Record<string, string> = {};
    parts.forEach((p) => {
      const eq = p.indexOf('=');
      if (eq > 0) kv[p.slice(0, eq)] = p.slice(eq + 1);
    });
    setStatus((s) => ({
      ...s,
      customLen: kv.len ? parseInt(kv.len, 10) : s.customLen,
      customStepMs: kv.step ? parseInt(kv.step, 10) : s.customStepMs,
      customCsv: kv.vals ?? s.customCsv,
      lastStatusAt: Date.now(),
    }));
  }
  if (line.startsWith('[Music]')) {
    handled = true;
    setStatus((s) => ({ ...s, lastStatusAt: Date.now() }));
  }
  if (line.startsWith("[Light]")) {
    const rawMatch = line.match(/raw=([0-9.]+)/i);
    const minMatch = line.match(/min=([0-9.]+)/i);
    const maxMatch = line.match(/max=([0-9.]+)/i);
    const ambMatch = line.match(/ambx=([0-9.]+)/i);
    const ambFactorMatch = line.match(/rampamb=([0-9.]+)/i);
    const rawVal = rawMatch ? parseFloat(rawMatch[1]) : undefined;
    const minVal = minMatch ? parseFloat(minMatch[1]) : undefined;
    const maxVal = maxMatch ? parseFloat(maxMatch[1]) : undefined;
    const ambVal = ambMatch ? parseFloat(ambMatch[1]) : undefined;
    const ambFacVal = ambFactorMatch ? parseFloat(ambFactorMatch[1]) : undefined;
    setStatus((s) => ({
      ...s,
      lightRaw: Number.isFinite(rawVal ?? NaN) ? rawVal : s.lightRaw,
      lightRawMin: Number.isFinite(minVal ?? NaN) ? minVal : s.lightRawMin,
      lightRawMax: Number.isFinite(maxVal ?? NaN) ? maxVal : s.lightRawMax,
      lightAmbMult: Number.isFinite(ambVal ?? NaN) ? ambVal : s.lightAmbMult,
      rampAmbient: Number.isFinite(ambFacVal ?? NaN) ? ambFacVal : s.rampAmbient,
      hasLight: line.toUpperCase().includes("N/A") ? false : s.hasLight ?? true,
      lightEnabled: line.toUpperCase().includes("N/A")
        ? false
        : s.lightEnabled ?? true,
      lastStatusAt: Date.now(),
    }));
    handled =true;
  }

  return handled;
}
