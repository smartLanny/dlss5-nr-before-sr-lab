'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { createLaunchCoordinator } = require('../src/product/launch-coordinator');

function fixture(overrides = {}) {
  const calls = [];
  const service = { gameDirectory: () => 'C:/game', gameExecutable: () => 'C:/game/a.exe',
    validateLaunch: async () => calls.push('validate'), launch: async () => { calls.push('launch'); return true; } };
  const settings = { assertReady: async () => calls.push('ready'), hasSrRequest: async () => true,
    beforeLaunch: async () => { calls.push('settings'); return [{ domain: 'sr', applied: true }]; },
    restore: async (_, domain) => calls.push(`restore-${domain}`), ...overrides };
  const legacySrModel = { applyBeforeLaunch: async () => { calls.push('legacy'); return { apply: { ok: true } }; },
    prepareMigration: async () => calls.push('restore-legacy'), write: async () => calls.push('legacy-write') };
  const guards = { assertGameClosed: async () => calls.push('closed') };
  return { calls, service, settings, legacySrModel, module: createLaunchCoordinator({ service, settings, legacySrModel, guards }) };
}

test('managed launch selects one SR writer and applies settings before launch', async () => {
  const f = fixture();
  const result = await f.module.launch('a');
  assert.equal(result.launched, true);
  assert.deepEqual(f.calls, ['ready', 'validate', 'closed', 'settings', 'launch']);
  await assert.rejects(f.module.writeLegacySr('a', 'm'), { code: 'SETTINGS_OWNERSHIP' });
  assert.ok(!f.calls.includes('legacy-write'));
});
test('legacy-only selection is retained until new SR settings explicitly take ownership', async () => {
  const f = fixture({ hasSrRequest: async () => false });
  await f.module.launch('a');
  assert.deepEqual(f.calls, ['ready', 'validate', 'closed', 'settings', 'legacy', 'launch']);
});
test('failed settings and invalid launch preflight cannot start the game', async () => {
  const f = fixture({ beforeLaunch: async () => [{ domain: 'fg', applied: false, code: 'SETTINGS_BACKEND_MISSING', reason: 'missing' }] });
  await assert.rejects(f.module.launch('a'), { code: 'SETTINGS_BACKEND_MISSING' });
  assert.ok(!f.calls.includes('launch'));
  f.service.validateLaunch = async () => { throw new Error('wrong EXE'); };
  f.calls.length = 0;
  await assert.rejects(f.module.launch('a'), /wrong EXE/);
  assert.deepEqual(f.calls, ['ready']);
});
test('no-op restore allows launch and uninstall restores both domains before legacy', async () => {
  const f = fixture({ beforeLaunch: async () => [{ domain: 'sr', applied: false, skipped: true, noOp: true }] });
  assert.equal((await f.module.launch('a')).launched, true);
  f.calls.length = 0;
  await f.module.restoreForUninstall('a');
  assert.deepEqual(f.calls, ['ready', 'closed', 'restore-fg', 'restore-sr', 'restore-legacy']);
});
test('shared IPC serialization waits after errors and never overlaps mutations', async () => {
  const f = fixture(), order = [];
  let release;
  const wait = new Promise(resolve => { release = resolve; });
  const first = f.module.serialize(async () => { order.push('a'); await wait; throw new Error('a failed'); });
  const second = f.module.serialize(async () => order.push('b'));
  await Promise.resolve();
  assert.deepEqual(order, ['a']);
  release();
  await assert.rejects(first, /a failed/);
  await second;
  assert.deepEqual(order, ['a', 'b']);
});
