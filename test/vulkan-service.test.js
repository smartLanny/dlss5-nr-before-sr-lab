'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { createVulkanService } = require('../src/product/vulkan-service');
const { createVulkanRuntimeProfile } = require('../src/product/vulkan-runtime-profile');
const { createReshadeVulkanActivation } = require('../src/product/reshade-vulkan-activation');
const { atomicJson } = require('../src/product/launch-safety');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-vulkan-service-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const userData = path.join(root, 'user'), appDir = path.join(root, 'app'), resourcesPath = path.join(root, 'resources');
  const runtimeRoot = path.join(resourcesPath, 'vulkan-runtime'), layerRoot = path.join(resourcesPath, 'vulkan-reshade');
  fs.mkdirSync(runtimeRoot, { recursive: true }); fs.mkdirSync(layerRoot, { recursive: true });
  const bindingPath = path.join(userData, 'vulkan-bindings.json');
  const runtime = { version: 1, id: 'nr-vulkan-e7df0fc', coreVersion: '0.4.6-hotfix.1-vulkan', sourceRevision: 'e7df0fc', architecture: 64,
    acceptance: { status: 'processed', hardwareFamily: 'RTX50' }, files: [
      { source: 'core.addon64', target: 'core.addon64', sha256: hash('pe64-core'), mutable: false },
      { source: 'nr_before_sr.ini', target: 'nr_before_sr.ini', sha256: hash('[NRBeforeSR]\nEnabled=1\n'), mutable: true }
    ] };
  fs.writeFileSync(path.join(runtimeRoot, 'core.addon64'), 'pe64-core');
  fs.writeFileSync(path.join(runtimeRoot, 'nr_before_sr.ini'), '[NRBeforeSR]\nEnabled=1\n');
  const manifest = JSON.stringify({ file_format_version: '1.2.0', layer: { name: 'VK_LAYER_reshade', type: 'GLOBAL', library_path: '.\\ReShade64.dll', disable_environment: { DISABLE_RESHADER: '1' } } });
  fs.writeFileSync(path.join(layerRoot, 'ReShade64.dll'), 'pe64-layer'); fs.writeFileSync(path.join(layerRoot, 'ReShade64.json'), manifest);
  const layer = { version: 1, id: 'reshade-vulkan-6.8', release: '6.8.0', architecture: 64,
    layer: { manifest: 'ReShade64.json', library: 'ReShade64.dll', manifestSha256: hash(manifest), librarySha256: hash('pe64-layer'), name: 'VK_LAYER_reshade' },
    activation: { interface: 'reshade-ini-v1' } };
  const writeRecipes = () => { fs.writeFileSync(path.join(runtimeRoot, 'recipe.json'), JSON.stringify(runtime)); fs.writeFileSync(path.join(layerRoot, 'recipe.json'), JSON.stringify(layer)); };
  writeRecipes();
  const hardware = { family: 'RTX50', families: ['RTX50'], series: ['RTX50'] }, calls = [], values = new Map();
  let antiCheat = false, running = false, activationFailure = false, archiveFailure = false, bindingClearFailure = false, archivedSaveFailure = false, firstBindingFailure = false;
  const pe = { getBitness: file => fs.readFileSync(file, 'utf8').startsWith('pe64') ? 64 : 32 };
  const realProfile = createVulkanRuntimeProfile({ userData, pe });
  const profile = { ...realProfile,
    async prepare(input) { calls.push('profile-start'); const result = await realProfile.prepare(input); calls.push('profile-published'); return result; },
    async inspect(input) { calls.push('profile-inspect'); return realProfile.inspect(input); },
    async archive(input) { calls.push('profile-archive'); if (archiveFailure) { archiveFailure = false; throw Object.assign(new Error('archive denied'), { code: 'EACCES' }); } return realProfile.archive(input); }
  };
  const actualActivation = createReshadeVulkanActivation({ userData, resolveBasePath(exe) {
    return read(bindingPath).bindings.find(row => row.exe.toLowerCase() === exe.toLowerCase())?.basePath;
  } });
  const activation = { ...actualActivation, async write(exe, expected, desired) {
    calls.push(desired.active ? 'activate' : 'deactivate');
    if (desired.active) {
      assert.equal(read(bindingPath).bindings.find(row => row.exe === exe).phase, 'prepared');
      assert.ok(fs.existsSync(read(bindingPath).bindings.find(row => row.exe === exe).basePath));
      if (activationFailure) { activationFailure = false; throw Object.assign(new Error('activation denied'), { code: 'EACCES' }); }
    }
    return actualActivation.write(exe, expected, desired);
  } };
  const registry = { identity: { scope: 'HKCU', view: '64', key: 'Software\\Khronos\\Vulkan\\ImplicitLayers' },
    async read(name) { return structuredClone(values.get(name.toLowerCase()) || { exists: false }); },
    async list() { return [...values].map(([name, row]) => ({ name, type: row.type, data: row.data })); },
    async listMachine() { return []; },
    async write(name, expected, desired) { calls.push(desired.exists ? 'registry-add' : 'registry-remove'); assert.deepEqual(await this.read(name), expected); if (desired.exists) values.set(name.toLowerCase(), structuredClone(desired)); else values.delete(name.toLowerCase()); }
  };
  const guards = { antiCheatPresent: () => antiCheat, async assertGameClosed() { calls.push('closed'); if (running) throw Object.assign(new Error('game running'), { code: 'ERR_GAME_RUNNING' }); } };
  const broker = { async inspect() { calls.push('broker-inspect'); return { elevated: false, launchable: true }; },
    async launch(input) { calls.push(['launch', input]); return { pid: 1234, elevated: false }; } };
  const create = extra => createVulkanService({ userData, appDir, resourcesPath, hardware,
    overrides: { pe, profile, activation, registry, guards, broker, async writeBinding(file, state) {
      if (firstBindingFailure) { firstBindingFailure = false; throw Object.assign(new Error('binding write denied'), { code: 'EACCES' }); }
      if (bindingClearFailure && !state.bindings.length) { bindingClearFailure = false; throw Object.assign(new Error('binding delete denied'), { code: 'EACCES' }); }
      if (archivedSaveFailure && state.bindings.some(row => row.phase === 'archived')) { archivedSaveFailure = false; throw Object.assign(new Error('archive receipt denied'), { code: 'EACCES' }); }
      return atomicJson(file, state);
    } }, ...extra });
  const game = (name, directory) => {
    const dir = directory || path.join(root, name); fs.mkdirSync(dir, { recursive: true });
    const exe = path.join(dir, name + '.exe'); fs.writeFileSync(exe, 'pe64-game');
    return { id: name, dir, scan: { chosen: { path: exe, bitness: 64, apiResolution: { api: 'vulkan', source: 'manual' } } } };
  };
  return { root, userData, appDir, resourcesPath, runtimeRoot, layerRoot, bindingPath, runtime, layer, hardware, calls, values, profile, activation, game, create, writeRecipes,
    setAntiCheat() { antiCheat = true; }, setRunning() { running = true; }, failActivation() { activationFailure = true; }, failArchive() { archiveFailure = true; },
    failClear() { bindingClearFailure = true; }, failArchivedSave() { archivedSaveFailure = true; }, failFirstBinding() { firstBindingFailure = true; } };
}

test('summary only reads metadata and missing/unaccepted packages stay unavailable', t => {
  const f = fixture(t), game = f.game('metadata'), service = f.create();
  assert.equal(service.summary(game).available, true);
  for (let n = 0; n < 20; n++) service.summary(game);
  assert.deepEqual(f.calls, []); assert.equal(f.values.size, 0); assert.equal(fs.existsSync(f.bindingPath), false);
  f.runtime.acceptance.status = 'loaded'; f.writeRecipes();
  assert.equal(service.summary(game).available, false); assert.match(service.summary(game).reason, /实际处理验收/);
  fs.unlinkSync(path.join(f.runtimeRoot, 'recipe.json'));
  assert.equal(service.summary(game).available, false); assert.match(service.summary(game).reason, /尚未提供/);
});

test('install publishes profile and binding before activation; real diagnosis distinguishes configuration from runtime', async t => {
  const f = fixture(t), game = f.game('ordered'), service = f.create();
  const result = await service.install(game);
  assert.equal(result.installed, true); assert.equal(result.runtimeVerified, false);
  assert.ok(f.calls.indexOf('profile-published') < f.calls.indexOf('registry-add'));
  assert.ok(f.calls.indexOf('registry-add') < f.calls.indexOf('activate'));
  const binding = read(f.bindingPath).bindings[0]; assert.equal(binding.phase, 'installed');
  assert.equal(service.configDir(game), binding.basePath);
  const diagnostic = await service.diagnose(game);
  assert.equal(diagnostic.ready, true); assert.equal(diagnostic.status, 'installed');
  assert.equal(diagnostic.loaded, 'unknown'); assert.equal(diagnostic.processed, 'unknown');
  const launched = await service.launch(game, ['--vulkan', 'value with spaces']);
  assert.equal(launched.elevated, false); assert.equal(launched.processed, 'unknown');
  assert.deepEqual(f.calls.find(row => Array.isArray(row))[1], { exe: game.scan.chosen.path, args: ['--vulkan', 'value with spaces'], cwd: game.dir });
});

test('restore retains old profile identity without the package, RTX50 hardware, EXE or a new scan result', async t => {
  const f = fixture(t), game = f.game('old-package'), service = f.create(); await service.install(game);
  const original = service.configDir(game);
  fs.writeFileSync(path.join(original, 'nr_before_sr.ini'), 'user-edited config');
  f.runtime.id = 'new-package'; f.runtime.coreVersion = '0.4.6-hotfix.2-vulkan'; f.writeRecipes();
  const restart = f.create();
  assert.equal(restart.configDir(game), original); assert.equal(restart.summary(game).coreVersion, '0.4.6-hotfix.1-vulkan');
  await assert.rejects(restart.install(game), { code: 'VULKAN_PACKAGE_LOCKED' });
  assert.equal((await restart.diagnose(game)).ready, true);
  fs.rmSync(f.resourcesPath, { recursive: true }); f.hardware.family = 'RTX40'; f.hardware.series = ['RTX40'];
  fs.unlinkSync(game.scan.chosen.path); game.scan.chosen = null;
  assert.equal(restart.summary(game).available, false); assert.equal(restart.summary(game).installed, true);
  const restored = await restart.restore(game);
  assert.equal(restored.restored, true); assert.equal(fs.existsSync(original), false);
  assert.equal(fs.readFileSync(path.join(restored.archivePath, 'nr_before_sr.ini'), 'utf8'), 'user-edited config');
  assert.equal(fs.existsSync(path.join(game.dir, 'ReShade.ini')), false); assert.equal(f.values.size, 0);
  assert.equal(restart.configDir(game), null); assert.deepEqual(read(f.bindingPath).bindings, []);
});

test('route, hardware, bitness, running game and anti-cheat checks reject before mutation', async t => {
  for (const [type, expected] of [['api','VULKAN_API_REQUIRED'],['gpu','VULKAN_GPU_UNSUPPORTED'],['x86','VULKAN_GAME_ARCH'],['running','ERR_GAME_RUNNING'],['anti','ERR_ANTI_CHEAT_CONFIRM']]) {
    const f = fixture(t), game = f.game(type), service = f.create();
    if (type === 'api') game.scan.chosen.apiResolution.api = 'dx12';
    if (type === 'gpu') { f.hardware.family = 'RTX40'; f.hardware.series = ['RTX40']; }
    if (type === 'x86') game.scan.chosen.bitness = 32;
    if (type === 'running') f.setRunning();
    if (type === 'anti') f.setAntiCheat();
    await assert.rejects(service.install(game), { code: expected });
    assert.equal(f.calls.includes('profile-start'), false); assert.equal(f.values.size, 0);
    assert.equal(fs.existsSync(f.bindingPath), false);
  }
  const f = fixture(t), game = f.game('confirmed'), service = f.create();f.setAntiCheat();
  await service.install(game, { allowAntiCheat: true }); assert.equal((await service.launch(game)).pid, 1234);
});

test('native receipt, unknown ReShade and same-directory different EXE never get taken over', async t => {
  const f = fixture(t), one = f.game('one'), service = f.create();
  fs.mkdirSync(path.join(one.dir, '_DLSS5_Backup'));fs.writeFileSync(path.join(one.dir, '_DLSS5_Backup', 'xiaofeng-manager.json'), '{}');
  await assert.rejects(service.install(one), { code: 'VULKAN_NATIVE_INSTALL_PRESENT' });
  const unknown = f.game('unknown');fs.writeFileSync(path.join(unknown.dir, 'ReShade.ini'), '[GENERAL]\nexternal=keep');
  await assert.rejects(service.install(unknown), { code: 'VULKAN_EXTERNAL_RESHADE' });
  assert.equal(fs.readFileSync(path.join(unknown.dir, 'ReShade.ini'), 'utf8'), '[GENERAL]\nexternal=keep');
  const first = f.game('first'), second = f.game('second', first.dir); await service.install(first);
  await assert.rejects(service.install(second), { code: 'VULKAN_DIRECTORY_BOUND' });
  assert.equal(read(f.bindingPath).bindings.length, 1);
  assert.equal((await service.install({ ...first, id: 'alias' })).unchanged, true);
});

test('activation failure retains binding plus deployment WAL; same-game restore recovers then archives', async t => {
  const f = fixture(t), game = f.game('failure'), service = f.create();f.failActivation();
  await assert.rejects(service.install(game), error => error.code === 'EACCES' && error.details.phase === 'deployment');
  assert.equal(read(f.bindingPath).bindings[0].phase, 'prepared');
  assert.equal(service.summary(game).installed, true); assert.equal(service.summary(game).needsRecovery, true);
  assert.equal((await service.diagnose(game)).status, 'pending');
  assert.equal((await service.restore(game)).restored, true);
  assert.equal(fs.existsSync(path.join(f.userData, 'vulkan-deployment', 'pending.json')), false);
  assert.equal(f.values.size, 0);assert.deepEqual(read(f.bindingPath).bindings, []);
});

test('pending from another game is not recovered by the wrong restore action', async t => {
  const f = fixture(t), first = f.game('first-pending'), second = f.game('second-pending'), service = f.create();
  await service.install(first);f.failActivation();await assert.rejects(service.install(second));
  await assert.rejects(service.restore(first), { code: 'VULKAN_RECOVERY_OTHER_GAME' });
  assert.equal((await f.activation.read(first.scan.chosen.path)).active, true);
  assert.ok(fs.existsSync(service.configDir(first)));
  await service.restore(second);await service.restore(first);
});

test('archive failure and crash after archive remain retryable without reactivating', async t => {
  const f = fixture(t), game = f.game('archive'), service = f.create();await service.install(game);
  f.failArchive();await assert.rejects(service.restore(game), { code: 'EACCES' });
  assert.equal(read(f.bindingPath).bindings[0].phase, 'deactivated');assert.equal((await f.activation.read(game.scan.chosen.path)).active, false);
  const previous = await service.restore(game);assert.equal(previous.archived, true);
  await service.install(game);
  f.failArchivedSave();await assert.rejects(service.restore(game), { code: 'EACCES' });
  assert.equal(read(f.bindingPath).bindings[0].phase, 'deactivated');assert.equal(fs.existsSync(service.configDir(game)), false);
  const restart = f.create();const final = await restart.restore(game);
  assert.equal(final.archived, true);assert.notEqual(final.archivePath, previous.archivePath);
  assert.deepEqual(read(f.bindingPath).bindings, []);
});

test('profile drift blocks launch and invalid binding cannot redirect activation', async t => {
  const f = fixture(t), game = f.game('drift'), service = f.create();await service.install(game);
  fs.writeFileSync(path.join(service.configDir(game), 'core.addon64'), 'pe64-foreign');
  const diagnostic = await service.diagnose(game);assert.equal(diagnostic.ready, false);assert.equal(diagnostic.processed, 'unknown');
  await assert.rejects(service.launch(game), { code: 'VULKAN_NOT_READY' });
  assert.equal(f.calls.some(row => Array.isArray(row)), false);
  const state = read(f.bindingPath);state.bindings[0].basePath = f.root;fs.writeFileSync(f.bindingPath, JSON.stringify(state));
  await assert.rejects(service.restore(game), { code: 'VULKAN_BINDING_INVALID' });
  assert.equal(service.summary(game).available, false);
  assert.equal(service.summary(game).installed, true); assert.equal(service.summary(game).needsRecovery, true);
});

test('install accepts the exact fixed package ID or core version and rejects arbitrary core selection', async t => {
  const f = fixture(t), game = f.game('fixed-package'), service = f.create();
  await assert.rejects(service.install(game, { version: '0.3.3.5' }), { code: 'VULKAN_PACKAGE_LOCKED' });
  assert.equal(f.calls.includes('profile-start'), false); assert.equal(fs.existsSync(f.bindingPath), false);
  assert.equal((await service.install(game, { version: f.runtime.id })).installed, true);
  assert.equal((await service.install(game, { version: f.runtime.coreVersion })).unchanged, true);
});

test('malformed or lost bindings preserve a visible recovery state and cannot become a successful no-op restore', async t => {
  const f = fixture(t), game = f.game('orphan'), service = f.create(); await service.install(game);
  const original = fs.readFileSync(f.bindingPath), ini = fs.readFileSync(path.join(game.dir, 'ReShade.ini'));
  fs.writeFileSync(f.bindingPath, '{invalid');
  assert.equal(service.summary(game).installed, true); assert.equal(service.summary(game).needsRecovery, true);
  assert.equal(service.summary(game).available, false);
  assert.throws(() => service.configDir(game), { code: 'VULKAN_RECORD_INVALID' });
  await assert.rejects(service.restore(game), error => error.code === 'VULKAN_RECORD_INVALID' && error.details.needsRecovery);
  const other = f.game('unrelated'); assert.equal(service.summary(other).installed, false);
  assert.equal(service.summary(other).needsRecovery, true); // unreadable global scope requires the caller's guard
  fs.unlinkSync(f.bindingPath);
  assert.equal(service.summary(game).installed, true); assert.equal((await service.diagnose(game)).pending, true);
  await assert.rejects(service.restore(game), { code: 'VULKAN_BINDING_MISSING' });
  await assert.rejects(service.install(game), { code: 'VULKAN_BINDING_MISSING' });
  assert.deepEqual(fs.readFileSync(path.join(game.dir, 'ReShade.ini')), ini); assert.equal(f.values.size, 1);
  assert.equal(service.summary(other).needsRecovery, false); // valid absent state does not taint unrelated games
  fs.writeFileSync(f.bindingPath, original); assert.equal((await service.restore(game)).restored, true);
});

test('binding clear failure after a successful archive is retryable and remains installed for recovery routing', async t => {
  const f = fixture(t), game = f.game('clear-binding'), service = f.create(); await service.install(game);
  f.failClear(); await assert.rejects(service.restore(game), { code: 'EACCES' });
  assert.equal(read(f.bindingPath).bindings[0].phase, 'archived');
  assert.equal(service.summary(game).installed, true); assert.equal(service.summary(game).needsRecovery, true);
  assert.equal((await f.activation.read(game.scan.chosen.path)).active, false);
  assert.equal((await f.create().restore(game)).restored, true); assert.deepEqual(read(f.bindingPath).bindings, []);
});

test('failed first binding write never activates a layer and accurately reports only the retained profile', async t => {
  const f = fixture(t), game = f.game('binding-write'), service = f.create(); f.failFirstBinding();
  await assert.rejects(service.install(game), error => error.code === 'EACCES' && error.details.phase === 'binding' &&
    error.details.bindingRetained === false && error.details.profileRetained === true);
  assert.equal(fs.existsSync(f.bindingPath), false); assert.equal(f.values.size, 0);
  assert.equal(fs.existsSync(path.join(game.dir, 'ReShade.ini')), false);
  assert.equal((await service.install(game)).installed, true); // verified complete published profile is reusable
});
