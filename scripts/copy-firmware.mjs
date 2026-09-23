#!/usr/bin/env node
// Builds one or all "real" (non-base) PlatformIO envs and copies the resulting binaries +
// a combined manifest.json into frontend/public/ota/, so the web UI can compare its bundled
// firmware version/checksum against whatever the connected device reports and offer an OTA
// update. Run from anywhere; paths are resolved relative to the repo root (this script's
// parent directory).
//
// Usage:
//   node scripts/copy-firmware.mjs            # build + copy every real env
//   node scripts/copy-firmware.mjs quarzlampe  # build + copy just one env

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, copyFileSync, writeFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');

function findPioExecutable() {
  if (process.env.PLATFORMIO_PIO_PATH) return process.env.PLATFORMIO_PIO_PATH;
  const candidates = [
    'pio',
    join(homedir(), '.platformio', 'penv', 'bin', 'pio'),
    join(homedir(), '.platformio', 'penv', 'Scripts', 'pio.exe'),
  ];
  for (const candidate of candidates) {
    try {
      execFileSync(candidate, ['--version'], { stdio: 'ignore' });
      return candidate;
    } catch {
      // try the next candidate
    }
  }
  throw new Error(
    'Could not find the "pio" executable (tried PATH and ~/.platformio/penv/bin). ' +
      'Install PlatformIO Core or set PLATFORMIO_PIO_PATH.',
  );
}

const pioExecutable = findPioExecutable();
const platformioIniPath = join(repoRoot, 'platformio.ini');
const versionHeaderPath = join(repoRoot, 'include', 'version.h');
const otaDir = join(repoRoot, 'frontend', 'public', 'ota');
const manifestPath = join(otaDir, 'manifest.json');

// Every env is flashable on its own (e.g. `quarzlampe` is both a real product AND the base
// `quarzlampe_rd03`/`quarzlampe_rd03d` extend) except the shared hardware/platform base at the
// very top of platformio.ini, which carries no product-identifying flags of its own and isn't
// meant to be flashed standalone. That can't be told apart from a real env by regex alone
// (`extends` doesn't imply "not real" - see above), so it's named explicitly here; update this
// if platformio.ini's base env is ever renamed.
const BASE_ENV = 'upesy_wroom';

function discoverRealEnvs() {
  const ini = readFileSync(platformioIniPath, 'utf8');
  const allEnvs = [...ini.matchAll(/^\[env:([^\]]+)\]/gm)].map((m) => m[1]);
  const real = allEnvs.filter((name) => name !== BASE_ENV);
  if (real.length === 0) {
    throw new Error(`No real envs found besides the base "${BASE_ENV}" in ${platformioIniPath}`);
  }
  return real;
}

function readFirmwareVersion() {
  const header = readFileSync(versionHeaderPath, 'utf8');
  const match = header.match(/#define\s+FIRMWARE_VERSION\s+"([^"]+)"/);
  if (!match) {
    throw new Error(`Could not find FIRMWARE_VERSION in ${versionHeaderPath}`);
  }
  return match[1];
}

function buildEnv(envName, version) {
  console.log(`\n=== Building ${envName} ===`);
  execFileSync(pioExecutable, ['run', '-e', envName], { cwd: repoRoot, stdio: 'inherit' });
  const binPath = join(repoRoot, '.pio', 'build', envName, 'firmware.bin');
  if (!existsSync(binPath)) {
    throw new Error(`Expected build output not found: ${binPath}`);
  }
  const bytes = readFileSync(binPath);
  const md5 = createHash('md5').update(bytes).digest('hex');
  const destDir = join(otaDir, envName);
  mkdirSync(destDir, { recursive: true });
  copyFileSync(binPath, join(destDir, 'firmware.bin'));
  console.log(`-> ${envName}: ${bytes.length} bytes, md5=${md5}, version=${version}`);
  // version lives per-env (not at the manifest top level): a single-env rebuild
  // (`node copy-firmware.mjs <env>`) must not make *other*, un-rebuilt envs' entries claim a
  // version they weren't actually built with.
  return { size: bytes.length, md5, version, builtAt: new Date().toISOString() };
}

function main() {
  const requestedEnv = process.argv[2];
  const realEnvs = discoverRealEnvs();
  if (realEnvs.length === 0) {
    throw new Error(`No [env:...] sections found in ${platformioIniPath}`);
  }
  const envsToBuild = requestedEnv ? [requestedEnv] : realEnvs;
  for (const envName of envsToBuild) {
    if (!realEnvs.includes(envName)) {
      throw new Error(`"${envName}" is not a real/flashable env. Known: ${realEnvs.join(', ')}`);
    }
  }

  mkdirSync(otaDir, { recursive: true });
  const version = readFirmwareVersion();
  const existing = existsSync(manifestPath) ? JSON.parse(readFileSync(manifestPath, 'utf8')) : { envs: {} };
  const envs = { ...(existing.envs || {}) };

  for (const envName of envsToBuild) {
    envs[envName] = buildEnv(envName, version);
  }

  const manifest = { envs };
  writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
  console.log(`\nWrote ${manifestPath}`);
  console.log(JSON.stringify(manifest, null, 2));
}

main();
