'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { createOperationPlans } = require('../src/product/operation-plan');
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'operation-plan-')); t.after(() => { assert.equal(path.dirname(dir), path.resolve(os.tmpdir())); fs.rmSync(dir, { force: true, recursive: true }); });
  const exe = path.join(dir, 'game.exe'); fs.writeFileSync(exe, 'original exe');
  const config = path.join(dir, 'nr_before_sr.ini'); fs.writeFileSync(config, '[NRBeforeSR]\nIntensity=1\n');
  const calls = [], state = { gameOverrides: {} }, deployment = { mode: 'local', loadingMode: 'proxy', generation: null, runtimeDir: dir, nrConfigDir: dir, activeConfigPath: path.join(dir, 'ReShade.ini') };
  const service = { gameExecutable: () => exe, gameDirectory: () => dir, getLayout: () => deployment, store: { read: () => state },
    listGames: async () => [{ id: 'game', addonVersion: 'core1' }], payloadState: () => ({ settings: { addonVersion: 'core1' } }),
    previewDeployment: async () => ({ planId: crypto.randomUUID(), changes: [{ role: 'core', path: path.join(dir, 'addon.dll'), beforeSha256: 'a', afterSha256: 'b', action: 'replace' },
      { role: 'receipt', path: path.join(dir, 'receipt.json'), beforeSha256: null, afterSha256: crypto.randomUUID(), action: 'create' }] }),
    applyDeployment: async () => { calls.push('deployment'); }, writeNrSettings: async (_id, patch) => { calls.push('nr'); fs.writeFileSync(config, JSON.stringify(patch)); },
    recoverDeployment: async () => { calls.push('recover-deployment'); }, inspectDeployment: async () => ({ pending: false, needsRecovery: false }),
    previewUninstall: async (_id, request) => ({ changes: [{ action: request.mode, path: path.join(dir, 'core.dll') }] }),
    uninstall: async (_id, request) => { calls.push(request.mode); return { removed: true }; } };
  const settings = { assertReady: async () => {}, pending: async () => [], inspect: async () => ({ applied: {}, requests: {} }), restore: async () => { calls.push('restore-fg'); },
    preview: async (_id, domain, request) => ({ operations: [{ action: 'set', domain, value: request.quality || request.mode }], blockers: [], destination: 'fixture', request }) };
  const options = { userData: path.join(dir, 'manager'), service, settings,
    components: { inspect: async () => ({}), restore: async () => { calls.push('restore-components'); } }, fgWorkflow: { recover: async () => {} },
    environment: { assertReady: async () => {}, recoverPending: async () => {} }, preparation: { assertReady: async () => {}, inspect: async () => ({ pending: false }) },
    guards: { assertGameClosed: async () => {} }, applyEnhancement: async (_id, domain) => { calls.push(domain); },
    restoreForUninstall: async () => { calls.push('restore-launch-settings'); }, inspectLaunchMode: async () => ({ steamAvailable: true }), setLaunchMode: async () => { calls.push('launch'); } };
  return { ...options, dir, exe, config, calls, service, settings, options, plans: createOperationPlans(options) };
}
test('preview never writes and Apply executes deployment, NR, SR and FG exactly once in order', async t => {
  const f = fixture(t), plan = await f.plans.preview('game', { deployment: 'external', version: 'core1', nr: { Intensity: 0.5 },
    sr: { backend: 'native', quality: 'quality', preset: 'K' }, fg: { backend: 'mfgunlock', mode: 'follow' } });
  assert.deepEqual(f.calls, []); assert.match(fs.readFileSync(f.config, 'utf8'), /Intensity=1/);
  const result = await f.plans.apply(plan.planId, { confirm: true, fingerprint: plan.fingerprint });
  assert.equal(result.applied, true); assert.deepEqual(f.calls, ['deployment', 'nr', 'sr', 'fg']); assert.equal((await f.plans.inspect('game')).pending, false);
});
test('EXE or configuration changes after preview block all writes', async t => {
  const f = fixture(t), plan = await f.plans.preview('game', { nr: { Intensity: 0.5 } });
  fs.writeFileSync(f.exe, 'different exe');
  await assert.rejects(f.plans.apply(plan.planId, { confirm: true, fingerprint: plan.fingerprint }), { code: 'OPERATION_CHANGED' }); assert.deepEqual(f.calls, []);
});
test('failed later stage leaves completed stages visible and blocks writes until owner recovery', async t => {
  const f = fixture(t); f.options.applyEnhancement = async () => { throw Object.assign(new Error('locked'), { code: 'EPERM' }); };
  f.plans = createOperationPlans(f.options);
  const plan = await f.plans.preview('game', { nr: { Intensity: 0.5 }, sr: { backend: 'native', quality: 'quality', preset: 'K' } });
  await assert.rejects(f.plans.apply(plan.planId, { confirm: true, fingerprint: plan.fingerprint }), error => error.details.completedStages.includes('nr'));
  await assert.rejects(f.plans.preview('game', { launchMode: 'exe' }), { code: 'OPERATION_RECOVERY_REQUIRED' });
  const recovered = await f.plans.recover('game'); assert.equal(recovered.recovered, true); assert.match(recovered.notice, /已完成的设置仍保留/);
  assert.deepEqual(JSON.parse(fs.readFileSync(f.config)), { Intensity: 0.5 });
});
test('each uninstall requires an explicit clean or restore choice and bound fingerprint', async t => {
  const f = fixture(t), clean = await f.plans.preview('game', { uninstall: 'clean' });
  await assert.rejects(f.plans.apply(clean.planId, { confirm: true }), { code: 'OPERATION_CONFIRM' });
  await f.plans.apply(clean.planId, { confirm: true, fingerprint: clean.fingerprint }); assert.deepEqual(f.calls, ['restore-launch-settings', 'clean']);
  await assert.rejects(f.plans.preview('game', { uninstall: true }), { code: 'OPERATION_INPUT' });
});
test('persisted plans are rebuilt from requests rather than trusting stored executable steps', async t => {
  const f = fixture(t), plan = await f.plans.preview('game', { launchMode: 'exe' });
  const file = path.join(f.options.userData, 'operation-plans', `${plan.planId}.preview.json`), edited = JSON.parse(fs.readFileSync(file));
  edited.steps = [{ kind: 'uninstall' }]; fs.writeFileSync(file, JSON.stringify(edited));
  const independent = createOperationPlans(f.options); await independent.apply(plan.planId, { confirm: true, fingerprint: plan.fingerprint });
  assert.deepEqual(f.calls, ['launch']);
});
