'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createStartupElevation, withPermissionRecovery } = require('../src/product/startup-elevation');

function fixture({ values = ['False', '42'], argv = ['manager.exe'], switches = [], release = false, reacquire = true, ready = async () => {} } = {}) {
  const calls = [], logs = []; let quits = 0, releases = 0, acquisitions = 0;
  const app = { commandLine: { hasSwitch: name => switches.includes(name) }, quit() { quits++; } };
  const elevation = createStartupElevation({ app, processInfo: { platform: 'win32', argv, env: {}, execPath: 'C:\\Manager Folder\\manager.exe' },
    handoff: { begin: () => ({ nonce: '22222222-2222-2222-2222-222222222222' }), wait: ready, cancel() { logs.push({ stage: 'handoff-cancelled' }); }, finish() {} },
    log: (stage, details) => logs.push({ stage, details }), runPowerShell: async (command, timeout) => {
      calls.push({ command, timeout }); const value = values[calls.length - 1]; if (value instanceof Error) throw value; return value;
    }, releaseLock() { releases++; return release; }, reacquireLock() { acquisitions++; return reacquire; }, restoreTimeoutMs: 0 });
  return { elevation, calls, logs, get quits() { return quits; }, get releases() { return releases; }, get acquisitions() { return acquisitions; } };
}

test('ordinary setup performs no token probe until context is explicitly requested and caches its result', async () => {
  const f = fixture(); assert.equal(f.calls.length, 0);
  const [first, second] = await Promise.all([f.elevation.context(), f.elevation.context()]);
  assert.equal(first.privilege, 'standard'); assert.deepEqual(first, second); assert.equal(first.mode, 'normal'); assert.equal(first.sandbox, true);
  assert.equal(f.calls.length, 1); assert.equal(f.calls[0].timeout, 3000); assert.equal(f.quits, 0);
});

test('a blocked token probe reports unknown while preserving UI, and explicit restart does not pretend elevation succeeded', async () => {
  const f = fixture({ values: [new Error('fixture policy denied PowerShell')] });
  assert.equal((await f.elevation.context()).privilege, 'unknown');
  await assert.rejects(f.elevation.relaunchAsAdministrator(), { code: 'STARTUP_ADMIN_CHECK_FAILED' });
  assert.equal(f.calls.length, 1); assert.equal(f.releases, 0); assert.equal(f.quits, 0);
});

test('an already elevated process needs no restart while a standard process preserves only its explicitly chosen startup flags', async () => {
  const admin = fixture({ values: ['True'] }); assert.equal(await admin.elevation.relaunchAsAdministrator(), true); assert.equal(admin.calls.length, 1); assert.equal(admin.quits, 0);
  const ordinary = fixture({ release: true }); assert.equal(await ordinary.elevation.relaunchAsAdministrator(), false);
  assert.equal(ordinary.releases, 1); assert.equal(ordinary.quits, 1); assert.equal(ordinary.elevation.restarting, true);
  assert.match(ordinary.calls[1].command, /--as-admin/); assert.match(ordinary.calls[1].command, /--elevation-attempted/);
  assert.doesNotMatch(ordinary.calls[1].command, /--no-sandbox|--disable-gpu/);
  const compat = fixture({ argv: ['manager.exe', '--no-sandbox'], switches: ['no-sandbox'] });
  await compat.elevation.relaunchAsAdministrator(); assert.match(compat.calls[1].command, /--no-sandbox/);
});

test('permission guidance preserves existing recovery details and leaves unrelated failures untouched', () => {
  const denial = { ok: false, error: { code: 'SETTINGS_LAUNCH_FAILED', message: 'Existing failure',
    details: { pending: true, cause: { code: 'EACCES' }, recoverableDomains: ['sr'] } } };
  const result = withPermissionRecovery(denial);
  assert.equal(result.error.code, denial.error.code); assert.equal(result.error.details.pending, true);
  assert.deepEqual(result.error.details.recoverableDomains, ['sr']); assert.equal(result.error.details.recoveryAction, 'restart-elevated');
  assert.equal(denial.error.details.recoveryAction, undefined, 'does not mutate the recorded transaction result');
  const blocked = { ok: false, error: { code: 'ERR_GAME_RUNNING', message: 'Close the game.' } };
  assert.equal(withPermissionRecovery(blocked), blocked);
});

test('administrator handoff keeps the parent alive and blocks writes until renderer readiness', async () => {
  let acknowledge;
  const f = fixture({ release: true, ready: () => new Promise(resolve => { acknowledge = resolve; }) });
  const pending = f.elevation.relaunchAsAdministrator();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.quits, 0); assert.equal(f.elevation.restarting, true);
  acknowledge(); await pending; assert.equal(f.quits, 1);
});

test('a failed child cancels the handoff and leaves writes suspended while a different instance still holds the lock', async () => {
  const f = fixture({ release: true, reacquire: false, ready: async () => { throw Object.assign(new Error('alive but not ready'), { code: 'STARTUP_CHILD_NOT_READY' }); } });
  await assert.rejects(f.elevation.relaunchAsAdministrator(), { code: 'STARTUP_CHILD_NOT_READY' });
  assert.equal(f.quits, 0); assert.equal(f.elevation.restarting, true); assert.equal(f.acquisitions, 1);
  assert.equal(f.logs.some(row => row.stage === 'handoff-cancelled'), true);
});
