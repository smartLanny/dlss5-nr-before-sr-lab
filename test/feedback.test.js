'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const test = require('node:test');
const assert = require('node:assert/strict');
const { createFeedbackCollector } = require('../src/product/feedback');

test('feedback collector records failures, redacts paths, and includes relevant logs', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'dlss5-feedback-'));
  const userData = path.join(root, 'user-data');
  const gameDir = path.join(root, 'game');
  const exeDir = path.join(gameDir, 'bin');
  fs.mkdirSync(exeDir, { recursive: true });
  fs.writeFileSync(path.join(exeDir, 'ReShade.log'), 'ReShade loaded\ninstall failed\n', 'utf8');

  const collector = createFeedbackCollector({ userData, productVersion: 'test-version' });
  await collector.record({ action: 'game-install', gameId: 'game-1', ok: false, errorCode: 'ERR_NO_WRITE_ACCESS', errorMessage: '没有权限' });

  const report = await collector.buildReport({
    gameId: 'game-1',
    game: {
      name: '示例游戏',
      launcher: '手动添加',
      dir: gameDir,
      chosen: { path: path.join(gameDir, 'bin', 'Game.exe'), apiLabel: 'DirectX 12', bitness: 64 }
    },
    diagnostic: { complete: false, components: [{ label: '写入权限', ok: false, detail: '异常' }] },
    hardware: { family: 'RTX40' },
    payload: { selectedVersion: '0.3.3.4', ready: true, missing: [], invalid: [] },
    settings: { customPath: path.join(os.homedir(), 'Documents', 'Private') }
  });

  assert.match(report.text, /ERR_NO_WRITE_ACCESS/);
  assert.match(report.text, /ReShade loaded/);
  assert.match(report.text, /%USERPROFILE%/);
  assert.doesNotMatch(report.text, new RegExp(os.homedir().replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'));
  assert.match(report.suggestedName, /DLSS5-反馈-示例游戏-/);
});

test('feedback reports anti-cheat confirmation separately from a real failure', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'dlss5-feedback-confirm-'));
  const collector = createFeedbackCollector({ userData: path.join(root, 'user-data'), productVersion: 'test-version' });
  await collector.record({
    action: 'game-repair',
    gameId: 'game-1',
    ok: false,
    outcome: '需确认',
    errorCode: 'ERR_ANTI_CHEAT_CONFIRM',
    errorMessage: '请确认后继续。'
  });

  const report = await collector.buildReport({
    gameId: 'game-1',
    game: { name: '示例游戏', launcher: 'Steam', dir: root, chosen: null },
    diagnostic: { complete: true, components: [] }
  });

  assert.match(report.text, /game-repair \| 需确认 \[ERR_ANTI_CHEAT_CONFIRM\]/);
  assert.doesNotMatch(report.text, /game-repair \| 失败 \[ERR_ANTI_CHEAT_CONFIRM\]/);
});
