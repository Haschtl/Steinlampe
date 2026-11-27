import { Dispatch, SetStateAction } from 'react';

export type ButtonRoleId = 'power' | 'modenext' | 'briup' | 'bridown';

export type ButtonInfo = {
  label: string;
  type: 'toggle' | 'momentary';
  role: ButtonRoleId;
  /** Which output channel this button's action applies to - 'all' or a
   *  numeric channel index as a string. Inert today (single-channel
   *  firmware, see MULTI_CHANNEL_FEATURE_PLAN.md); carried through now so
   *  the UI doesn't need another wire-format bump once channels exist. */
  target: string;
  state: string;
};

/**
 * Parses the firmware's button list (see `buttons=` in src/print.cpp) into
 * structured entries. Accepts both the current `label:type:role:target:state`
 * shape and the older pre-target `label:type:role:state` one (4 fields) a
 * lamp can still be reporting if it hasn't been reflashed since this field
 * was added - this frontend auto-deploys on every push, independently of
 * when any given physical lamp gets reflashed, so the two can disagree for
 * a while. Unknown/malformed segments are skipped rather than throwing,
 * since this is live device data.
 */
function parseButtonsField(raw: string): ButtonInfo[] {
  if (!raw) return [];
  return raw
    .split(',')
    .map((entry) => {
      const parts = entry.split(':');
      let label: string, type: string, role: string, target: string, state: string;
      if (parts.length >= 5) {
        [label, type, role, target, state] = parts;
      } else if (parts.length === 4) {
        [label, type, role, state] = parts;
        target = 'all'; // pre-target firmware had no concept of a target - broadcast is the closest equivalent
      } else {
        return null;
      }
      if (type !== 'toggle' && type !== 'momentary') return null;
      if (role !== 'power' && role !== 'modenext' && role !== 'briup' && role !== 'bridown') return null;
      return { label, type, role, target, state } as ButtonInfo;
    })
    .filter((b): b is ButtonInfo => b !== null);
}

/** Splits a pipe-delimited "PREFIX|key=val|key=val|..." line into a key/value map (prefix excluded). */
function splitKv(line: string): Record<string, string> {
  const kv: Record<string, string> = {};
  line
    .split('|')
    .slice(1)
    .forEach((p) => {
      const eq = p.indexOf('=');
      if (eq > 0) kv[p.slice(0, eq)] = p.slice(eq + 1);
    });
  return kv;
}

export type DeviceStatus = {
  connected: boolean;
  // True once the initial post-connect status snapshot (refreshStatus()) has
  // completed. Effects that fire extra BLE traffic on connect (e.g. profile
  // previews) should gate on this instead of `connected`, so they don't race
  // the status snapshot for the shared notify queue/characteristic.
  statusReady: boolean;
  connecting: boolean;
  deviceName: string;
  lastStatusAt: number | null;
  patternCount: number;
  patternName?: string;
  currentPattern?: number;
  brightness?: number;
  outputLevel?: number; // last applied normalized output (0..100), pre-gamma - what's actually being driven to the LED
  outputSamples?: { t: number; v: number }[]; // rolling window for the live output graph
  firmwareVersion?: string;
  firmwareEnv?: string;
  otaActive?: boolean;
  lampState?: string;
  switchState?: string;
  hasSwitch?: boolean;
  buttons?: ButtonInfo[];
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
  patternInvert?: boolean;
  patternReactive?: boolean;
  outputMode?: "pwm" | "analog";
  idleOffMin?: number;
  wakeActive?: boolean;
  sleepActive?: boolean;
  notifyActive?: boolean;
  idleMinutes?: number;
  pwmCurve?: number;
  pwmRaw?: number;
  pwmMax?: number;
  patternMarginLow?: number;
  patternMarginHigh?: number;
  presenceBle?: string;
  hasPresenceBle?: boolean;
  presenceBleCount?: number;
  presenceBleThreshold?: number;
  presenceBleAutoOn?: boolean;
  presenceBleAutoOff?: boolean;
  presenceBleList?: string[];
  presenceBleGraceMs?: number;
  presenceBleHwOverride?: boolean;
  hasRadar?: boolean;
  radarEnabled?: boolean;
  radarPresent?: boolean;
  radarDistanceCm?: number;
  radarSpeedCmS?: number;
  radarTargetCount?: number;
  radarDimEnabled?: boolean;
  radarDimNearCm?: number;
  radarDimFarCm?: number;
  radarMotionOnEnabled?: boolean;
  radarMotionSpeedThr?: number;
  radarMotionHoldMs?: number;
  radarOffDistanceEnabled?: boolean;
  radarOffDistanceCm?: number;
  radarOffGraceMs?: number;
  radarHwOverride?: boolean;
  radarVelocityFactor?: number;
  quickCsv?: string;
  gestCsv?: string;
  meshNet?: boolean;
  meshNodeCount?: number;
  /** 'provisioner' | 'node' | 'none' - see src/mesh.cpp's meshRoleText(). */
  meshRole?: 'provisioner' | 'node' | 'none';
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
  bleAvailable?: boolean;
  musicGain?: number;
  musicSmooth?: number;
  musicAuto?: boolean;
  musicAutoThr?: number;
  musicMode?: string;
  musicMod?: number;
  musicEnv?: number;
  musicLevel?: number;
  musicKickMs?: number;
  notifyMin?: number;
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
  potiMin?: number;
  potiMax?: number;
  potiInvert?: boolean;
  potiMode?: 'brightness' | 'scan';
  /** 'all' or a numeric channel index as a string - inert today, see ButtonInfo.target. */
  potiTarget?: string;
  potiVal?: number;
  potiRaw?: number;
  hasPush?: boolean;
  pushEnabled?: boolean;
  pushDebounceMs?: number;
  pushDoubleMs?: number;
  pushHoldMs?: number;
  pushStepMs?: number;
  pushStep?: number;
  trustedBle?: string[];
  trustedBleBonds?: string[];
  // Raw "profile show N" response text per slot (or "empty"), keyed by slot
  // number - lives here (global, connection-scoped) rather than as per-card
  // local state so navigating away and back doesn't forget it and flash
  // "Loading..." again for data the device already told us once this
  // session. See memory: profile-preview-global-state.
  profilePreviews?: Record<number, string>;
};

/**
 * Parses a single status/notify line and updates the DeviceStatus.
 * Returns true if the line was handled (and should generally be filtered from log).
 */
export function parseStatusLine(line: string, setStatus: Dispatch<SetStateAction<DeviceStatus>>): boolean {
  if (!line) return false;
  if (line.startsWith('STATE|')) {
    const kv: Record<string, string> = {};
    line
      .split('|')
      .slice(1)
      .forEach((part) => {
        const eq = part.indexOf('=');
        if (eq > 0) kv[part.slice(0, eq)] = part.slice(eq + 1);
      });
    const brightness = kv.bri !== undefined ? parseFloat(kv.bri) : undefined;
    const pattern = kv.pattern !== undefined ? parseInt(kv.pattern, 10) : undefined;
    const poti = kv.poti !== undefined && kv.poti.toUpperCase() !== 'N/A' ? parseFloat(kv.poti) : undefined;
    const potiRaw = kv.poti_raw !== undefined ? parseInt(kv.poti_raw, 10) : undefined;
    const outputLevel = kv.out !== undefined ? parseFloat(kv.out) : undefined;
    setStatus((s) => ({
      ...s,
      lampState: kv.lamp ?? s.lampState,
      brightness: Number.isFinite(brightness ?? NaN) ? brightness : s.brightness,
      currentPattern: Number.isFinite(pattern ?? NaN) ? pattern : s.currentPattern,
      hasPoti: kv.poti !== undefined ? kv.poti.toUpperCase() !== 'N/A' : s.hasPoti,
      potiVal: Number.isFinite(poti ?? NaN) ? poti : s.potiVal,
      potiRaw: Number.isFinite(potiRaw ?? NaN) ? potiRaw : s.potiRaw,
      outputLevel: Number.isFinite(outputLevel ?? NaN) ? outputLevel : s.outputLevel,
      outputSamples: Number.isFinite(outputLevel ?? NaN)
        ? [...(s.outputSamples ?? []).slice(-299), { t: Date.now(), v: outputLevel as number }]
        : s.outputSamples,
      lastStatusAt: Date.now(),
    }));
    return true;
  }
  // [Lamp]/[Clap]/[Brightness] and the rest of this file's old ad-hoc
  // human-text parsers (below, after the structured branches) were deleted
  // here (status protocol consolidation) - every one of them was either
  // chasing a line format the firmware no longer emits (dead), or actively
  // wrong (e.g. the old [Quick]/[PresenceBLE] parsers extracted garbage or
  // clobbered state from unrelated lines), and in every live case the same
  // information already arrives via STATE|/a structured KEY|k=v group line.
  // See memory: status-parsing-consolidation.
  if (line.startsWith('STATUS')) {
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
      // Everything here is the slimmed core line (status protocol redesign) -
      // light_*/music_*/clap_*/poti_*(detail)/push_*(detail)/filter_*/
      // radar_*(detail)/custom_len/custom_step_ms/gamma/pwm_*/bri_min/
      // bri_max/notif_min no longer ever arrive on this line; they're their
      // own on-demand group (LIGHT|/MUSIC|/POTI|/PUSH|/FILTERS|/RADAR|/IO|)
      // or, for custom_len/step, already covered by CUSTOM|. Only the
      // ON/OFF/N/A glance for each feature stays here, for Home chips and
      // the Advanced tile list.
      const hasLight = kv.light ? isAvailable('light') : s.hasLight;
      const hasMusic = kv.music ? isAvailable('music') : s.hasMusic;
      const hasPoti = kv.poti ? isAvailable('poti') : s.hasPoti;
      const buttons = Object.prototype.hasOwnProperty.call(kv, 'buttons') ? parseButtonsField(kv.buttons) : undefined;
      const hasPush = kv.push ? isAvailable('push') : s.hasPush;
      const hasPresenceBle = kv.presence_ble ? isAvailable('presence_ble') : s.hasPresenceBle;
      const hasRadar = kv.radar ? isAvailable('radar') : s.hasRadar;
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
        lampState: kv.lamp ?? s.lampState,
        switchState: kv.switch && kv.switch.toUpperCase() !== 'N/A' ? kv.switch : s.switchState,
        hasSwitch,
        buttons: buttons ?? s.buttons,
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
        wakeActive: kv.wake_active ? kv.wake_active === '1' : s.wakeActive,
        sleepActive: kv.sleep_active ? kv.sleep_active === '1' : s.sleepActive,
        notifyActive: kv.notify_active ? kv.notify_active === '1' : s.notifyActive,
        idleMinutes: asInt('idle_min') ?? s.idleMinutes,
        patternSpeed: asNum('pat_speed') ?? s.patternSpeed,
        patternFade: kv.pat_fade ? (kv.pat_fade === 'off' ? 0 : parseFloat(kv.pat_fade)) : s.patternFade,
        patternInvert: kv.pat_inv
          ? ['1', 'on', 'true'].includes(kv.pat_inv.toLowerCase())
            ? true
            : ['0', 'off', 'false'].includes(kv.pat_inv.toLowerCase())
              ? false
              : s.patternInvert
          : s.patternInvert,
        patternReactive: kv.pat_reactive ? kv.pat_reactive === '1' : s.patternReactive,
        patternMarginLow: asNum('pat_lo') ?? s.patternMarginLow,
        patternMarginHigh: asNum('pat_hi') ?? s.patternMarginHigh,
        quickCsv: kv.quick ?? s.quickCsv,
        gestCsv: Object.prototype.hasOwnProperty.call(kv, 'gest') ? kv.gest : s.gestCsv,
        meshNet: Object.prototype.hasOwnProperty.call(kv, 'mesh_net') ? kv.mesh_net === '1' : s.meshNet,
        meshNodeCount: asInt('mesh_nodes') ?? s.meshNodeCount,
        meshRole:
          kv.mesh_role === 'provisioner' || kv.mesh_role === 'node' || kv.mesh_role === 'none'
            ? kv.mesh_role
            : s.meshRole,
        presenceBle: kv.presence_ble ?? s.presenceBle,
        hasPresenceBle,
        presenceBleCount: asInt('presence_ble_count') ?? s.presenceBleCount,
        hasRadar,
        radarEnabled: kv.radar ? kv.radar.toUpperCase() === 'ON' : hasRadar === false ? false : s.radarEnabled,
        firmwareVersion: kv.fw_ver ?? s.firmwareVersion,
        firmwareEnv: kv.fw_env ?? s.firmwareEnv,
        otaActive: kv.ota_active !== undefined ? kv.ota_active === '1' : s.otaActive,
        bleAvailable: kv.ble ? kv.ble.toUpperCase() !== 'DOWN' : s.bleAvailable,
        hasLight,
        lightEnabled: kv.light ? kv.light.toUpperCase() === 'ON' : hasLight === false ? false : s.lightEnabled,
        hasMusic,
        musicEnabled: kv.music ? kv.music.toUpperCase() === 'ON' : hasMusic === false ? false : s.musicEnabled,
        hasPoti,
        potiEnabled: kv.poti ? kv.poti.toUpperCase() === 'ON' : hasPoti === false ? false : s.potiEnabled,
        hasPush,
        pushEnabled: kv.push ? kv.push.toUpperCase() === 'ON' : hasPush === false ? false : s.pushEnabled,
      };
    });
    return true;
  }
  // Per-page status groups (status protocol redesign) - fetched on demand
  // via "status radar"/"status light"/etc instead of riding along on every
  // STATUS/STATUS1/STATUS2 broadcast. Each merges into the exact same
  // DeviceStatus fields the big STATUS branch above already populates.
  if (line.startsWith('RADAR|')) {
    const kv = splitKv(line);
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
      const hasRadar = kv.radar ? isAvailable('radar') : s.hasRadar;
      return {
        ...s,
        lastStatusAt: Date.now(),
        hasRadar,
        radarEnabled: kv.radar ? kv.radar.toUpperCase() === 'ON' : hasRadar === false ? false : s.radarEnabled,
        radarPresent: kv.radar_present ? kv.radar_present === '1' : s.radarPresent,
        radarDistanceCm: asNum('radar_dist') ?? s.radarDistanceCm,
        radarSpeedCmS: asNum('radar_speed') ?? s.radarSpeedCmS,
        radarTargetCount: asInt('radar_targets') ?? s.radarTargetCount,
        radarDimEnabled: kv.radar_dim ? kv.radar_dim === '1' : s.radarDimEnabled,
        radarDimNearCm: asNum('radar_dim_near') ?? s.radarDimNearCm,
        radarDimFarCm: asNum('radar_dim_far') ?? s.radarDimFarCm,
        radarMotionOnEnabled: kv.radar_motion ? kv.radar_motion === '1' : s.radarMotionOnEnabled,
        radarMotionSpeedThr: asNum('radar_motion_thr') ?? s.radarMotionSpeedThr,
        radarMotionHoldMs: asInt('radar_motion_hold') ?? s.radarMotionHoldMs,
        radarOffDistanceEnabled: kv.radar_offdist ? kv.radar_offdist === '1' : s.radarOffDistanceEnabled,
        radarOffDistanceCm: asNum('radar_off_cm') ?? s.radarOffDistanceCm,
        radarOffGraceMs: asInt('radar_off_grace') ?? s.radarOffGraceMs,
        radarHwOverride: kv.radar_hwoverride ? kv.radar_hwoverride === '1' : s.radarHwOverride,
        radarVelocityFactor: asNum('radar_velocity') ?? s.radarVelocityFactor,
      };
    });
    return true;
  }
  if (line.startsWith('LIGHT|')) {
    const kv = splitKv(line);
    const asNum = (k: string) => {
      const v = parseFloat(kv[k]);
      return Number.isFinite(v) ? v : undefined;
    };
    const isAvailable = (k: string) => (kv[k] ? kv[k].toUpperCase() !== 'N/A' : false);
    setStatus((s) => {
      const hasLight = kv.light ? isAvailable('light') : s.hasLight;
      return {
        ...s,
        lastStatusAt: Date.now(),
        hasLight,
        lightEnabled: kv.light ? kv.light.toUpperCase() === 'ON' : hasLight === false ? false : s.lightEnabled,
        lightGain: asNum('light_gain') ?? s.lightGain,
        lightAlpha: asNum('light_alpha') ?? s.lightAlpha,
        lightClampMin: asNum('light_min') ?? s.lightClampMin,
        lightClampMax: asNum('light_max') ?? s.lightClampMax,
        lightRaw: asNum('light_raw') ?? s.lightRaw,
        lightRawMin: asNum('light_raw_min') ?? s.lightRawMin,
        lightRawMax: asNum('light_raw_max') ?? s.lightRawMax,
      };
    });
    return true;
  }
  if (line.startsWith('MUSIC|')) {
    const kv = splitKv(line);
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
      const hasMusic = kv.music ? isAvailable('music') : s.hasMusic;
      return {
        ...s,
        lastStatusAt: Date.now(),
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
      };
    });
    return true;
  }
  if (line.startsWith('POTI|')) {
    const kv = splitKv(line);
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
      const hasPoti = kv.poti ? isAvailable('poti') : s.hasPoti;
      const potiMode = kv.poti_mode === 'scan' || kv.poti_mode === 'brightness' ? kv.poti_mode : undefined;
      return {
        ...s,
        lastStatusAt: Date.now(),
        hasPoti,
        potiEnabled: kv.poti ? kv.poti.toUpperCase() === 'ON' : hasPoti === false ? false : s.potiEnabled,
        potiAlpha: asNum('poti_alpha') ?? s.potiAlpha,
        potiDelta: asNum('poti_delta') ?? s.potiDelta,
        potiOff: asNum('poti_off') ?? s.potiOff,
        potiSample: asInt('poti_sample') ?? s.potiSample,
        potiMin: asNum('poti_min') ?? s.potiMin,
        potiMax: asNum('poti_max') ?? s.potiMax,
        potiInvert: kv.poti_inv ? kv.poti_inv === '1' : s.potiInvert,
        potiMode: potiMode ?? s.potiMode,
        potiTarget: kv.poti_target ?? s.potiTarget,
        potiVal: asNum('poti_val') ?? s.potiVal,
        potiRaw: asInt('poti_raw') ?? s.potiRaw,
      };
    });
    return true;
  }
  if (line.startsWith('PUSH|')) {
    const kv = splitKv(line);
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
      const hasPush = kv.push ? isAvailable('push') : s.hasPush;
      return {
        ...s,
        lastStatusAt: Date.now(),
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
  if (line.startsWith('PRESENCE|')) {
    const kv = splitKv(line);
    const asInt = (k: string) => {
      const v = parseInt(kv[k] ?? '', 10);
      return Number.isFinite(v) ? v : undefined;
    };
    const isAvailable = (k: string) => (kv[k] ? kv[k].toUpperCase() !== 'N/A' : false);
    setStatus((s) => {
      const hasPresenceBle = kv.presence_ble ? isAvailable('presence_ble') : s.hasPresenceBle;
      const presenceBleList = kv.presence_ble_list
        ? kv.presence_ble_list.split(',').map((x) => x.trim()).filter(Boolean)
        : s.presenceBleList;
      return {
        ...s,
        lastStatusAt: Date.now(),
        presenceBle: kv.presence_ble ?? s.presenceBle,
        hasPresenceBle,
        presenceBleCount: asInt('presence_ble_count') ?? s.presenceBleCount,
        presenceBleThreshold: asInt('presence_ble_thr') ?? s.presenceBleThreshold,
        presenceBleAutoOn: kv.presence_ble_on ? kv.presence_ble_on === '1' : s.presenceBleAutoOn,
        presenceBleAutoOff: kv.presence_ble_off ? kv.presence_ble_off === '1' : s.presenceBleAutoOff,
        presenceBleList,
        presenceBleGraceMs: asInt('presence_ble_grace') ?? s.presenceBleGraceMs,
        presenceBleHwOverride: kv.presence_ble_hwoverride ? kv.presence_ble_hwoverride === '1' : s.presenceBleHwOverride,
      };
    });
    return true;
  }
  if (line.startsWith('FILTERS|')) {
    const kv = splitKv(line);
    const asNum = (k: string) => {
      const v = parseFloat(kv[k]);
      return Number.isFinite(v) ? v : undefined;
    };
    const asInt = (k: string) => {
      const v = parseInt(kv[k] ?? '', 10);
      return Number.isFinite(v) ? v : undefined;
    };
    setStatus((s) => ({
      ...s,
      lastStatusAt: Date.now(),
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
    }));
    return true;
  }
  if (line.startsWith('IO|')) {
    const kv = splitKv(line);
    const asNum = (k: string) => {
      const v = parseFloat(kv[k]);
      return Number.isFinite(v) ? v : undefined;
    };
    const asInt = (k: string) => {
      const v = parseInt(kv[k] ?? '', 10);
      return Number.isFinite(v) ? v : undefined;
    };
    setStatus((s) => {
      const outputMode = kv.out ? (kv.out.toLowerCase().startsWith('ana') ? 'analog' : 'pwm') : undefined;
      return {
        ...s,
        lastStatusAt: Date.now(),
        outputMode: outputMode ?? s.outputMode,
        pwmCurve: asNum('gamma') ?? s.pwmCurve,
        pwmRaw: asInt('pwm_raw') ?? s.pwmRaw,
        pwmMax: asInt('pwm_max') ?? s.pwmMax,
        briMin: asNum('bri_min') ?? s.briMin,
        briMax: asNum('bri_max') ?? s.briMax,
        notifyMin: asNum('notif_min') ?? s.notifyMin,
      };
    });
    return true;
  }
  if (line.startsWith('[Touch] raw=')) {
    // Only the "touch" command's debug snapshot (src/print.cpp's
    // printTouchDebug()) - other "[Touch] ..." lines (detected/double-tap/
    // release/disabled/tune ...) have a different shape and are left to
    // the console log only, not this card's live fields.
    const m = line.match(/thrOn=(-?\d+)\s+thrOff=(-?\d+)/);
    setStatus((s) => ({
      ...s,
      touchThrOn: m ? parseInt(m[1], 10) : s.touchThrOn,
      touchThrOff: m ? parseInt(m[2], 10) : s.touchThrOff,
      lastTouchLine: line,
      lastStatusAt: Date.now(),
    }));
    return true;
  }
  if (line.startsWith('[Trust]')) {
    const lower = line.toLowerCase();
    // "[Trust] BLE bonds: ..." also contains "ble", so it must be checked
    // before the plain trust-list match below or it clobbers trustedBle
    // with the bonds list instead of its own field.
    const isBleBonds = lower.includes('ble bonds');
    const isBle = !isBleBonds && lower.includes('ble');
    const payload = line.includes(':') ? line.slice(line.indexOf(':') + 1) : '';
    const list = payload
      .split(',')
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
    setStatus((s) => ({
      ...s,
      trustedBle: isBle ? list : s.trustedBle,
      trustedBleBonds: isBleBonds ? list : s.trustedBleBonds,
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
        hasPresenceBle: kv.presence_ble ? kv.presence_ble.toUpperCase() !== 'N/A' : s.hasPresenceBle,
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
  // Everything below this point used to include a long tail of ad-hoc
  // human-text parsers (generic "Switch:"/"Touch:" regexes, a "Lamp="
  // fallback, an old "Pattern N/Total - 'name'" line, [Quick]/[Mode]/
  // [PresenceBLE]/[Touch]/[Filter]/Ramp=/RampOn/RampOff/PresenceBLE=) - all
  // deleted (status protocol consolidation). Checked each against what the
  // firmware actually emits today: most matched nothing anymore (dead), a
  // few were actively wrong (e.g. [Quick] extracted "set -> 1,2,3" instead
  // of "1,2,3"; [PresenceBLE] overwrote presenceBle's ON/OFF from ANY
  // "[PresenceBLE] ..." line, including ones that aren't enable/disable
  // confirmations at all, like "[PresenceBLE] Grace 1000 ms"), and the rest
  // were simply superseded by a structured KEY|k=v line that already
  // covers the same ground more reliably. See memory:
  // status-parsing-consolidation.
  let handled = false;
  if (line.startsWith('[Profile ')) {
    const m = line.match(/^\[Profile (\d+)\]\s*(.*)$/);
    if (m) {
      handled = true;
      const slot = parseInt(m[1], 10);
      const raw = m[2];
      setStatus((s) => ({
        ...s,
        profilePreviews: { ...s.profilePreviews, [slot]: raw },
        lastStatusAt: Date.now(),
      }));
    }
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
  return handled;
}
