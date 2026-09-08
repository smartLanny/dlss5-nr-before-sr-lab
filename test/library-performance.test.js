'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { createLibraryService } = require('../src/product/library-service');

function noDlss(exe) {
  const chosen = { path: exe, rel: path.basename(exe), name: path.basename(exe), bitness: 64, api: 'dxgi', apiLabel: 'DirectX 12' };
  return { chosen, exeCandidates: [chosen], dlssFiles: [], primaryDlss: null, reshade: { installed: false } };
}

test('manual refresh with no DLSS never expands the saved root or scans ancestors', async () => {
  const root = path.resolve('build', 'scan-boundary', 'PickedGame'), exe = path.join(root, 'Binaries', 'Win64', 'Game.exe'), visited = [];
  const library = createLibraryService({ library: { discover: () => ({ games: [] }), dedupe: rows => rows },
    scan: { scanGame: async dir => { visited.push(dir); return noDlss(exe); } } });
  const rows = await library.scanAll({ manualGames: [root], manualExecutables: [{ root, file: exe }] });
  assert.deepEqual(visited, [root]); assert.equal(rows[0].dir, root); assert.equal(rows[0].chosen.path, exe);
});

test('EXE root inference stops after binary-layout ancestors and reuses its last scan', async () => {
  const root = path.resolve('build', 'scan-boundary', 'PickedGame'), exe = path.join(root, 'Binaries', 'Win64', 'Game.exe'), visited = [];
  const library = createLibraryService({ library: { discover: () => ({ games: [] }), dedupe: rows => rows },
    scan: { scanGame: async dir => { visited.push(dir); return noDlss(exe); } } });
  const result = await library.prepareSelection(exe);
  assert.deepEqual(visited, [path.dirname(exe), path.join(root, 'Binaries'), root]); assert.equal(result.root, root);
});

test('one refresh indexes each Steam artwork directory once without per-file stat calls', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manager-art-index-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const cache = path.join(root, 'appcache', 'librarycache'); fs.mkdirSync(cache, { recursive: true });
  const games = Array.from({ length: 40 }, (_, i) => ({ id: String(i + 1), launcher: 'Steam', steamRoot: root, dir: path.join(root, `Game${i}`), name: `Game${i}` }));
  for (const game of games) fs.writeFileSync(path.join(cache, `${game.id}_library_600x900.jpg`), 'art');
  const readDir = fs.readdirSync.bind(fs), stat = fs.statSync.bind(fs); let flatReads = 0, artworkStats = 0;
  t.mock.method(fs, 'readdirSync', (dir, ...args) => { if (dir === cache) flatReads++; return readDir(dir, ...args); });
  t.mock.method(fs, 'statSync', (file, ...args) => { if (String(file).startsWith(cache + path.sep)) artworkStats++; return stat(file, ...args); });
  const library = createLibraryService({ library: { discover: () => ({ games }), dedupe: rows => rows },
    scan: { scanGame: async dir => noDlss(path.join(dir, 'Game.exe')) } });
  const result = await library.scanAll({});
  assert.equal(result.length, 40); assert.ok(result.every(game => game.poster)); assert.equal(flatReads, 1); assert.equal(artworkStats, 0);
});

test('EXE selection still reaches a sibling Unreal Engine Plugins DLSS layout', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'manager-ue-plugins-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const exe = path.join(root, 'Project', 'Binaries', 'Win64', 'Game.exe');
  fs.mkdirSync(path.join(root, 'Engine', 'Plugins'), { recursive: true });
  const library = createLibraryService({ scan: { scanGame: async dir => {
    const result = noDlss(exe);
    if (dir === root) { result.dlssFiles = [{ name: 'nvngx_dlss.dll' }]; result.primaryDlss = result.dlssFiles[0]; }
    return result;
  } } });
  assert.equal((await library.findGameRoot(exe)).root, root);
});
