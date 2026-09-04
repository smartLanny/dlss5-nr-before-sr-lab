'use strict';

const fs = require('fs');
const path = require('path');

const DEFAULTS = Object.freeze({
  version: 1,
  scanDrives: false,
  scanFolders: [],
  manualGames: [],
  excludedRoots: [],
  lastSelectedGame: null
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

function validate(value) {
  const state = value && value.version === 1 ? value : {};
  return {
    ...DEFAULTS,
    ...state,
    version: 1,
    scanDrives: state.scanDrives === true,
    scanFolders: uniquePaths(state.scanFolders),
    manualGames: uniquePaths(state.manualGames),
    excludedRoots: uniquePaths(state.excludedRoots),
    lastSelectedGame: typeof state.lastSelectedGame === 'string' ? state.lastSelectedGame : null
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

module.exports = { DEFAULTS, validate, createStore, uniquePaths };
