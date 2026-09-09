'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const fsp = require('fs/promises');
const os = require('os');
const path = require('path');
const { createLaunchSettingsService } = require('../src/product/launch-settings-service');
const policy = require('../src/product/launch-settings-policy');
const { DRS } = require('../src/product/launch-profile-plan');
const { enhancementEvidence } = require('./helpers/enhancement-evidence');

const absent = () => ({ kind: 'absent', value: null, location: null, predefined: null });
const explicit = value => ({ kind: 'explicit', value, location: 0, predefined: false });
const clone = structuredClone;
function fakeDriver() {
  let current = { profile: null, settings: Object.fromEntries([...policy.IDS.sr, ...policy.IDS.fg].map(id => [id, absent()])) };
  let failAfterWrite = null;
  return {
    read: async () => clone(current), peek: () => clone(current), set: value => { current = clone(value); },
    failOnce(afterWrite = () => {}) { failAfterWrite = afterWrite; },
    async write(exe, expected, desired) {
      assert.deepEqual(current, expected, 'full driver CAS'); current = clone(desired);
      if (!current.profile && Object.values(current.settings).some(value => value.kind === 'explicit'))
        current.profile = { name: 'owned fixture', appName: exe, exclusive: true, owned: true };
      if (failAfterWrite) { const afterWrite = failAfterWrite; failAfterWrite = null; afterWrite(current); throw Object.assign(new Error('lost response'), { code: 'NVAPI_READBACK_MISMATCH' }); }
      return clone(current);
    }
  };
}
function fixture(t, options = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-launch-settings-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const game = path.join(root, 'game'), exeDir = path.join(game, 'Binaries', 'Win64'), exe = path.join(exeDir, 'Game.exe');
  fs.mkdirSync(exeDir, { recursive: true }); fs.writeFileSync(exe, 'synthetic');
  const driver = fakeDriver(), legacy = options.legacy || { migrationInfo: async () => ({ baselineCaptured: false }) };
  const service = createLaunchSettingsService({ userData: path.join(root, 'user'), appDir: root,
    // Historical fixtures exercise protocol-11 receipt creation/restoration.
    // Production omits this escape hatch and refuses new legacy requests.
    allowLegacyControl: true,
    getFeatureEvidence: async () => enhancementEvidence(),
    gameDirectory: () => game, gameExecutable: () => exe, driver, legacySrModel: legacy,
    detectHardware: async () => ({ family: options.family || 'RTX50', series: [options.family || 'RTX50'] }),
    environment: async () => ({ verified: true, running: [] }), peBitness: () => 64, ...(options.service || {}) });
  return { root, game, exeDir, exe, driver, service };
}

test('generic launch recovery cannot bypass REFramework ownership checks', async t => {
  const f = fixture(t), pending = path.join(f.game, '_DLSS5_Backup/pending-switch.json');
  fs.mkdirSync(path.dirname(pending), { recursive: true });
  const bytes = JSON.stringify({ files: [{ rel: '_DLSS5_Backup/reframework-preparation.json' }] });
  fs.writeFileSync(pending, bytes);
  const before = f.driver.peek();
  await assert.rejects(f.service.recover('g'), { code: 'REF_RECOVERY_REQUIRED' });
  assert.equal(fs.readFileSync(pending, 'utf8'), bytes); assert.deepEqual(f.driver.peek(), before);
});

test('missing evidence cannot write, and confirmation persists only for this EXE content and domain', async t => {
  const evidence = enhancementEvidence({ gameSetting: { state: 'unknown', source: null } });
  const f = fixture(t, { service: { getFeatureEvidence: async () => evidence } });
  const request = { backend: 'native', quality: 'quality', preset: 'K' };
  let plan = await f.service.preview('g', 'sr', request);
  assert.equal(plan.eligibility.state, 'waiting-confirmation');
  await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_BLOCKED' });
  const confirmed = await f.service.confirmGameFeature('g', 'sr', { enabled: true });
  assert.equal(confirmed.eligible, true); assert.equal(confirmed.runtimeVerified, false);
  assert.equal((await f.service.assessEligibility('g', 'fg')).eligible, false);
  plan = await f.service.preview('g', 'sr', request); await f.service.apply(plan.id, { confirm: true });
  fs.writeFileSync(f.exe, 'updated executable');
  assert.equal((await f.service.inspectGameFeatureConfirmation('g', 'sr')).confirmation, null);
  assert.equal((await f.service.assessEligibility('g', 'sr')).eligible, false);
});

test('observed game-off revokes confirmation and cannot be overridden by confirming again', async t => {
  let gameSetting = { state: 'unknown' };
  const f = fixture(t, { service: { getFeatureEvidence: async () => enhancementEvidence({ gameSetting }) } });
  await f.service.confirmGameFeature('g', 'sr', { enabled: true });
  gameSetting = { state: 'off', source: 'game-config' };
  await assert.rejects(f.service.confirmGameFeature('g', 'sr', { enabled: true }), { code: 'SETTINGS_CONFIRMATION_BLOCKED' });
  assert.equal((await f.service.inspectGameFeatureConfirmation('g', 'sr')).confirmation, null);
  gameSetting = { state: 'unknown' };
  assert.equal((await f.service.assessEligibility('g', 'sr')).state, 'waiting-confirmation');
});

test('game switch recheck between preview and apply blocks every write', async t => {
  let gameSetting = { state: 'on', source: 'game-config' };
  const f = fixture(t, { service: { getFeatureEvidence: async () => enhancementEvidence({ gameSetting }) } });
  const plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'quality', preset: 'K' });
  const before = f.driver.peek(); gameSetting = { state: 'off', source: 'game-config' };
  await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_BLOCKED' });
  assert.deepEqual(f.driver.peek(), before); assert.deepEqual(await f.service.pending('g'), []);
});

test('external NVIDIA change requires explicit reapply and launch performs no driver write', async t => {
  const f = fixture(t), request = { backend: 'native', quality: 'performance', preset: 'auto' };
  await f.service.apply((await f.service.preview('g', 'sr', request)).id, { confirm: true }); await f.service.save('g', 'sr', request);
  const changed = f.driver.peek(); changed.settings[DRS.srPreset] = explicit(11); f.driver.set(changed);
  let writes = 0; f.driver.write = async () => { writes++; throw Error('launch must not write'); };
  assert.equal((await f.service.beforeLaunch('g'))[0].code, 'SETTINGS_REQUIRE_REAPPLY');
  assert.equal(writes, 0); assert.deepEqual(f.driver.peek(), changed);
});

test('component-preparation preview shows real operations but cannot itself be applied', async t => {
  let ready = false;
  const f = fixture(t, { family: 'RTX40', service: { assertComponents: async () => { if (!ready) throw Object.assign(Error('prepare addon'), { code: 'SETTINGS_COMPONENTS_NOT_READY' }); } } });
  const request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 };
  await assert.rejects(f.service.preview('g', 'fg', request), { code: 'SETTINGS_COMPONENTS_NOT_READY' });
  const plan = await f.service.preview('g', 'fg', request, { allowComponentPreparation: true });
  assert.equal(plan.preparation.required, true); assert.equal(plan.eligibility.eligible, true); assert.ok(plan.operations.length);
  assert.equal(fs.existsSync(path.join(f.exeDir, 'ReShade.ini')), false);
  ready = true; await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_PREPARATION_REQUIRED' });
  const fresh = await f.service.preview('g', 'fg', request); await f.service.apply(fresh.id, { confirm: true });
  assert.match(fs.readFileSync(path.join(f.exeDir, 'ReShade.ini'), 'utf8'), /ForceMultiplier=3/);
});

test('recommended model refuses unknown in-game quality when only the model is requested', async t => {
  const f = fixture(t);
  await assert.rejects(f.service.preview('g', 'sr', { backend: 'native', quality: 'preserve', preset: 'auto' }), { code: 'SETTINGS_PRESET_UNKNOWN' });
});

test('explicit driver reapply rebases only external keys and never accepts a changed profile association', async t => {
  const f = fixture(t), request = { backend: 'native', quality: 'performance', preset: 'M' };
  await f.service.apply((await f.service.preview('g', 'sr', request)).id, { confirm: true });
  const changed = f.driver.peek(); changed.settings[DRS.srPreset] = explicit(11); f.driver.set(changed);
  await assert.rejects(f.service.preview('g', 'sr', request), { code: 'SETTINGS_EXTERNAL_CHANGE' });
  const plan = await f.service.preview('g', 'sr', request, { reapplyExternalChanges: true });
  assert.equal(plan.externalChanges.length, 1); assert.equal(plan.externalChanges[0].current.value, 11);
  await f.service.apply(plan.id, { confirm: true }); await f.service.restore('g', 'sr');
  assert.equal(f.driver.peek().settings[DRS.srPreset].value, 11);
  assert.equal(f.driver.peek().settings[DRS.srMode].kind, 'absent');
  await f.service.apply((await f.service.preview('g', 'sr', request)).id, { confirm: true });
  const other = f.driver.peek(); other.profile.name = 'different profile'; f.driver.set(other);
  await assert.rejects(f.service.preview('g', 'sr', request, { reapplyExternalChanges: true }), { code: 'SETTINGS_EXTERNAL_CHANGE' });
});

test('explicit no-change Apply records existing driver values without rewriting them or inventing runtime proof', async t => {
  const f = fixture(t), request = { backend: 'native', quality: 'quality', preset: 'K' };
  const existing = f.driver.peek(); existing.profile = { name: 'External game profile', appName: f.exe, exclusive: true, owned: false };
  for (const op of policy.nativeSr(request, { series: ['RTX50'] }).operations) existing.settings[op.id] = explicit(op.value);
  f.driver.set(existing); let writes = 0; const write = f.driver.write; f.driver.write = async (...args) => { writes++; return write(...args); };
  const plan = await f.service.preview('g', 'sr', request); assert.equal(plan.recordOnly, true);
  const result = await f.service.apply(plan.id, { confirm: true }); await f.service.save('g', 'sr', request);
  assert.equal(result.configurationUnchanged, true); assert.equal(result.runtimeVerified, false); assert.equal(writes, 0);
  assert.equal((await f.service.beforeLaunch('g'))[0].noOp, true);
  await f.service.restore('g', 'sr'); assert.deepEqual(f.driver.peek(), existing);
});

test('explicit no-change MFG Apply adopts the current key baseline and still detects external drift', async t => {
  const f = fixture(t, { family: 'RTX40' }), file = path.join(f.exeDir, 'ReShade.ini'), request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 };
  const original = '[RenoDX.MFGUnlock]\nEnabled=1\nForceMultiplier=3\n'; fs.writeFileSync(file, original);
  const plan = await f.service.preview('g', 'fg', request); assert.equal(plan.recordOnly, true);
  await f.service.apply(plan.id, { confirm: true }); await f.service.save('g', 'fg', request);
  assert.equal((await f.service.beforeLaunch('g'))[0].noOp, true); assert.equal(fs.readFileSync(file, 'utf8'), original);
  fs.writeFileSync(file, original.replace('Multiplier=3', 'Multiplier=4'));
  assert.equal((await f.service.beforeLaunch('g'))[0].code, 'SETTINGS_REQUIRE_REAPPLY');
});

test('explicit MFG reapply preserves external values as the new restore baseline', async t => {
  const f = fixture(t, { family: 'RTX40' }), file = path.join(f.exeDir, 'ReShade.ini'), request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 };
  await f.service.apply((await f.service.preview('g', 'fg', request)).id, { confirm: true });
  fs.writeFileSync(file, fs.readFileSync(file, 'utf8').replace('ForceMultiplier=3', 'ForceMultiplier=4'));
  const plan = await f.service.preview('g', 'fg', request, { reapplyExternalChanges: true });
  assert.equal(plan.externalChanges[0].key, 'ForceMultiplier');
  await f.service.apply(plan.id, { confirm: true }); await f.service.restore('g', 'fg');
  assert.match(fs.readFileSync(file, 'utf8'), /ForceMultiplier=4/);
});

function externalFixture(t, extraService = {}) {
  let f;
  const location = { verified: true, mode: 'external' };
  f = fixture(t, { family: 'RTX40', service: { ...extraService, getLayout: () => location } });
  const active = path.join(f.root, 'external', 'active'); fs.mkdirSync(active, { recursive: true });
  Object.assign(location, { exe: f.exe, activeConfigPath: path.join(active, 'ReShade.ini'), reshadeConfigDir: active });
  return { ...f, active, location, pending: path.join(f.game, '_DLSS5_Backup/launch-external-config-pending.json') };
}

test('external MFG config apply and restore use the verified active INI and preserve local and NR settings', async t => {
  const f = externalFixture(t), local = path.join(f.exeDir, 'ReShade.ini'), ini = f.location.activeConfigPath;
  fs.writeFileSync(local, 'local peer config'); fs.writeFileSync(ini, '[RenoDX]\nStrength=0.5\n');
  const request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 };
  const plan = await f.service.preview('g', 'fg', request); await f.service.apply(plan.id, { confirm: true });
  assert.match(fs.readFileSync(ini, 'utf8'), /ForceMultiplier=3/);
  assert.equal((await f.service.inspect('g')).applied.fg.readbackVerified, true);
  assert.equal(fs.existsSync(f.pending), false);
  fs.appendFileSync(ini, '[HDR]\nEnabled=1\n');
  await f.service.restore('g', 'fg');
  assert.match(fs.readFileSync(ini, 'utf8'), /Strength=0.5/); assert.match(fs.readFileSync(ini, 'utf8'), /\[HDR\]/);
  assert.doesNotMatch(fs.readFileSync(ini, 'utf8'), /ForceMultiplier/);
  assert.equal(fs.readFileSync(local, 'utf8'), 'local peer config');
});

test('external config identity change after preview blocks writes before pending creation', async t => {
  const f = externalFixture(t), request = { backend: 'mfgunlock', mode: 'follow' };
  const plan = await f.service.preview('g', 'fg', request);
  const other = path.join(f.root, 'other-active'); fs.mkdirSync(other);
  f.location.activeConfigPath = path.join(other, 'ReShade.ini'); f.location.reshadeConfigDir = other;
  await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_LAYOUT_CHANGED' });
  assert.equal(fs.existsSync(f.pending), false); assert.equal(fs.existsSync(f.location.activeConfigPath), false);
});

test('external-config recovery rejects foreign owners and preserves an external writer after interruption', async t => {
  for (const altered of ['none', 'owner', 'external']) {
    const f = externalFixture(t), ini = f.location.activeConfigPath, beforeText = '[NR]\nValue=old\n', after = '[NR]\nValue=new\n';
    fs.writeFileSync(ini, altered === 'external' ? 'external replacement' : after);
    fs.mkdirSync(path.dirname(f.pending), { recursive: true });
    const row = { version: 1, owner: altered === 'owner' ? 'external-core-migration' : 'launch-mfg-config',
      transactionId: '22222222-2222-2222-2222-222222222222', domain: 'fg', restoring: false, exe: f.exe,
      configFile: ini, beforeText, beforeHash: policy.hash(beforeText), afterHash: policy.hash(after) };
    fs.writeFileSync(f.pending, JSON.stringify(row));
    if (altered === 'none') { await f.service.recover('g'); assert.equal(fs.readFileSync(ini, 'utf8'), beforeText); assert.equal(fs.existsSync(f.pending), false); }
    else {
      await assert.rejects(f.service.recover('g'), { code: altered === 'owner' ? 'SETTINGS_RECEIPT_INVALID' : 'SETTINGS_EXTERNAL_CHANGE' });
      assert.equal(fs.readFileSync(ini, 'utf8'), altered === 'external' ? 'external replacement' : after); assert.equal(fs.existsSync(f.pending), true);
    }
  }
});

function seedExternalRecovery(f, before, after) {
  fs.writeFileSync(f.location.activeConfigPath, after); fs.mkdirSync(path.dirname(f.pending), { recursive: true });
  fs.writeFileSync(f.pending, JSON.stringify({ version: 1, owner: 'launch-mfg-config', transactionId: '22222222-2222-4222-8222-222222222222',
    domain: 'fg', restoring: false, exe: f.exe, configFile: f.location.activeConfigPath,
    beforeText: before, beforeHash: policy.hash(before), afterHash: policy.hash(after) }));
}
async function failExternalTemporaryWrite(f, work) {
  const originalOpen = fsp.open; let writes = 0;
  fsp.open = async (file, flags, ...args) => {
    const handle = await originalOpen(file, flags, ...args);
    if (flags === 'wx' && path.dirname(path.resolve(file)) === f.active && path.resolve(file) !== f.location.activeConfigPath) {
      const write = handle.writeFile.bind(handle);
      handle.writeFile = async content => { writes++; await write(Buffer.from(content).subarray(0, 8)); throw Object.assign(new Error('interrupted after eight staged bytes'), { code: 'EIO' }); };
    }
    return handle;
  };
  try { await work(); } finally { fsp.open = originalOpen; }
  assert.equal(writes, 1, 'the fixture must actually interrupt the temporary configuration write');
  assert.equal(fs.readdirSync(f.active).some(name => name.endsWith('.tmp')), false, 'only this failed temporary file is reclaimed');
}

test('external MFG partial temporary write preserves the complete active INI and leaves recovery usable', async t => {
  const f = externalFixture(t), ini = f.location.activeConfigPath, before = '[HDR]\nEnabled=1\n[RenoDX]\nStrength=0.5\n';
  fs.writeFileSync(ini, before); const plan = await f.service.preview('g', 'fg', { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 });
  await failExternalTemporaryWrite(f, () => assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'EIO' }));
  assert.equal(fs.readFileSync(ini, 'utf8'), before); assert.equal(fs.existsSync(f.pending), false);
  await f.service.recover('g'); assert.equal(fs.readFileSync(ini, 'utf8'), before); await f.service.assertReady('g');
});

test('external MFG rollback temporary write failure retains a complete after-image and can be retried', async t => {
  const f = externalFixture(t), ini = f.location.activeConfigPath, before = '[HDR]\nEnabled=1\n', after = before + '[MFGUnlock]\nForceMultiplier=3\n';
  seedExternalRecovery(f, before, after); const pendingBefore = fs.readFileSync(f.pending);
  await failExternalTemporaryWrite(f, () => assert.rejects(f.service.recover('g'), { code: 'EIO' }));
  assert.equal(fs.readFileSync(ini, 'utf8'), after); assert.deepEqual(fs.readFileSync(f.pending), pendingBefore);
  assert.equal((await f.service.recover('g')).externalRestored, true); assert.equal(fs.readFileSync(ini, 'utf8'), before);
  assert.equal(fs.existsSync(f.pending), false); await f.service.assertReady('g');
});

test('external edits during the final process check are preserved before both apply and rollback publication', async t => {
  for (const mode of ['apply', 'rollback']) {
    let f, changed = false;
    const external = '[HDR]\nEnabled=1\nExternalChoice=keep\n';
    f = externalFixture(t, { environment: async () => {
      if (!changed && fs.readdirSync(f.active).some(name => name.endsWith('.tmp'))) { fs.writeFileSync(f.location.activeConfigPath, external); changed = true; }
      return { verified: true, running: [] };
    } });
    const before = '[HDR]\nEnabled=1\n', after = before + '[MFGUnlock]\nForceMultiplier=3\n';
    if (mode === 'apply') {
      fs.writeFileSync(f.location.activeConfigPath, before);
      const plan = await f.service.preview('g', 'fg', { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 });
      await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_RECOVERY_FIRST' });
    } else {
      seedExternalRecovery(f, before, after);
      await assert.rejects(f.service.recover('g'), { code: 'SETTINGS_EXTERNAL_CHANGE' });
    }
    assert.equal(changed, true); assert.equal(fs.readFileSync(f.location.activeConfigPath, 'utf8'), external);
    assert.equal(fs.existsSync(f.pending), true); assert.equal(fs.readdirSync(f.active).some(name => name.endsWith('.tmp')), false);
  }
});

test('new MFG config is scoped to ReShade.ini, restores missing files, and refuses stale/legacy requests', async t => {
  const checked = [];
  const f = fixture(t, { family: 'RTX40', service: { allowLegacyControl: false, assertComponents: async (id, backend) => checked.push([id, backend]) } });
  const file = path.join(f.exeDir, 'ReShade.ini'), originalDriver = f.driver.peek();
  const request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 4 };
  const preview = await f.service.preview('g', 'fg', request);
  await f.service.apply(preview.id, { confirm: true }); await f.service.save('g', 'fg', request);
  assert.match(fs.readFileSync(file, 'utf8'), /ForceMultiplier=4/);
  assert.equal(fs.existsSync(path.join(f.exeDir, 'RTX40MFG-Universal.json')), false);
  assert.equal((await f.service.inspect('g')).applied.fg.readbackVerified, true);
  assert.equal((await f.service.inspect('g')).runtimeVerified, false);
  assert.deepEqual(f.driver.peek(), originalDriver);
  assert.deepEqual(checked, [['g', 'mfgunlock'], ['g', 'mfgunlock']]);
  await f.service.restore('g', 'fg'); assert.equal(fs.existsSync(file), false);
  await assert.rejects(f.service.preview('g', 'fg', { backend: 'rtx40', mode: 'dynamic', targetFps: 120 }), { code: 'SETTINGS_FG_MIGRATION_REQUIRED' });
  await assert.rejects(f.service.save('g', 'fg', { backend: 'rtx40', mode: 'follow' }), { code: 'SETTINGS_FG_MIGRATION_REQUIRED' });
  assert.deepEqual(await f.service.pending('g'), []);
});

test('MFG restoration preserves later ReShade settings, and preview/apply catches a file appearing in between', async t => {
  const f = fixture(t, { family: 'RTX40' }), file = path.join(f.exeDir, 'ReShade.ini');
  const request = { backend: 'mfgunlock', mode: 'follow' };
  let preview = await f.service.preview('g', 'fg', request);
  fs.writeFileSync(file, '[GENERAL]\nPresetPath=player.ini\n');
  await assert.rejects(f.service.apply(preview.id, { confirm: true }), { code: 'FILE_CHANGED' });
  assert.equal(fs.readFileSync(file, 'utf8'), '[GENERAL]\nPresetPath=player.ini\n');
  preview = await f.service.preview('g', 'fg', request); await f.service.apply(preview.id, { confirm: true });
  fs.appendFileSync(file, '[INPUT]\nKeyOverlay=36\n');
  await f.service.restore('g', 'fg');
  assert.match(fs.readFileSync(file, 'utf8'), /PresetPath=player.ini/);
  assert.match(fs.readFileSync(file, 'utf8'), /KeyOverlay=36/);
  assert.doesNotMatch(fs.readFileSync(file, 'utf8'), /ForceMultiplier|RenoDX.MFGUnlock/);
});

test('native SR owns four keys, leaves FG isolated and restores through the shared file journal', async t => {
  const f = fixture(t); const request = { backend: 'native', quality: 'custom', renderPercent: 75, preset: 'M' };
  await f.service.save('g', 'sr', request); assert.equal(await f.service.hasSrRequest('g'), true);
  const preview = await f.service.preview('g', 'sr'); assert.deepEqual(new Set(preview.driverSettings), new Set(policy.IDS.sr));
  await f.service.apply(preview.id, { confirm: true });
  assert.equal(f.driver.peek().settings[DRS.srRatio].value, 75);
  for (const id of policy.IDS.fg) assert.deepEqual(f.driver.peek().settings[id], absent());
  const state = await f.service.inspect('g'); assert.equal(state.applied.sr.readbackVerified, true); assert.equal(state.runtimeVerified, false);
  await f.service.restore('g', 'sr'); assert.equal(f.driver.peek().profile, null); assert.deepEqual((await f.service.inspect('g')).applied, {});
  assert.equal(await f.service.hasSrRequest('g'), true); assert.equal((await f.service.inspect('g')).requests.sr, undefined);
  assert.deepEqual(await f.service.pending('g'), []);
});

test('recommended SR model follows the selected quality and launch never applies saved drafts', async t => {
  for (const [quality,preset] of [['quality',11],['balanced',11],['performance',13],['ultraPerformance',12],['dlaa',11]]) {
    const f=fixture(t,{family:'RTX40'}), request={backend:'native',quality,preset:'auto'};
    await f.service.save('g','sr',request);
    const result=await f.service.beforeLaunch('g'); assert.equal(result[0].code,'SETTINGS_REQUIRE_APPLY');
    await f.service.apply((await f.service.preview('g','sr',request)).id,{confirm:true});
    assert.equal((await f.service.beforeLaunch('g'))[0].noOp,true);
    assert.equal(f.driver.peek().settings[DRS.srPreset].value,preset);
    assert.equal((await f.service.inspect('g')).requests.sr.request.preset,'auto');
    await f.service.restore('g','sr'); assert.equal(f.driver.peek().profile,null);
  }
  const f=fixture(t,{service:{detectHardware:async()=>({family:'mixed',series:['RTX30','RTX50']})}});
  await assert.rejects(f.service.preview('g','sr',{backend:'native',quality:'quality',preset:'auto'}),{code:'SETTINGS_GPU_UNKNOWN'});
  assert.deepEqual(await f.service.pending('g'),[]);
});

test('shared predefined profile refusal occurs before any pending record or driver write', async t => {
  const f=fixture(t), before=f.driver.peek(); before.profile={name:'Shared game family',appName:'game/game.exe',exclusive:false,owned:false}; f.driver.set(before);
  let writes=0; f.driver.write=async()=>{writes++;throw Error('must not write');};
  const plan=await f.service.preview('g','sr',{backend:'native',quality:'quality',preset:'M'});
  await assert.rejects(f.service.apply(plan.id,{confirm:true,automatic:true}),{code:'SETTINGS_BLOCKED'});
  assert.equal(writes,0); assert.deepEqual(await f.service.pending('g'),[]); assert.equal(await f.service.hasSrRequest('g'),false);
  assert.deepEqual(f.driver.peek(),before);
});

test('verified official game profile applies and restores SR/FG without changing shared associations', async t => {
  const f=fixture(t), before=f.driver.peek(), appName='game/Binaries/Win64/Game.exe';
  before.profile={name:'Official game',appName,exclusive:false,owned:false,scope:{predefined:true,applications:[appName,'GameDX11.exe'],fingerprint:'a'.repeat(64)}};
  f.driver.set(before);
  for(const [domain,request] of [['sr',{backend:'native',quality:'quality',preset:'auto'}],['fg',{backend:'nvidia',mode:'fixed',multiplier:4}]]) {
    const plan=await f.service.preview('g',domain,request);assert.deepEqual(plan.blockers,[]);await f.service.apply(plan.id,{confirm:true,automatic:true});
  }
  assert.deepEqual(f.driver.peek().profile,before.profile);
  assert.equal((await f.service.inspect('g')).driverScope.applications.length,2);
  assert.equal((await f.service.inspect('g')).applied.sr.readbackVerified,true);
  await f.service.restore('g','fg'); await f.service.restore('g','sr');assert.deepEqual(f.driver.peek(),before);assert.deepEqual(await f.service.pending('g'),[]);
});

test('official game profile association changes refuse restore and retain the receipt', async t => {
  const f=fixture(t), before=f.driver.peek(), appName='game/Binaries/Win64/Game.exe';
  before.profile={name:'Official game',appName,exclusive:false,owned:false,scope:{predefined:true,applications:[appName,'other.exe'],fingerprint:'a'.repeat(64)}};f.driver.set(before);
  const plan=await f.service.preview('g','sr',{backend:'native',quality:'quality',preset:'M'});await f.service.apply(plan.id,{confirm:true});
  const changed=f.driver.peek();changed.profile.scope.fingerprint='b'.repeat(64);f.driver.set(changed);
  await assert.rejects(f.service.restore('g','sr'),{code:'SETTINGS_EXTERNAL_CHANGE'});assert.equal(fs.existsSync(f.service.receiptFile('g')),true);assert.deepEqual(f.driver.peek(),changed);
});

test('dev8 refused shared-profile pending clears only after full unchanged-state verification', async t => {
  for(const changed of [false,'setting','profile']) {
    const f=fixture(t), before=f.driver.peek(); before.profile={name:'Shared game',appName:'game/game.exe',exclusive:false,owned:false}; f.driver.set(before);
    const desired=clone(before); desired.settings[DRS.srPreset]=explicit(13);
    const pending=f.service.driverPendingFile('g'); fs.mkdirSync(path.dirname(pending),{recursive:true});
    fs.writeFileSync(pending,JSON.stringify({version:1,transactionId:'22222222-2222-2222-2222-222222222222',domain:'sr',restoring:false,exe:f.exe,ids:[DRS.srPreset],before,desired,after:null}));
    let writes=0;f.driver.write=async()=>{writes++;throw Error('never write a shared profile');};
    if(changed){const current=clone(before);if(changed==='setting')current.settings[DRS.fgCount]=explicit(6);else current.profile.name='Other profile';f.driver.set(current);
      await assert.rejects(f.service.recover('g'),{code:'SETTINGS_EXTERNAL_CHANGE'});assert.equal(fs.existsSync(pending),true);
    }else{assert.equal((await f.service.recover('g')).driverRestored,true);assert.deepEqual(await f.service.pending('g'),[]);}
    assert.equal(writes,0);
  }
});

test('old SR baseline is restored before the new full four-key baseline is captured', async t => {
  let captured = true, migrations = 0, f;
  const legacy = { migrationInfo: async () => ({ baselineCaptured: captured, selection: 'm', effective: 'm' }),
    prepareMigration: async () => { migrations++; captured = false; const state = f.driver.peek(); state.settings[DRS.srPreset] = explicit(11); f.driver.set(state); return { ok: true, restored: true }; } };
  f = fixture(t, { legacy });
  const plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'preserve', preset: 'M' });
  await f.service.apply(plan.id, { confirm: true }); assert.equal(migrations, 1);
  const applied = (await f.service.inspect('g')).applied.sr;
  assert.equal(applied.baseline.settings[DRS.srPreset].value, 11);
  assert.equal(f.driver.peek().settings[DRS.srPreset].value, 13);
});

test('handing SR back to the game first migrates an old baseline and keeps legacy writer disabled', async t => {
  let captured = true, migrations = 0, f;
  const legacy = { migrationInfo: async () => ({ baselineCaptured: captured, selection: 'auto', effective: 'm' }),
    prepareMigration: async () => { migrations++; captured = false; const state = f.driver.peek(); state.settings[DRS.srPreset] = explicit(11); f.driver.set(state); return { ok: true, restored: true }; } };
  f = fixture(t, { legacy });
  const result = await f.service.restore('g', 'sr'); assert.equal(result.noOp, true); assert.equal(migrations, 1);
  assert.equal(f.driver.peek().settings[DRS.srPreset].value, 11); assert.equal(await f.service.hasSrRequest('g'), true);
  const state = await f.service.inspect('g'); assert.equal(state.legacy.managed, true); assert.equal(state.legacy.effective, 'm'); assert.deepEqual(state.applied, {});
});

test('uncertain driver failure restores the before snapshot and clears both pending gates', async t => {
  const f = fixture(t); const plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'quality', preset: 'K' });
  f.driver.failOnce(current => { current.settings[DRS.fgCount] = explicit(99); });
  await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'NVAPI_READBACK_MISMATCH' });
  assert.equal(f.driver.peek().settings[DRS.srPreset].kind, 'absent'); assert.equal(f.driver.peek().settings[DRS.fgCount].value, 99);
  assert.ok(f.driver.peek().profile, 'unowned domain value keeps the profile'); assert.deepEqual(await f.service.pending('g'), []);
  assert.equal(fs.existsSync(f.service.receiptFile('g')), false);
});

test('RTX40 control v11 requires manual review for latent debug and restores only owned keys', async t => {
  const f = fixture(t, { family: 'RTX40' }), file = path.join(f.exeDir, policy.FILES.rtx40);
  const original = '{"version":11,"followGame":true,"mode":"follow","multiplier":2,"generatedOnlyDebug":true,"other":7}\n';
  fs.writeFileSync(file, original); await f.service.save('g', 'fg', { backend: 'rtx40', mode: 'fixed', multiplier: 3 });
  const auto = await f.service.beforeLaunch('g'); assert.equal(auto[0].code, 'SETTINGS_REQUIRE_APPLY'); assert.equal(fs.readFileSync(file, 'utf8'), original);
  const plan = await f.service.preview('g', 'fg'); assert.equal(plan.requiresReview, true); await f.service.apply(plan.id, { confirm: true });
  const changed = JSON.parse(fs.readFileSync(file, 'utf8')); assert.equal(changed.multiplier, 3); assert.equal(changed.other, 7);
  await f.service.restore('g', 'fg'); assert.deepEqual(JSON.parse(fs.readFileSync(file, 'utf8')), JSON.parse(original));
});

test('no-op driver apply revalidates settings and profile after preview without overwriting external changes', async t => {
  for (const changedField of ['setting', 'profile']) {
    const f = fixture(t), request = { backend: 'native', quality: 'quality', preset: 'M' };
    await f.service.apply((await f.service.preview('g', 'sr', request)).id, { confirm: true });
    const receiptBefore = fs.readFileSync(f.service.receiptFile('g'), 'utf8');
    const plan = await f.service.preview('g', 'sr', request);
    assert.equal(plan.noOp, true);
    const external = f.driver.peek();
    if (changedField === 'setting') external.settings[DRS.srPreset] = explicit(11);
    else external.profile = { ...external.profile, name: 'replacement profile', owned: false };
    f.driver.set(external);
    await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_EXTERNAL_CHANGE' });
    assert.deepEqual(f.driver.peek(), external);
    assert.equal(fs.readFileSync(f.service.receiptFile('g'), 'utf8'), receiptBefore);
    assert.deepEqual(await f.service.pending('g'), []);
  }
});

test('no-op RTX40 file apply revalidates the control file after preview', async t => {
  const f = fixture(t, { family: 'RTX40' }), file = path.join(f.exeDir, policy.FILES.rtx40);
  fs.writeFileSync(file, JSON.stringify({ version: 11, followGame: true, mode: 'follow', multiplier: 2 }));
  const request = { backend: 'rtx40', mode: 'fixed', multiplier: 3 };
  await f.service.apply((await f.service.preview('g', 'fg', request)).id, { confirm: true });
  const receiptBefore = fs.readFileSync(f.service.receiptFile('g'), 'utf8');
  const plan = await f.service.preview('g', 'fg', request);
  assert.equal(plan.noOp, true);
  const external = JSON.parse(fs.readFileSync(file, 'utf8')); external.multiplier = 4;
  fs.writeFileSync(file, JSON.stringify(external));
  const externalText = fs.readFileSync(file, 'utf8');
  await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'FILE_CHANGED' });
  assert.equal(fs.readFileSync(file, 'utf8'), externalText);
  assert.equal(fs.readFileSync(f.service.receiptFile('g'), 'utf8'), receiptBefore);
  assert.deepEqual(await f.service.pending('g'), []);
});

test('restore without an applied domain is a no-op and never probes a missing backend', async t => {
  const f = fixture(t, { family: 'unknown' });
  assert.deepEqual(await f.service.restore('g', 'fg'), { applied: false, skipped: true, noOp: true, runtimeVerified: false });
});
test('after restore an EXE change can use the new editor without reviving the legacy writer', async t => {
  const f = fixture(t);
  await f.service.save('g', 'sr', { backend: 'native', quality: 'quality', preset: 'M' });
  const plan = await f.service.preview('g', 'sr'); await f.service.apply(plan.id, { confirm: true });
  await f.service.restore('g', 'sr');
  const nextExe = path.join(f.exeDir, 'Other.exe'); fs.writeFileSync(nextExe, 'synthetic');
  const next = createLaunchSettingsService({ userData: path.join(f.root, 'user'), appDir: f.root,
    getFeatureEvidence: async () => enhancementEvidence(),
    gameDirectory: () => f.game, gameExecutable: () => nextExe, driver: f.driver,
    detectHardware: () => ({ family: 'RTX50', series: ['RTX50'] }), environment: async () => ({ verified: true, running: [] }), peBitness: () => 64 });
  assert.deepEqual((await next.inspect('g')).applied, {});
  assert.equal(await next.hasSrRequest('g'), true);
  assert.deepEqual(await next.beforeLaunch('g'), []);
  await next.save('g', 'sr', { backend: 'native', quality: 'balanced' });
  assert.equal((await next.apply((await next.preview('g', 'sr')).id, { confirm: true })).applied, true);
});

test('official fixed and dynamic FG share the profile without overwriting native SR', async t => {
  const f = fixture(t);
  let plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'quality', preset: 'L' });
  await f.service.apply(plan.id, { confirm: true }); const srBefore = clone(f.driver.peek().settings[DRS.srPreset]);
  plan = await f.service.preview('g', 'fg', { backend: 'nvidia', mode: 'fixed', multiplier: 4 });
  await f.service.apply(plan.id, { confirm: true }); assert.equal(f.driver.peek().settings[DRS.fgCount].value, 3);
  plan = await f.service.preview('g', 'fg', { backend: 'nvidia', mode: 'dynamic', targetFps: 120 });
  await f.service.apply(plan.id, { confirm: true }); assert.equal(f.driver.peek().settings[DRS.fgMode].value, 4); assert.equal(f.driver.peek().settings[DRS.fgTarget].value, 120);
  assert.deepEqual(f.driver.peek().settings[DRS.srPreset], srBefore);
  await f.service.restore('g', 'fg'); assert.ok(f.driver.peek().profile); assert.deepEqual(f.driver.peek().settings[DRS.srPreset], srBefore);
  await f.service.restore('g', 'sr'); assert.equal(f.driver.peek().profile, null);
});

test('OptiScaler ratio restore preserves unrelated later INI edits', async t => {
  const f = fixture(t), file = path.join(f.exeDir, policy.FILES.optiscaler);
  fs.writeFileSync(file, '[UpscaleRatio]\r\nUpscaleRatioOverrideEnabled=false\r\n[FG]\r\nEnabled=true\r\n');
  const plan = await f.service.preview('g', 'sr', { backend: 'optiscaler', quality: 'custom', renderPercent: 75 });
  await f.service.apply(plan.id, { confirm: true }); assert.match(fs.readFileSync(file, 'utf8'), /UpscaleRatioOverrideValue=1\.333333333/);
  fs.appendFileSync(file, 'UserKey=keep\r\n'); await f.service.restore('g', 'sr');
  const restored = fs.readFileSync(file, 'utf8'); assert.match(restored, /UpscaleRatioOverrideEnabled=false/); assert.doesNotMatch(restored, /UpscaleRatioOverrideValue/); assert.match(restored, /Enabled=true/); assert.match(restored, /UserKey=keep/);
});

test('newly acquired file keys restore to their latest unowned values', async t => {
  const mfg = fixture(t, { family: 'RTX40' }), mfgFile = path.join(mfg.exeDir, policy.FILES.rtx40);
  fs.writeFileSync(mfgFile, JSON.stringify({ version: 11, followGame: true, mode: 'follow', multiplier: 2, dynamicTargetFrameRate: 60, dynamicExperimental56: false }));
  let plan = await mfg.service.preview('g', 'fg', { backend: 'rtx40', mode: 'fixed', multiplier: 3 });
  await mfg.service.apply(plan.id, { confirm: true });
  const external = JSON.parse(fs.readFileSync(mfgFile, 'utf8')); external.dynamicTargetFrameRate = 144;
  fs.writeFileSync(mfgFile, JSON.stringify(external));
  plan = await mfg.service.preview('g', 'fg', { backend: 'rtx40', mode: 'dynamic', targetFps: 120 });
  await mfg.service.apply(plan.id, { confirm: true }); await mfg.service.restore('g', 'fg');
  const restored = JSON.parse(fs.readFileSync(mfgFile, 'utf8'));
  assert.equal(restored.dynamicTargetFrameRate, 144, 'target baseline starts when the target key is first acquired');
  assert.equal(restored.mode, 'follow'); assert.equal(restored.multiplier, 2, 'previously acquired keys retain their original baselines');

  const sr = fixture(t), iniFile = path.join(sr.exeDir, policy.FILES.optiscaler);
  fs.writeFileSync(iniFile, '[UpscaleRatio]\nUpscaleRatioOverrideEnabled=false\nUpscaleRatioOverrideValue=1.5\n');
  plan = await sr.service.preview('g', 'sr', { backend: 'optiscaler', quality: 'quality' });
  await sr.service.apply(plan.id, { confirm: true });
  fs.writeFileSync(iniFile, fs.readFileSync(iniFile, 'utf8').replace('Value=1.5', 'Value=1.7'));
  plan = await sr.service.preview('g', 'sr', { backend: 'optiscaler', quality: 'custom', renderPercent: 75 });
  await sr.service.apply(plan.id, { confirm: true }); await sr.service.restore('g', 'sr');
  assert.match(fs.readFileSync(iniFile, 'utf8'), /UpscaleRatioOverrideValue=1\.7/);
  assert.match(fs.readFileSync(iniFile, 'utf8'), /UpscaleRatioOverrideEnabled=false/);
});

test('replacing an exclusive driver profile cannot reuse another profile recovery baseline', async t => {
  const f = fixture(t), request = { backend: 'native', quality: 'quality', preset: 'M' };
  const plan = await f.service.preview('g', 'sr', request); await f.service.apply(plan.id, { confirm: true });
  const replacement = f.driver.peek(); replacement.profile = { ...replacement.profile, name: 'Another exclusive user profile', owned: false };
  f.driver.set(replacement);
  assert.equal((await f.service.inspect('g')).applied.sr.readbackVerified, false);
  await assert.rejects(f.service.preview('g', 'sr', request), { code: 'SETTINGS_EXTERNAL_CHANGE' }, 'even an otherwise no-op request must reject changed profile identity');
  await assert.rejects(f.service.restore('g', 'sr'), { code: 'SETTINGS_EXTERNAL_CHANGE' });
  assert.deepEqual(f.driver.peek(), replacement, 'new profile and its values remain intact');
  assert.ok((await f.service.inspect('g')).applied.sr, 'original recovery record remains intact');
});

test('damaged or target-injecting applied receipt is rejected before any write', async t => {
  const f = fixture(t), file = f.service.receiptFile('g'); fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, '{bad'); await assert.rejects(f.service.inspect('g'), { code: 'SETTINGS_RECEIPT_INVALID' });
  fs.writeFileSync(file, JSON.stringify({ version: 1, exe: f.exe, applied: { sr: { exe: f.exe, backend: 'optiscaler', request: { backend: 'optiscaler', quality: 'game' }, name: '..\\outside.ini', baselineText: '', lastValues: {} } }, driverOriginalProfile: null }));
  await assert.rejects(f.service.restore('g', 'sr'), { code: 'SETTINGS_RECEIPT_INVALID' });
});

test('inspect performs fresh readback and apply rejects an EXE changed after preview', async t => {
  const f = fixture(t); let plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'quality', preset: 'K' });
  fs.writeFileSync(f.exe, 'changed synthetic'); await assert.rejects(f.service.apply(plan.id, { confirm: true }), { code: 'SETTINGS_EXE_CHANGED' });
  plan = await f.service.preview('g', 'sr', { backend: 'native', quality: 'quality', preset: 'K' }); await f.service.apply(plan.id, { confirm: true });
  assert.equal((await f.service.inspect('g')).applied.sr.readbackVerified, true);
  const changed = f.driver.peek(); changed.settings[DRS.srPreset] = explicit(12); f.driver.set(changed);
  assert.equal((await f.service.inspect('g')).applied.sr.readbackVerified, false);
});

test('pending recovery remains visible with a corrupt current receipt and legacy inspection failure', async t => {
  let recovered = false, f;
  const journal = {
    safePath: (_root, rel) => rel,
    pendingPath: game => path.join(game, '_DLSS5_Backup', 'pending-switch.json'),
    async recover(game) { recovered = true; fs.rmSync(this.pendingPath(game), { force: true }); fs.rmSync(f.service.receiptFile('g'), { force: true }); return true; },
    async transaction() { throw new Error('not used'); }, async capture() {}
  };
  const legacy = { migrationInfo: async () => { throw Object.assign(new Error('legacy damaged'), { code: 'SETTINGS_LEGACY_MIGRATION' }); } };
  f = fixture(t, { legacy, service: { journal } });
  const pending = journal.pendingPath(f.game); fs.mkdirSync(path.dirname(pending), { recursive: true }); fs.writeFileSync(pending, '{}'); fs.writeFileSync(f.service.receiptFile('g'), '{bad');
  const state = await f.service.inspect('g'); assert.equal(state.pending.length, 1); assert.deepEqual(state.applied, {}); assert.equal(state.receiptError.code, 'SETTINGS_RECEIPT_INVALID'); assert.match(state.notice, /先恢复/);
  await f.service.recover('g'); assert.equal(recovered, true); assert.deepEqual(await f.service.pending('g'), []);
  const after = await f.service.inspect('g'); assert.equal(after.legacy.error.code, 'SETTINGS_LEGACY_MIGRATION'); assert.match(after.notice, /旧 SR/);
});

test('generic recovery refuses Feeder WAL before touching either files or the driver', async t => {
  for (const rel of ['_DLSS5_Backup/xiaofeng-feeder.json','Bin/_DLSS5_Feeder/addons/core.addon64','_DLSS5_Backup/feeder-settings/test/old.ini']) {
    const f=fixture(t),pending=path.join(f.game,'_DLSS5_Backup/pending-switch.json');
    fs.mkdirSync(path.dirname(pending),{recursive:true}); const bytes=JSON.stringify({files:[{rel}]}); fs.writeFileSync(pending,bytes);
    const before=f.driver.peek(); await assert.rejects(f.service.recover('g'),{code:'FEEDER_RECOVERY_REQUIRED'});
    assert.equal(fs.readFileSync(pending,'utf8'),bytes);assert.deepEqual(f.driver.peek(),before);
  }
});
