'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { createExternalRuntime, RECEIPT, PENDING } = require('../src/product/external-runtime');
const { INSTALLED_NAMES } = require('../src/product/constants');
const { addonValues } = require('../src/product/reshade-layout');
const hash = value => crypto.createHash('sha256').update(value).digest('hex');

function fixture(t, options = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xf-direct-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const gameRoot = path.join(root, 'game'), dir = path.join(gameRoot, 'Pal', 'Binaries', 'Win64');
  fs.mkdirSync(dir, { recursive: true });
  const exe = path.join(dir, 'Palworld-Win64-Shipping.exe'); fs.writeFileSync(exe, 'only a fixture EXE');
  const game = { id: 'palworld-fixture', dir: gameRoot, scan: { chosen: { path: exe, bitness: 64, apiResolution: { api: 'dx12' } } } };
  const original = '; existing personal configuration\r\n[ADDON]\r\nAddonPath=.\\addon\r\nDisabledAddons=Generic Depth,User HDR@hdr.addon64\r\n' +
    '[GENERAL]\r\nEffectSearchPaths=.\\reshade-shaders\\**\r\nPresetPath=.\\ReShadePreset.ini\r\n[STYLE]\r\nHdrOverlayBrightness=203\r\n' +
    '[SCREENSHOT]\r\nSavePath=.\\shots\r\nPostSaveCommandWorkingDirectory=.\\\r\n';
  fs.writeFileSync(path.join(dir, 'ReShade.ini'), original); fs.writeFileSync(path.join(dir, 'ReShadePreset.ini'), 'Techniques=UserChoice');
  fs.writeFileSync(path.join(dir, 'renodx-ue-extended.addon64'), 'inactive root HDR');
  const payloadDir = path.join(root, 'payload'); fs.mkdirSync(payloadDir);
  const payload = { version: '0.4.7beta', versionInfo: { compatibility: 'dx11' } };
  for (const kind of ['addon', 'bridge', 'runtime', 'config', 'reshade', 'carrier']) {
    const file = path.join(payloadDir, INSTALLED_NAMES[kind]), bytes = kind === 'reshade' ? 'ReShade Searching for add-ons' : kind === 'config' ? '[NRBeforeSR]\nIntensity=1.4\n' : 'payload:' + kind;
    fs.writeFileSync(file, bytes); payload[kind] = { file, actual: hash(bytes) };
  }
  fs.copyFileSync(payload.runtime.file, path.join(dir, INSTALLED_NAMES.runtime));
  const deploymentOptions = { userData: path.join(root, 'data'), pe: { getBitness: () => 64, getImports: () => [] },
    guards: { assertGameClosed: async () => {}, antiCheatPresent: () => false }, ...options };
  const service = createExternalRuntime(deploymentOptions);
  return { root, gameRoot, dir, exe, game, payload, original, service, options: deploymentOptions,
    pending: path.join(gameRoot, PENDING), receipt: path.join(gameRoot, RECEIPT), request: { mode: 'external', loadingMode: 'proxy', api: 'dx12', payload } };
}

test('first external deployment handles Palworld missing directories without activating root HDR or owning existing runtime', async t => {
  const f = fixture(t), preset = fs.readFileSync(path.join(f.dir, 'ReShadePreset.ini'));
  const current = f.service.getLayout(f.game);
  assert.equal(current.addonDir, path.join(f.dir, 'addon')); assert.equal(current.verified, false);
  const plan = await f.service.preview(f.game, f.request);
  assert.equal(plan.configured.addonDir, path.join(f.dir, 'addon'));
  assert.equal(plan.desired.addonDir, f.service.location(f.game).runtimeDir);
  assert.ok(plan.warnings.some(row => row.code === 'ADDON_DIRECTORY_MISSING'));
  assert.ok(plan.warnings.some(row => row.code === 'FILTER_DIRECTORY_MISSING'));
  assert.equal(plan.inactiveAddons[0].name, 'renodx-ue-extended.addon64');
  assert.ok(!plan.changes.some(row => row.path === path.join(f.dir, INSTALLED_NAMES.runtime)));
  assert.equal(fs.existsSync(path.join(f.gameRoot, '_DLSS5_Backup')), false);
  assert.equal(fs.existsSync(f.options.userData), false);
  const result = await f.service.apply(plan.planId), layout = result.layout;
  assert.equal(layout.verified, true); assert.equal((await f.service.inspect(f.game)).ready, true);
  assert.equal(fs.existsSync(path.join(f.gameRoot, '_DLSS5_Backup', 'xiaofeng-manager.json')), false);
  assert.equal(fs.existsSync(path.join(layout.runtimeDir, 'renodx-ue-extended.addon64')), false);
  assert.equal(fs.readFileSync(path.join(f.dir, 'renodx-ue-extended.addon64'), 'utf8'), 'inactive root HDR');
  assert.equal(fs.readFileSync(path.join(f.dir, INSTALLED_NAMES.runtime), 'utf8'), 'payload:runtime');
  const active = fs.readFileSync(layout.activeConfigPath, 'utf8');
  assert.equal(addonValues(active).get('AddonPath')[0], '.');
  assert.equal(addonValues(active, 'GENERAL').get('PresetPath')[0], path.join(f.dir, 'ReShadePreset.ini'));
  assert.match(active, /DisabledAddons=Generic Depth,User HDR@hdr.addon64/); assert.match(active, /HdrOverlayBrightness=203/);
  await assert.rejects(f.service.preview(f.game, { mode: 'local' }), { code: 'DEPLOYMENT_DIRECT_LOCAL_UNSUPPORTED' });
  const preview = await f.service.previewRemove(f.game, 'restore'); assert.ok(preview.changes.some(row => row.role === 'game-proxy' && row.afterSha256 === null));
  await f.service.remove(f.game, 'restore');
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  assert.deepEqual(fs.readFileSync(path.join(f.dir, 'ReShadePreset.ini')), preset);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
  assert.equal(fs.readFileSync(path.join(f.dir, INSTALLED_NAMES.runtime), 'utf8'), 'payload:runtime');
  assert.equal(fs.existsSync(path.join(layout.runtimeDir, INSTALLED_NAMES.addon)), false);
  assert.equal((await f.service.inspect(f.game)).installed, false);
});

test('active custom directory and explicit DLL loads retain disabled HDR, sidecars and original file locations', async t => {
  const f = fixture(t), addon = path.join(f.dir, 'addon'); fs.mkdirSync(addon);
  fs.writeFileSync(path.join(addon, 'hdr.addon64'), 'user HDR in searched directory');
  fs.writeFileSync(path.join(addon, 'hdr.ini'), 'UserBrightness=170');
  fs.writeFileSync(path.join(addon, 'Explicit.dll'), 'explicit early user module');
  fs.appendFileSync(path.join(f.dir, 'ReShade.ini'), '[ADDON]\r\nLoadFromDllMain=.\\Explicit.dll\r\n');
  const before = fs.readFileSync(path.join(f.dir, 'ReShade.ini'));
  const plan = await f.service.preview(f.game, f.request), result = await f.service.apply(plan.planId);
  const active = result.layout.runtimeDir;
  assert.equal(fs.readFileSync(path.join(active, 'hdr.addon64'), 'utf8'), 'user HDR in searched directory');
  assert.equal(fs.readFileSync(path.join(active, 'hdr.ini'), 'utf8'), 'UserBrightness=170');
  assert.equal(addonValues(fs.readFileSync(path.join(active, 'ReShade.ini'), 'utf8')).get('LoadFromDllMain')[0], 'Explicit.dll');
  assert.match(fs.readFileSync(path.join(active, 'ReShade.ini'), 'utf8'), /User HDR@hdr.addon64/);
  await f.service.remove(f.game, 'clean');
  assert.deepEqual(fs.readFileSync(path.join(f.dir, 'ReShade.ini')), before);
  assert.equal(fs.readFileSync(path.join(addon, 'hdr.addon64'), 'utf8'), 'user HDR in searched directory');
});

test('ambiguous or linked source layouts fail preview without creating any owner state', async t => {
  for (const variant of ['multiple', 'junction']) {
    const f = fixture(t);
    if (variant === 'multiple') fs.appendFileSync(path.join(f.dir, 'ReShade.ini'), '[ADDON]\nAddonPath=.\\another\n');
    else { const target = path.join(f.root, 'junction-target'); fs.mkdirSync(target); fs.symlinkSync(target, path.join(f.dir, 'addon'), 'junction'); }
    await assert.rejects(f.service.preview(f.game, f.request), { code: 'DEPLOYMENT_SOURCE_LAYOUT' });
    assert.equal(fs.existsSync(f.options.userData), false); assert.equal(fs.existsSync(path.join(f.gameRoot, '_DLSS5_Backup')), false);
  }
});

test('source changes after first external preview reject before a file WAL or profile exists', async t => {
  const f = fixture(t), plan = await f.service.preview(f.game, f.request);
  fs.appendFileSync(path.join(f.dir, 'ReShade.ini'), '; new external edit\n');
  await assert.rejects(f.service.apply(plan.planId), { code: 'DEPLOYMENT_PLAN_CHANGED' });
  assert.equal(fs.existsSync(f.pending), false); assert.equal(fs.existsSync(f.options.userData), false);
  assert.match(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), /new external edit/);
});

test('first external interruption after publishing proxy recovers exact original config and leaves old unowned files', async t => {
  const f = fixture(t, { afterWrite: ({ row }) => { if (row.role === 'game-proxy') throw Object.assign(new Error('process exited'), { preservePending: true }); } });
  const plan = await f.service.preview(f.game, f.request);
  await assert.rejects(f.service.apply(plan.planId), /process exited/); assert.equal(fs.existsSync(f.pending), true);
  const restarted = createExternalRuntime({ ...f.options, afterWrite: undefined });
  assert.equal((await restarted.recover(f.game)).recovered, true);
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false); assert.equal(fs.existsSync(f.receipt), false);
  assert.equal(fs.readFileSync(path.join(f.dir, INSTALLED_NAMES.runtime), 'utf8'), 'payload:runtime');
});

test('direct clean archives a replaced proxy while restore restores it; an identical borrowed proxy stays unowned', async t => {
  for (const mode of ['clean', 'restore', 'borrowed']) {
    const f = fixture(t), previous = mode === 'borrowed' ? fs.readFileSync(f.payload.reshade.file) : Buffer.from('older ReShade Searching for add-ons');
    fs.writeFileSync(path.join(f.dir, 'dxgi.dll'), previous);
    const plan = await f.service.preview(f.game, f.request); await f.service.apply(plan.planId);
    const saved = JSON.parse(fs.readFileSync(f.receipt)); assert.equal(saved.initialProxy.owned, mode !== 'borrowed');
    const removed = await f.service.remove(f.game, mode === 'restore' ? 'restore' : 'clean');
    assert.equal(removed.removed, true);
    if (mode === 'clean') assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
    else assert.deepEqual(fs.readFileSync(path.join(f.dir, 'dxgi.dll')), previous);
    assert.deepEqual(fs.readFileSync(path.join(f.options.userData, 'external-runtime', saved.id, 'history', saved.initialOperation, saved.initialProxy.snapshot)), previous);
    assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  }
});

test('direct standard layout converts to an owned ordinary install and its ordinary restore remains valid', async t => {
  const f = fixture(t), localIni = f.original.replace('AddonPath=.\\addon', 'AddonPath=.');
  fs.writeFileSync(path.join(f.dir, 'ReShade.ini'), localIni);
  fs.unlinkSync(path.join(f.dir, INSTALLED_NAMES.runtime));
  const first = await f.service.preview(f.game, f.request); await f.service.apply(first.planId);
  const external = f.service.getLayout(f.game);
  fs.writeFileSync(path.join(external.runtimeDir, INSTALLED_NAMES.config), '[NRBeforeSR]\nIntensity=1.7\n');
  const local = await f.service.preview(f.game, { mode: 'local' });
  assert.ok(local.changes.some(row => row.role === 'manifest'));
  await f.service.apply(local.planId);
  assert.match(fs.readFileSync(path.join(f.dir, INSTALLED_NAMES.config), 'utf8'), /Intensity=1.7/);
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), localIni);
  assert.equal(fs.readFileSync(path.join(f.dir, INSTALLED_NAMES.addon), 'utf8'), 'payload:addon');
  assert.equal(fs.existsSync(path.join(f.dir, 'renodx-ue-extended.addon64')), true);
  const installer = require('../src/product/installer').createInstaller({ guards: f.options.guards, pe: f.options.pe });
  const removed = await installer.uninstall({ gameDir: f.gameRoot, scan: f.game.scan, mode: 'restore', removeSettings: false });
  assert.equal(removed.removed, true); assert.equal(fs.existsSync(path.join(f.dir, INSTALLED_NAMES.addon)), false);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
});

test('direct helper transition and later proxy transition keep the original uninstall baseline', async t => {
  const f = fixture(t), first = await f.service.preview(f.game, f.request); await f.service.apply(first.planId);
  const helper = await f.service.preview(f.game, { mode: 'external', loadingMode: 'helper' }); await f.service.apply(helper.planId);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
  const proxy = await f.service.preview(f.game, { mode: 'external', loadingMode: 'proxy' }); await f.service.apply(proxy.planId);
  await f.service.remove(f.game, 'restore');
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
});

test('UTF-8 BOM survives a first external round trip and a changed root proxy is never blessed', async t => {
  const f = fixture(t), original = Buffer.from('\uFEFF' + f.original); fs.writeFileSync(path.join(f.dir, 'ReShade.ini'), original);
  const plan = await f.service.preview(f.game, f.request); await f.service.apply(plan.planId);
  const rootProxy = path.join(f.dir, 'dxgi.dll'), loader = fs.readFileSync(rootProxy);
  fs.writeFileSync(rootProxy, 'external replacement proxy');
  assert.equal((await f.service.inspect(f.game)).ready, false);
  await assert.rejects(f.service.previewRemove(f.game), { code: 'DEPLOYMENT_FILE_CHANGED' });
  assert.equal(fs.readFileSync(rootProxy, 'utf8'), 'external replacement proxy');
  fs.writeFileSync(rootProxy, loader); await f.service.remove(f.game, 'restore');
  assert.deepEqual(fs.readFileSync(path.join(f.dir, 'ReShade.ini')), original);
});

test('an invalid output prefix and a partial initial copy never leave a live component or file WAL', async t => {
  const prefix = fixture(t);
  fs.mkdirSync(prefix.options.userData, { recursive: true }); fs.writeFileSync(path.join(prefix.options.userData, 'external-runtime'), 'existing unrelated file');
  await assert.rejects(prefix.service.preview(prefix.game, prefix.request));
  assert.equal(fs.existsSync(prefix.pending), false);
  assert.equal(fs.readFileSync(path.join(prefix.options.userData, 'external-runtime'), 'utf8'), 'existing unrelated file');
  const f = fixture(t, { copyFile: async (source, destination) => { fs.writeFileSync(destination, fs.readFileSync(source).subarray(0, 3)); throw Object.assign(new Error('copy interrupted'), { code: 'EACCES' }); } });
  const plan = await f.service.preview(f.game, f.request);
  await assert.rejects(f.service.apply(plan.planId), /copy interrupted/);
  assert.equal(fs.existsSync(f.pending), false); assert.equal(fs.existsSync(f.receipt), false);
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
  assert.equal(fs.readdirSync(f.service.location(f.game).runtimeDir).length, 0);
});

test('an original addon appearing during publication rolls back instead of silently changing the active profile', async t => {
  let originalAddonDirectory;
  const f = fixture(t, { afterWrite: ({ row }) => {
    if (row.role === 'game-proxy') { fs.mkdirSync(originalAddonDirectory); fs.writeFileSync(path.join(originalAddonDirectory, 'new-user.addon64'), 'external user addon'); }
  } });
  originalAddonDirectory = path.join(f.dir, 'addon');
  const plan = await f.service.preview(f.game, f.request);
  await assert.rejects(f.service.apply(plan.planId), { code: 'DEPLOYMENT_PLAN_CHANGED' });
  assert.equal(fs.existsSync(f.pending), false); assert.equal(fs.existsSync(f.receipt), false);
  assert.equal(fs.readFileSync(path.join(originalAddonDirectory, 'new-user.addon64'), 'utf8'), 'external user addon');
  assert.equal(fs.readFileSync(path.join(f.dir, 'ReShade.ini'), 'utf8'), f.original);
  assert.equal(fs.existsSync(path.join(f.dir, 'dxgi.dll')), false);
});
