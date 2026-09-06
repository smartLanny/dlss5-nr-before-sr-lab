'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { createSrModelService } = require('../src/product/sr-model-service');

test('per-game SR model selection persists and auto resolves from GPU series', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-sr-model-'));
  const gameDir = path.join(root, 'GameA');
  const exe = path.join(gameDir, 'game.exe');
  fs.mkdirSync(gameDir, { recursive: true });
  fs.writeFileSync(exe, '');
  const calls = [];
  const options = {
    userData: path.join(root, 'userdata'),
    resourcesPath: root,
    appDir: root,
    gameDirectory: () => gameDir,
    gameExecutable: () => exe,
    detectHardware: () => ({ family: 'RTX50', series: ['RTX50'], names: ['NVIDIA GeForce RTX 5090'] }),
    nvapi: { applySrPreset: async request => { calls.push(request); return { ok: true, rawPreset: 13 }; } }
  };
  const service = createSrModelService(options);
  assert.equal((await service.read('a')).selection, 'auto');
  assert.equal((await service.read('a')).effective, 'm');
  assert.equal((await service.write('a', 'k')).selection, 'k');

  const restarted = createSrModelService(options);
  assert.equal((await restarted.read('a')).selection, 'k');
  const applied = await restarted.applyBeforeLaunch('a');
  assert.equal(applied.effective, 'k');
  assert.equal(calls[0].preset, 'k');
  fs.rmSync(root, { recursive: true, force: true });
});

test('unknown hardware auto policy clears a previous override instead of guessing', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-sr-model-'));
  const gameDir = path.join(root, 'GameB');
  const exe = path.join(gameDir, 'game.exe');
  fs.mkdirSync(gameDir, { recursive: true });
  fs.writeFileSync(exe, '');
  let request = null;
  const service = createSrModelService({
    userData: path.join(root, 'userdata'), resourcesPath: root, appDir: root,
    gameDirectory: () => gameDir, gameExecutable: () => exe,
    detectHardware: () => ({ family: 'unknown', series: [], names: [] }),
    nvapi: { applySrPreset: async value => { request = value; return { ok: true, cleared: true }; } }
  });
  const applied = await service.applyBeforeLaunch('b');
  assert.equal(applied.effective, 'default');
  assert.equal(request.preset, 'default');
  fs.rmSync(root, { recursive: true, force: true });
});
