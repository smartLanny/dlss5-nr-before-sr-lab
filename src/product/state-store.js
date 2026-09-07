'use strict';

const fs = require('fs');
const path = require('path');

const DEFAULTS = Object.freeze({
  version: 1,
  scanDrives: false,
  scanFolders: [],
  manualGames: [],
  manualExecutables: [],
  gameOverrides: {},
  excludedRoots: [],
  excludedGames: [],
  lastSelectedGame: null,
  addonVersion: null
});

function uniquePaths(rows) {
  const seen = new Set();
  const out = [];
  for (const row of Array.isArray(rows) ? rows : []) {
    if (typeof row !== 'string' || !path.isAbsolute(row)) continue;
    const key = path.resolve(row).toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(path.resolve(row));
  }
  return out;
}

function uniqueManualExecutables(rows) {
  const seen = new Set();
  const out = [];
  for (const row of Array.isArray(rows) ? rows : []) {
    if (!row || typeof row !== 'object' || typeof row.root !== 'string' || typeof row.file !== 'string') continue;
    if (!path.isAbsolute(row.root) || !path.isAbsolute(row.file) || !/\.exe$/i.test(row.file)) continue;
    const root = path.resolve(row.root);
    const file = path.resolve(row.file);
    const key = `${root.toLowerCase()}|${file.toLowerCase()}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ root, file });
  }
  return out;
}

function normalizeGameOverrides(value) {
  const out = {};
  if (!value || typeof value !== 'object' || Array.isArray(value)) return out;
  for (const [rawDir, row] of Object.entries(value)) {
    if (!path.isAbsolute(rawDir) || !row || typeof row !== 'object') continue;
    const dir = path.resolve(rawDir).toLowerCase();
    const name = typeof row.name === 'string' ? row.name.trim().slice(0, 160) : '';
    const icon = typeof row.icon === 'string' && row.icon.length <= 1024 * 1024 ? row.icon : null;
    const api = ['dx11', 'dx12'].includes(row.api) ? row.api : 'auto';
    const apiExecutable = typeof row.apiExecutable === 'string' && path.isAbsolute(row.apiExecutable)
      ? path.resolve(row.apiExecutable) : null;
    const carrierExecutable = typeof row.carrierExecutable === 'string' && path.isAbsolute(row.carrierExecutable)
      ? path.resolve(row.carrierExecutable) : null;
    const carrierEnabled = typeof row.carrierEnabled === 'boolean' && carrierExecutable ? row.carrierEnabled : null;
    if (!name && !icon && (api === 'auto' || !apiExecutable) && carrierEnabled === null) continue;
    out[dir] = { name, icon, api, apiExecutable, carrierEnabled, carrierExecutable };
  }
  return out;
}

function normalizeExcludedGames(value) {
  const out = [];
  const seen = new Set();
  for (const row of Array.isArray(value) ? value : []) {
    if (!row || typeof row !== 'object') continue;
    const dir = typeof row.dir === 'string' && path.isAbsolute(row.dir) ? path.resolve(row.dir) : null;
    const executable = typeof row.executable === 'string' && path.isAbsolute(row.executable) && /\.exe$/i.test(row.executable)
      ? path.resolve(row.executable)
      : null;
    const launcher = typeof row.launcher === 'string' ? row.launcher.slice(0, 80) : '';
    const id = typeof row.id === 'string' || typeof row.id === 'number' ? String(row.id) : null;
    const appid = typeof row.appid === 'string' || typeof row.appid === 'number' ? String(row.appid) : null;
    if (!dir && !executable && !(launcher && (id || appid))) continue;
    const key = [dir && dir.toLowerCase(), executable && executable.toLowerCase(), launcher.toLowerCase(), id || '', appid || ''].join('|');
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({ dir, executable, launcher, id, appid });
  }
  return out;
}

function validate(value) {
  const state = value && value.version === 1 ? value : {};
  return {
    ...DEFAULTS,
    ...state,
    version: 1,
    scanDrives: state.scanDrives === true,
    scanFolders: uniquePaths(state.scanFolders),
    manualGames: uniquePaths(state.manualGames),
    manualExecutables: uniqueManualExecutables(state.manualExecutables),
    gameOverrides: normalizeGameOverrides(state.gameOverrides),
    excludedRoots: uniquePaths(state.excludedRoots),
    excludedGames: normalizeExcludedGames(state.excludedGames),
    lastSelectedGame: typeof state.lastSelectedGame === 'string' ? state.lastSelectedGame : null,
    addonVersion: typeof state.addonVersion === 'string' ? state.addonVersion : null
  };
}

function createStore(file) {
  function read() {
    try { return validate(JSON.parse(fs.readFileSync(file, 'utf8'))); }
    catch { return validate(null); }
  }
  async function write(patch) {
    const next = validate({ ...read(), ...(patch || {}) });
    await fs.promises.mkdir(path.dirname(file), { recursive: true });
    const temp = `${file}.tmp`;
    await fs.promises.writeFile(temp, JSON.stringify(next, null, 2), 'utf8');
    await fs.promises.rename(temp, file);
    return next;
  }
  return { read, write };
}

module.exports = { DEFAULTS, validate, createStore, uniquePaths, uniqueManualExecutables, normalizeGameOverrides, normalizeExcludedGames };
