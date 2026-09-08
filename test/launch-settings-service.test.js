'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { createLaunchSettingsService } = require('../src/product/launch-settings-service');
const policy = require('../src/product/launch-settings-policy');
const { DRS } = require('../src/product/launch-profile-plan');

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

test('automatic SR model resolves per hardware while preserving the saved auto selection', async t => {
  for (const [family,preset] of [['RTX30',11],['RTX40',13],['RTX50',13]]) {
    const f=fixture(t,{family}), request={backend:'native',quality:'quality',preset:'auto'};
    await f.service.save('g','sr',request);
    const result=await f.service.beforeLaunch('g'); assert.equal(result[0].applied,true);
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
  const auto = await f.service.beforeLaunch('g'); assert.equal(auto[0].code, 'SETTINGS_REVIEW_REQUIRED'); assert.equal(fs.readFileSync(file, 'utf8'), original);
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
