'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { appError } = require('./errors');
const { detectGpu } = require('./gpu');
const { createNvapiDrs } = require('./nvapi-drs');
const {
  CHOICES,
  normalizeChoice,
  hardwareSeries,
  recommendSrPreset,
  effectiveSrPreset,
  hasFp8Penalty
} = require('./sr-model-policy');

function loadPolicies(file) {
  try {
    const parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
    const out = {};
    for (const [key, value] of Object.entries(parsed && parsed.games || {})) {
      if (typeof key !== 'string' || !path.isAbsolute(key)) continue;
      out[path.resolve(key).toLowerCase()] = normalizeChoice(value);
    }
    return out;
  } catch {
    return {};
  }
}

async function savePolicies(file, games) {
  await fs.promises.mkdir(path.dirname(file), { recursive: true });
  const temp = `${file}.tmp`;
  await fs.promises.writeFile(temp, JSON.stringify({ version: 1, games }, null, 2), 'utf8');
  await fs.promises.rename(temp, file);
}

function resolveHelperPath(resourcesPath, appDir) {
  const packaged = resourcesPath ? path.join(resourcesPath, 'nvapi-drs.ps1') : null;
  if (packaged && fs.existsSync(packaged)) return packaged;
  return path.join(appDir, 'src', 'product', 'nvapi-drs.ps1');
}

function createSrModelService(options) {
  const policyFile = path.join(options.userData, 'sr-model-policies.json');
  const detectHardware = options.detectHardware || detectGpu;
  const gameDirectory = options.gameDirectory;
  const gameExecutable = options.gameExecutable;
  const nvapi = options.nvapi || createNvapiDrs({
    scriptPath: resolveHelperPath(options.resourcesPath, options.appDir)
  });

  function keyForGame(id) {
    const dir = gameDirectory(id);
    if (typeof dir !== 'string' || !path.isAbsolute(dir)) throw appError('ERR_UNKNOWN_GAME');
    return path.resolve(dir).toLowerCase();
  }

  function currentSelection(id) {
    return loadPolicies(policyFile)[keyForGame(id)] || 'auto';
  }

  async function read(id) {
    const hardware = detectHardware();
    const selection = currentSelection(id);
    const recommended = recommendSrPreset(hardware);
    const effective = effectiveSrPreset(selection, hardware);
    return {
      selection,
      effective,
      recommended,
      hardwareSeries: hardwareSeries(hardware),
      hardwareNames: Array.isArray(hardware && hardware.names) ? hardware.names : [],
      hardwareFamily: hardware && hardware.family || 'unknown',
      fp8Penalty: hasFp8Penalty(hardware, effective)
    };
  }

  async function write(id, selection) {
    const normalized = String(selection || '').trim().toLowerCase();
    if (!CHOICES.includes(normalized)) throw appError('ERR_BAD_REQUEST', { selection });
    const games = loadPolicies(policyFile);
    games[keyForGame(id)] = normalized;
    await savePolicies(policyFile, games);
    return read(id);
  }

  async function applyBeforeLaunch(id) {
    const state = await read(id);
    const exe = gameExecutable(id);
    if (typeof exe !== 'string' || !path.isAbsolute(exe) || path.extname(exe).toLowerCase() !== '.exe') {
      throw appError('ERR_NO_GAME_EXE');
    }
    const hash = crypto.createHash('sha256').update(path.resolve(exe).toLowerCase()).digest('hex').slice(0, 8);
    const friendlyName = `Xiaofeng DLSS5 - ${path.basename(exe, path.extname(exe))} [${hash}]`;
    let apply;
    try {
      apply = await nvapi.applySrPreset({ exePath: exe, preset: state.effective, friendlyName });
    } catch (error) {
      apply = { ok: false, code: 'NVAPI_APPLY_FAILED', error: error && error.message ? error.message : String(error) };
    }
    return { ...state, apply };
  }

  return { read, write, applyBeforeLaunch, policyFile };
}

module.exports = { createSrModelService, loadPolicies, savePolicies, resolveHelperPath };
