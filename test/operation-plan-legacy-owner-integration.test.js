'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { fixture, peBytes, put, sha, PROJECT } = require('./helpers/operation-integration-fixture');
const { createCompactBundle } = require('../src/product/payload');
const { createExternalRuntime, RECEIPT: PROFILE_RECEIPT } = require('../src/product/external-runtime');
const { createHoYoProfileService } = require('../src/product/hoyoshade-profile');
const { createLegacyService, RECEIPT: LEGACY_RECEIPT } = require('../src/product/legacy-service');
const { createFeederRoutingService } = require('../src/product/feeder-routing-service');
const { createLegacyRuntime } = require('../src/product/legacy-runtime');
const { UPSTREAM } = require('../src/product/legacy-runtime-catalog');
const { fingerprint } = require('../src/product/feeder-runtime');
const { getIni } = require('../src/product/launch-ini');
const confirm = plan => ({ confirm: true, fingerprint: plan.fingerprint });

function runtimeFixture(root, pe, loader) {
  const assets = [], pool = path.join(root, 'fixture-legacy-pool');
  const add = (id, name, role, content, mutable = false, target) => {
    const bytes = Buffer.isBuffer(content) ? content : Buffer.from(content); put(path.join(pool, name), bytes);
    assets.push({ id, source: name, role, sha256: sha(bytes), bytes: bytes.length, mutable,
      architecture: /\.(?:dll|addon64)$/.test(name) ? 'x64' : null, ...(target ? { target } : {}) });
  };
  add('loader-x64', 'ReShade64.dll', 'game-loader', loader);
  add('provider-x64', 'dlss5-feed.addon64', 'provider', peBytes('fixed Feeder provider'));
  add('core', 'fixture-feeder-core.addon64', 'core', peBytes('fixed Feeder Core NRBeforeSR'));
  add('chain', 'nrchain_nvngx.dll', 'chain', peBytes('fixed Feeder chain'));
  for (const family of ['rtx40', 'rtx50']) add('runtime-' + family, family + '.dll', 'nr-runtime', peBytes('runtime ' + family));
  add('core-config', 'nr_before_sr.ini', 'core-config', '[NRBeforeSR]\nEnabled=1\nIntensity=1.2\nR8OutputEncoding=2\n', true);
  add('preset', 'ReShadePreset.ini', 'preset', 'Techniques=vort_MotionEffects@vort_Motion.fx,DLSS5_Feed@DLSS5_Feed.fx\n', true);
  add('shader-feed', 'Feed.fx', 'shader', 'fixture shader', false, 'reshade-shaders/Shaders/DLSS5_Feed.fx');
  add('shader-motion', 'Motion.fx', 'shader', 'fixture motion', false, 'reshade-shaders/Shaders/vort_Motion.fx');
  add('texture', 'fixture.png', 'texture', 'fixture texture', false, 'reshade-shaders/Textures/fixture.png');
  add('license', 'LICENSE.txt', 'license', 'fixture license', false, 'LICENSE.txt');
  const manifest = { schema: 1, upstream: UPSTREAM, coreInterface: 'NRExternalProviderV1', coreVersion: 'fixture-feeder-core',
    coreFileName: 'fixture-feeder-core.addon64', coreVariant: { requiredInterface: 'NRExternalProviderV1', genericCoreInterchangeable: false }, assets };
  put(path.join(pool, 'manifest.json'), JSON.stringify(manifest));
  const lock = { manifestFingerprint: fingerprint(manifest), restorableRecipeFingerprints: [] };
  const runtime = createLegacyRuntime({ appDir: root, root: pool, pe, lock });
  for (const loadingBackend of ['local', 'hoyoshade']) lock.restorableRecipeFingerprints.push(runtime.load({ api: 'dx11', architecture: 'x64', hardwareFamily: 'RTX50', loadingBackend }).fingerprint);
  return runtime;
}
async function jointFixture(t, mode) {
  return fixture(t, { api: 'dx11', family: 'RTX50', noDlss: true, exeName: mode === 'hoyoshade' ? 'YuanShen.exe' : 'Game.exe',
    specialSetup: async ({ root, userData, resourcesPath, hardware, guards, pe, gameRoot, exeDir, exe }) => {
      const loader = fs.readFileSync(path.join(PROJECT, 'payload/nr-before-sr/fixed/RTX50/ReShade64.dll'));
      const payload = path.join(resourcesPath, 'payload/nr-before-sr');
      for (const family of ['RTX40', 'RTX50']) put(path.join(payload, 'fixed', family, 'ReShade64.dll'), loader);
      put(path.join(payload, 'bundle.json'), JSON.stringify(createCompactBundle(payload,
        ['fixture-core-1', 'fixture-core-2'].map(id => ({ id, label: id, compatibility: 'dx11' })), 'fixture-core-1')));
      put(path.join(resourcesPath, 'hoyoshade/component.json'), fs.readFileSync(path.join(PROJECT, 'resources/hoyoshade/component.json')));
      const runtime = runtimeFixture(root, pe, loader), events = [], launcher = path.join(root, 'HYP.exe'); put(launcher, peBytes('fixture HYP; never launched'));
      const external = createExternalRuntime({ userData, pe, guards, afterWrite: ({ row }) => {
        if (row.role === 'profile-loader' && row.after === null) {
          assert.equal(fs.existsSync(path.join(gameRoot, LEGACY_RECEIPT)), false, 'legacy restore completes before the profile loader is removed');
          events.push('profile-remove');
        }
      } });
      let modern;
      const known = async game => modern ? modern.ownedModuleManifest(game) : [];
      const hoyo = createHoYoProfileService({ userData, resourcesPath, appDir: root, externalRuntime: external, pe, getKnownComponents: known });
      modern = createLegacyService({ appDir: root, userData, resourcesPath, runtime, hardware, pe, guards, getLayout: game => hoyo.profile(game), getKnownComponents: known,
        broker: { inspect: async () => ({ elevated: false, launchable: true }), launch: async () => { throw new Error('fixture never launches a game'); } },
        afterWrite: ({ row }) => { if (row.role === 'receipt' && row.after === null) events.push('legacy-remove'); } });
      const feeder = createFeederRoutingService({ appDir: root, userData, resourcesPath, runtime, modern, hardware });
      const original = '; personal INI\r\n[ADDON]\r\nAddonPath=.\\personal-addons\r\n[GENERAL]\r\nPresetPath=.\\PersonalPreset.ini\r\n[STYLE]\r\nHdrOverlayBrightness=178\r\n[INPUT]\r\nKeyOverlay=36,0,0,0\r\n';
      put(path.join(exeDir, 'ReShade.ini'), original); put(path.join(exeDir, 'PersonalPreset.ini'), 'Techniques=UserEffect');
      return { overrides: { feeder, hoyo, externalDeployment: external }, runtime, modern, hoyo, external, feeder, events, launcher, original, exe };
    } });
}
async function stablePreview(f, request) {
  const first = await f.plans.preview(f.id, request), second = await f.plans.preview(f.id, request);
  assert.deepEqual(first.blockers, []); assert.deepEqual(second.blockers, []); assert.equal(first.fingerprint, second.fingerprint);
  return first;
}

test('real AppService and OperationPlan keep local Feeder previews stable, route NR and hotkeys correctly, and restore the owner', async t => {
  const f = await jointFixture(t, 'local'), request = { route: 'feeder', api: 'dx11', loadingBackend: 'local', deployment: 'local' };
  const preview = await stablePreview(f, request); assert.equal(fs.existsSync(path.join(f.gameRoot, LEGACY_RECEIPT)), false);
  assert.equal((await f.plans.apply(preview.planId, confirm(preview))).applied, true);
  const game = (await f.service.listGames())[0], layout = f.service.getLayout(f.id);
  assert.equal((await f.special.modern.inspect(game)).ready, true); assert.equal((await f.service.inspectDeployment(f.id)).ready, true);
  assert.equal(layout.source, 'feeder'); assert.ok(layout.nrConfigDir.includes('_DLSS5_Feeder15'));
  const settings = await stablePreview(f, { nr: { Intensity: 1.65 }, hotkeys: { reshade: { key: 121, ctrl: true, shift: false, alt: false } } });
  await f.plans.apply(settings.planId, confirm(settings));
  assert.equal((await f.service.readNrSettings(f.id)).Intensity, 1.65); assert.equal((await f.service.readGameHotkeys(f.id)).reshade.key, 121);
  assert.match(fs.readFileSync(path.join(layout.nrConfigDir, 'nr_before_sr.ini'), 'utf8'), /Intensity=1\.65/);
  assert.equal(fs.existsSync(path.join(f.exeDir, 'nr_before_sr.ini')), false);
  assert.equal(getIni(fs.readFileSync(layout.activeConfigPath, 'utf8'), 'INPUT', 'KeyOverlay'), '121,1,0,0');
  const restore = await stablePreview(f, { uninstall: 'restore' }); await f.plans.apply(restore.planId, confirm(restore));
  assert.equal(fs.existsSync(path.join(f.gameRoot, LEGACY_RECEIPT)), false); assert.equal(fs.existsSync(path.join(f.exeDir, 'dxgi.dll')), false);
  assert.match(fs.readFileSync(path.join(f.exeDir, 'ReShade.ini'), 'utf8'), /HdrOverlayBrightness=178/);
  assert.equal(fs.readFileSync(path.join(f.exeDir, 'PersonalPreset.ini'), 'utf8'), 'Techniques=UserEffect');
  assert.equal((await f.plans.inspect(f.id)).pending, false);
});

test('real AppService and OperationPlan bind HoYo and legacy owners, retain pins across rebind and repair, then restore legacy before profile', async t => {
  const f = await jointFixture(t, 'hoyoshade'), request = { route: 'feeder', api: 'dx11', loadingBackend: 'hoyoshade',
    hoyo: { family: 'genshin', channel: 'cn', launcher: { kind: 'hoyoplay', path: f.special.launcher } } };
  const preview = await stablePreview(f, request); assert.equal((await f.plans.apply(preview.planId, confirm(preview))).applied, true);
  let game = (await f.service.listGames())[0];
  assert.equal((await f.special.hoyo.inspect(game)).ready, true); assert.equal((await f.special.modern.inspect(game)).ready, true);
  assert.equal((await f.service.inspectDeployment(f.id)).ready, true);
  const record = f.special.modern.receipt(game), core = record.files.find(row => row.role === 'core'), coreBytes = fs.readFileSync(core.path);
  const rebound = await stablePreview(f, request); await f.plans.apply(rebound.planId, confirm(rebound));
  const repaired = await stablePreview(f, { repair: true }); await f.plans.apply(repaired.planId, confirm(repaired));
  game = (await f.service.listGames())[0]; assert.equal(f.special.modern.receipt(game).recipeFingerprint, record.recipeFingerprint); assert.deepEqual(fs.readFileSync(core.path), coreBytes);
  assert.equal((await f.special.hoyo.inspect(game)).ready, true); assert.equal((await f.special.modern.inspect(game)).ready, true);
  const layout = f.service.getLayout(f.id); assert.notEqual(layout.activeConfigPath, path.join(f.exeDir, 'ReShade.ini'));
  const edited = await stablePreview(f, { nr: { Intensity: 1.55 }, hotkeys: { reshade: { key: 120, ctrl: false, shift: true, alt: false } } });
  await f.plans.apply(edited.planId, confirm(edited)); assert.match(fs.readFileSync(path.join(layout.nrConfigDir, 'nr_before_sr.ini'), 'utf8'), /Intensity=1\.55/);
  assert.equal(getIni(fs.readFileSync(layout.activeConfigPath, 'utf8'), 'INPUT', 'KeyOverlay'), '120,0,1,0');
  assert.equal(fs.existsSync(path.join(f.exeDir, 'nr_before_sr.ini')), false);
  const restore = await stablePreview(f, { uninstall: 'restore' }); await f.plans.apply(restore.planId, confirm(restore));
  assert.ok(f.special.events.indexOf('legacy-remove') >= 0); assert.ok(f.special.events.indexOf('legacy-remove') < f.special.events.indexOf('profile-remove'));
  assert.equal(fs.existsSync(path.join(f.gameRoot, LEGACY_RECEIPT)), false); assert.equal(JSON.parse(fs.readFileSync(path.join(f.gameRoot, PROFILE_RECEIPT))).removed, true);
  assert.equal(fs.readFileSync(path.join(f.exeDir, 'ReShade.ini'), 'utf8'), f.special.original); assert.equal(fs.readFileSync(path.join(f.exeDir, 'PersonalPreset.ini'), 'utf8'), 'Techniques=UserEffect');
  assert.equal((await f.plans.inspect(f.id)).pending, false);
});
