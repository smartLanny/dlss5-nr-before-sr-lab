'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createAppService } = require('../src/product/app-service');
const { createLibraryService } = require('../src/product/library-service');
const { createInstaller } = require('../src/product/installer');
const { createCompactBundle } = require('../src/product/payload');
const { sha256 } = require('../src/product/payload');
const { PAYLOAD_FILES, DX11_COMPAT_VERSION, DX11_COMPAT_CARRIER } = require('../src/product/constants');
const journal = require('../src/core/file-journal');
const { CARRIER, BRIDGE, zip, dx11Fixture } = require('./helpers/ota-fixture');

function makePayload(t, root) {
  const payloadRoot = path.join(root, 'resources', 'payload', 'nr-before-sr');
  for (const family of ['RTX40', 'RTX50']) {
    const dir = path.join(payloadRoot, 'fixed', family);
    fs.mkdirSync(dir, { recursive: true });
    for (const name of ['ReShade64.dll', 'nrchain_nvngx.dll', 'nvngx_dlssnr.dll'])
      fs.writeFileSync(path.join(dir, name), name === 'ReShade64.dll' ? 'ReShade Searching for add-ons' : `${family}:${name}`);
  }
  for (const version of ['0.3.3.5', DX11_COMPAT_VERSION]) {
    const dir = path.join(payloadRoot, 'versions', version);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, PAYLOAD_FILES.addon), `${version}:addon`);
    fs.writeFileSync(path.join(dir, PAYLOAD_FILES.config), `${version}:config`);
  }
  const compat = path.join(payloadRoot, 'versions', DX11_COMPAT_VERSION);
  fs.writeFileSync(path.join(compat, PAYLOAD_FILES.bridge), 'compat:bridge');
  fs.writeFileSync(path.join(compat, DX11_COMPAT_CARRIER), 'compat:carrier');
  const bundle = createCompactBundle(payloadRoot, [
    { id: '0.3.3.5', label: 'stable' },
    { id: DX11_COMPAT_VERSION, label: 'unified DX11', compatibility: 'dx11' }
  ], '0.3.3.5');
  fs.writeFileSync(path.join(payloadRoot, 'bundle.json'), JSON.stringify(bundle));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return payloadRoot;
}

function makeService(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-api-service-'));
  const gameDir = path.join(root, 'game');
  const exeDir = path.join(gameDir, 'Binaries', 'Win64');
  fs.mkdirSync(exeDir, { recursive: true });
  const exe = path.join(exeDir, 'Game.exe');
  fs.writeFileSync(exe, 'fixture exe');
  const payloadDir = makePayload(t, root);
  const scan = {
    gameDir,
    exeCandidates: [{ path: exe, rel: 'Binaries\\Win64\\Game.exe', name: 'Game.exe', size: 1, api: 'dxgi', apiLabel: 'DirectX 11/12', bitness: 64, dx12: false }],
    chosen: { path: exe, rel: 'Binaries\\Win64\\Game.exe', name: 'Game.exe', size: 1, api: 'dxgi', apiLabel: 'DirectX 11/12', bitness: 64, dx12: false },
    dlssFiles: [{ path: path.join(gameDir, 'nvngx_dlss.dll'), name: 'nvngx_dlss.dll', bitness: 64 }],
    primaryDlss: { path: path.join(gameDir, 'nvngx_dlss.dll'), name: 'nvngx_dlss.dll', bitness: 64 },
    emulator: null,
    reshade: { installed: false }
  };
  const scanModule = {
    async scanGame() { return structuredClone(scan); },
    selectPrimaryDlss(files) { return files[0] || null; },
    inspectReShade(dir) {
      const file = path.join(dir, 'dxgi.dll');
      return fs.existsSync(file)
        ? { installed: true, file: 'dxgi.dll', addonSupport: fs.readFileSync(file).includes('Searching for add-ons') }
        : { installed: false, file: null, addonSupport: false };
    }
  };
  const library = createLibraryService({ scan: scanModule, library: { discover: () => ({ games: [], roots: [] }), dedupe: rows => rows } });
  const installer = createInstaller({ journal, scan: scanModule, guards: { antiCheatPresent: () => false, assertGameClosed: async () => {} }, pe: { getBitness: () => 64 } });
  const service = createAppService({ userData: path.join(root, 'user-data'), resourcesPath: path.join(root, 'resources'), appDir: root, version: '0.4.5', overrides: { library, installer, detectGpu: () => ({ family: 'RTX40', series: ['RTX 4070'], vendor: 'NVIDIA' }) } });
  return { root, gameDir, exe, payloadDir, service, scan, library, installer };
}

async function externalFixture(t) {
  const f=makeService(t);await f.service.addManualGame(f.gameDir);const id=(await f.service.boot()).games[0].id;await f.service.setGameApi(id,'dx12');
  const external=path.join(f.root,'external','nr-before-sr');fs.cpSync(f.payloadDir,external,{recursive:true});
  const addon=path.join(external,'versions','0.3.3.5',PAYLOAD_FILES.addon);fs.writeFileSync(addon,'external:stable:addon');
  const bundleFile=path.join(external,'bundle.json'),bundle=JSON.parse(fs.readFileSync(bundleFile,'utf8'));bundle.versions['0.3.3.5'].files[PAYLOAD_FILES.addon]=sha256(addon);fs.writeFileSync(bundleFile,JSON.stringify(bundle));
  return {...f,id,external,addon,bundleFile};
}

test('external source persists across restart and installs from selected files while preserving game paths',async t=>{
  const f=await externalFixture(t),before=f.service.store.read();
  const result=await f.service.selectPayloadSource(path.dirname(f.external));assert.equal(result.payload.source.mode,'external');assert.equal(result.addons[0].source,'external');
  assert.deepEqual(f.service.store.read().manualGames,before.manualGames);assert.deepEqual(f.service.store.read().gameOverrides,before.gameOverrides);
  const restarted=createAppService({userData:path.join(f.root,'user-data'),resourcesPath:path.join(f.root,'resources'),appDir:f.root,overrides:{library:f.library,installer:f.installer,detectGpu:()=>({family:'RTX40'})}});
  assert.equal((await restarted.boot()).payload.source.path,fs.realpathSync(f.external));await restarted.install(f.id,{version:'0.3.3.5'});
  assert.equal(fs.readFileSync(path.join(path.dirname(f.exe),PAYLOAD_FILES.addon),'utf8'),'external:stable:addon');
});

test('bad source selection leaves active preferences intact and disconnected source cannot silently fall back',async t=>{
  const f=await externalFixture(t);await f.service.selectPayloadSource(f.external);const before=f.service.store.read();
  await assert.rejects(f.service.selectPayloadSource(path.join(f.root,'missing')),{code:'ERR_PAYLOAD_SOURCE_MISSING'});assert.deepEqual(f.service.store.read(),before);
  const moved=path.join(f.root,'disconnected');assert.ok(f.external.startsWith(f.root+path.sep)&&moved.startsWith(f.root+path.sep));fs.renameSync(f.external,moved);
  const boot=await f.service.boot();assert.equal(boot.games.length,1);assert.equal(boot.payload.ready,false);assert.equal(boot.payload.source.error.code,'ERR_PAYLOAD_SOURCE_UNAVAILABLE');
  await assert.rejects(f.service.install(f.id),{code:'ERR_PAYLOAD_SOURCE_UNAVAILABLE'});assert.equal(fs.existsSync(path.join(path.dirname(f.exe),'dxgi.dll')),false);
  await f.service.selectPayloadSource(null);assert.equal((await f.service.boot()).payload.source.mode,'bundled');assert.equal(f.service.store.read().payloadSourcePath,null);
});

test('source manifest changes require explicit recheck and operation rejects later binary drift',async t=>{
  const f=await externalFixture(t);await f.service.selectPayloadSource(f.external);fs.appendFileSync(f.bundleFile,'\n');
  assert.equal(f.service.payloadState().payload.source.error.code,'ERR_PAYLOAD_SOURCE_CHANGED');await assert.rejects(f.service.install(f.id),{code:'ERR_PAYLOAD_SOURCE_CHANGED'});
  assert.equal((await f.service.recheckPayloadSource()).payload.ready,true);fs.appendFileSync(f.addon,'changed');
  await assert.rejects(f.service.install(f.id),{code:'ERR_PAYLOAD_HASH'});assert.equal(fs.existsSync(path.join(path.dirname(f.exe),'dxgi.dll')),false);
});

test('repair never silently replaces an installed version absent from the selected external catalog',async t=>{
  const f=await externalFixture(t);await f.service.install(f.id,{version:'0.3.3.5'});
  const bundle=JSON.parse(fs.readFileSync(f.bundleFile,'utf8'));delete bundle.versions['0.3.3.5'];bundle.defaultVersion=DX11_COMPAT_VERSION;fs.writeFileSync(f.bundleFile,JSON.stringify(bundle));
  await f.service.selectPayloadSource(f.external);await assert.rejects(f.service.repair(f.id),{code:'ERR_ADDON_NOT_FOUND'});
  await f.service.repair(f.id,{version:DX11_COMPAT_VERSION});
});

test('external-only manager starts without bundled payload and can prepare installation from a remembered source', async t => {
  const f = await externalFixture(t);
  const staging = path.join(f.root, 'saved-bundled-fixture');
  assert.ok(f.payloadDir.startsWith(f.root + path.sep) && staging.startsWith(f.root + path.sep));
  fs.renameSync(f.payloadDir, staging);
  const restarted = createAppService({ userData: path.join(f.root, 'user-data'), resourcesPath: path.join(f.root, 'resources'),
    appDir: f.root, overrides: { library: f.library, installer: f.installer, detectGpu: () => ({ family: 'RTX40' }) } });
  const initial = await restarted.boot();
  assert.equal(initial.games.length, 1);
  assert.equal(initial.payload.ready, false);
  assert.equal(initial.payload.source.mode, 'unconfigured');
  assert.equal(initial.payload.source.path, '');
  assert.equal(initial.payload.source.bundledAvailable, false);
  const selected = await restarted.selectPayloadSource(f.external);
  assert.equal(selected.payload.source.bundledAvailable, false);
  assert.equal(selected.payload.ready, true);
  await assert.rejects(restarted.selectPayloadSource(null), { code: 'ERR_PAYLOAD_MISSING' });
  assert.equal(restarted.store.read().payloadSourcePath, fs.realpathSync(f.external));
  await restarted.install(f.id, { version: '0.3.3.5' });
  assert.equal(fs.readFileSync(path.join(path.dirname(f.exe), PAYLOAD_FILES.addon), 'utf8'), 'external:stable:addon');
});

test('API override routes one game through DX12 then unified DX11 and persists', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir, { name: 'Fixture' });
  let boot = await f.service.boot();
  const id = boot.games[0].id;
  assert.equal(boot.games[0].chosen.apiResolution.api, 'mixed');
  assert.equal(boot.games[0].supportCode, 'ERR_API_SELECTION_REQUIRED');
  await assert.rejects(f.service.install(id, { version: DX11_COMPAT_VERSION }), { code: 'ERR_API_SELECTION_REQUIRED' });

  await f.service.setGameApi(id, 'dx12');
  boot = await f.service.boot();
  assert.equal(boot.games[0].apiOverride, 'dx12');
  assert.equal(boot.games[0].chosen.apiResolution.api, 'dx12');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), 'nrchain_nvngx.dll')), true);
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER)), false);

  await f.service.setGameApi(id, 'dx11');
  boot = await f.service.boot();
  assert.equal(boot.games[0].chosen.apiResolution.api, 'dx11');
  await f.service.repair(id, { version: DX11_COMPAT_VERSION });
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER)), true);

  await f.service.setGameApi(id, 'dx12');
  await f.service.repair(id, { version: DX11_COMPAT_VERSION });
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER)), false);
  await f.service.uninstall(id);
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), 'nrchain_nvngx.dll')), false);

  const fresh = createAppService({ userData: path.join(f.root, 'user-data'), resourcesPath: path.join(f.root, 'resources'), appDir: f.root, version: '0.4.5', overrides: { library: f.library, installer: f.installer, detectGpu: () => ({ family: 'RTX40', series: ['RTX 4070'], vendor: 'NVIDIA' }) } });
  // The persisted override is checked through a fresh service using the same
  // real library; no fake game state is injected into the service layer.
  const freshBoot = await fresh.boot();
  assert.equal(freshBoot.games[0].apiOverride, 'dx12');
  assert.equal(freshBoot.games[0].chosen.apiResolution.api, 'dx12');
});

test('carrier follows API automatically and the retired manual endpoint cannot override it', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  const dir = path.dirname(f.exe);
  const carrier = path.join(dir, DX11_COMPAT_CARRIER);
  const core = path.join(dir, PAYLOAD_FILES.addon), bridge = path.join(dir, PAYLOAD_FILES.bridge);
  const before = [fs.readFileSync(core), fs.readFileSync(bridge)];
  const unrelated = path.join(dir, 'another-nr-addon.addon64');
  fs.writeFileSync(unrelated, 'keep unrelated addon');
  const rows = await f.service.setGameApi(id, 'dx12');
  assert.equal(fs.existsSync(carrier), false);
  assert.deepEqual(fs.readFileSync(core), before[0]);
  assert.deepEqual(fs.readFileSync(bridge), before[1]);
  assert.equal(fs.readFileSync(unrelated, 'utf8'), 'keep unrelated addon');
  assert.equal(rows[0].components.dx11Carrier, false);
  assert.equal(rows[0].supported, true);
  assert.equal(rows[0].addonVersion, DX11_COMPAT_VERSION);
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, false);
  await assert.rejects(f.service.setGameCarrier(id, true), { code: 'ERR_BAD_REQUEST' });
  await f.service.setGameApi(id, 'dx11');
  assert.equal(fs.existsSync(carrier), true, 'saving DX11 prepares the carrier through the repair transaction');
  assert.equal(fs.existsSync(bridge), true);
});

test('historical carrier preference fields are ignored by the automatic API route', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  const key = path.resolve(f.gameDir).toLowerCase(), state = f.service.store.read();
  await f.service.store.write({ gameOverrides: { ...state.gameOverrides, [key]: { api: 'dx11', apiExecutable: f.exe, carrierEnabled: false, carrierExecutable: f.exe } } });
  const game = (await f.service.boot()).games[0]; assert.equal(game.apiOverride, 'dx11'); assert.equal(game.components.dx11Carrier, true);
  assert.equal(Object.hasOwn(f.service.store.read().gameOverrides[key], 'carrierEnabled'), false);
});

test('imported compatibility OTA follows the selected API without mixing package generations', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir, { name: 'Fixture' });
  let boot = await f.service.boot();
  const id = boot.games[0].id;
  const exeDir = path.dirname(f.exe);
  const addonFile = path.join(exeDir, PAYLOAD_FILES.addon);
  const bridgeFile = path.join(exeDir, BRIDGE);
  const carrierFile = path.join(exeDir, CARRIER);

  await f.service.setGameApi(id, 'dx12');
  await f.service.install(id, { version: '0.3.3.5' });
  assert.equal(fs.readFileSync(addonFile, 'utf8'), '0.3.3.5:addon');
  assert.equal(fs.existsSync(bridgeFile), true);
  assert.equal(fs.existsSync(carrierFile), false);

  const packageFile = zip(path.join(f.root, 'compatibility-ota.zip'), dx11Fixture());
  const versions = await f.service.importAddonFile(packageFile);
  const imported = versions.find(item => item.source === 'imported');
  assert.ok(imported);
  assert.match(imported.id, /^imported-[a-f0-9]{12}$/);
  assert.equal(imported.compatibility, 'dx11');
  assert.equal(path.basename(imported.carrierFile), CARRIER);
  assert.match(imported.addonSha256, /^[a-f0-9]{64}$/);
  assert.match(imported.bridgeSha256, /^[a-f0-9]{64}$/);
  assert.match(imported.carrierSha256, /^[a-f0-9]{64}$/);

  await f.service.upgradeAddon(id, imported.id);
  assert.equal(fs.readFileSync(addonFile, 'utf8'), 'dx11-core');
  assert.equal(fs.readFileSync(bridgeFile, 'utf8'), 'matched-bridge');
  assert.equal(fs.existsSync(carrierFile), false, 'DX12 must not deploy the packaged carrier');
  boot = await f.service.boot();
  assert.equal(boot.games[0].addonVersion, imported.id);

  await f.service.setGameApi(id, 'dx11');
  assert.equal(fs.readFileSync(carrierFile, 'utf8'), 'matched-carrier');
  boot = await f.service.boot();
  assert.equal(boot.games[0].addonVersion, imported.id);

  await f.service.setGameApi(id, 'dx12');
  assert.equal(fs.existsSync(carrierFile), false, 'saving DX12 immediately retires the managed carrier');
  assert.equal((await f.service.diagnose(id)).routeMismatch, false);
  await f.service.repair(id, { version: imported.id });
  assert.equal(fs.existsSync(carrierFile), false, 'DX12 repair retires the managed carrier');
  assert.equal(fs.readFileSync(addonFile, 'utf8'), 'dx11-core');
  assert.equal(fs.readFileSync(bridgeFile, 'utf8'), 'matched-bridge');

  const fresh = createAppService({
    userData: path.join(f.root, 'user-data'), resourcesPath: path.join(f.root, 'resources'),
    appDir: f.root, version: '0.4.5',
    overrides: { library: f.library, installer: f.installer, detectGpu: () => ({ family: 'RTX40', series: ['RTX 4070'], vendor: 'NVIDIA' }) }
  });
  const persisted = fresh.listAddonVersions().find(item => item.id === imported.id);
  assert.equal(persisted.compatibility, 'dx11');
  assert.equal(persisted.otaManifest.version, 'beta0.4.5-dx11-compat');
  assert.equal(persisted.otaManifest.sourceCommit, 'd'.repeat(40));
  assert.equal(persisted.otaManifest.includesDx11, true);
  assert.equal(persisted.addonSha256, imported.addonSha256);
  assert.equal(persisted.bridgeSha256, imported.bridgeSha256);
  assert.equal(persisted.carrierSha256, imported.carrierSha256);
  boot = await fresh.boot();
  assert.equal(boot.games[0].addonVersion, imported.id);

  const importedCarrier = path.join(f.root, 'user-data', 'addon-versions', imported.id, CARRIER);
  fs.writeFileSync(importedCarrier, 'tampered-carrier-source');
  const before = { addon: fs.readFileSync(addonFile), bridge: fs.readFileSync(bridgeFile) };
  await assert.rejects(fresh.setGameApi(id, 'dx11'), error =>
    error.code === 'ERR_ADDON_INVALID' && error.details && error.details.reason === 'hash');
  assert.deepEqual(fs.readFileSync(addonFile), before.addon);
  assert.deepEqual(fs.readFileSync(bridgeFile), before.bridge);
  assert.equal(fs.existsSync(carrierFile), false, 'failed validation cannot change the game carrier');
  assert.equal(imported.label, '0.4.5-DX11-兼容增强');
  assert.equal(persisted.label, '0.4.5-DX11-兼容增强');
});


test('failed DX12 transition restores API preference and leaves the changed carrier intact', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  const carrier = path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER);
  fs.writeFileSync(carrier, 'user edit');
  await assert.rejects(f.service.setGameApi(id, 'dx12'), { code: 'ERR_FILE_CHANGED' });
  assert.equal((await f.service.boot()).games[0].apiOverride, 'dx11');
  assert.equal(fs.readFileSync(carrier, 'utf8'), 'user edit');
});

test('auto returns to mixed detection and retires a managed DX11 carrier', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  await f.service.setGameApi(id, 'auto');
  const game = (await f.service.boot()).games[0]; assert.equal(game.apiOverride, 'auto'); assert.equal(game.chosen.detectedApi, 'mixed');
  assert.equal(game.supportCode, 'ERR_API_SELECTION_REQUIRED'); assert.equal(game.components.dx11Carrier, false);
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER)), false);
});

test('an old manifest manual-off flag cannot suppress a later DX11 selection', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  await f.installer.disableCarrier({ gameDir: f.gameDir, scan: f.service.gameScan(id), manual: true });
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, true, 'UI state derives from API rather than old manual-off metadata');
  await f.service.setGameApi(id, 'dx12');
  await f.service.setGameApi(id, 'dx11');
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, true);
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER)), true);
});

test('all API choices persist per EXE while detected API remains independent', async t => {
  const f = makeService(t); await f.service.addManualGame(f.gameDir); const id = (await f.service.boot()).games[0].id;
  for (const api of ['dx9', 'dx10', 'dx11', 'dx12', 'vulkan', 'opengl']) {
    await f.service.setGameApi(id, api); const game = (await f.service.boot()).games[0];
    assert.equal(game.apiOverride, api); assert.equal(game.chosen.apiResolution.api, api); assert.equal(game.chosen.detectedApi, 'mixed');
    if (!['dx11', 'dx12'].includes(api)) assert.equal(game.supportCode, 'ERR_UNSUPPORTED_API');
  }
  await f.service.setGameApi(id, 'auto'); const automatic = (await f.service.boot()).games[0];
  assert.equal(automatic.apiOverride, 'auto'); assert.equal(automatic.chosen.apiResolution.api, 'mixed'); assert.equal(automatic.supportCode, 'ERR_API_SELECTION_REQUIRED');
});

test('implicit superseded core selection repairs to replacement but explicit old request is rejected', async t => {
  const f = makeService(t); await f.service.addManualGame(f.gameDir); let game = (await f.service.boot()).games[0]; const id = game.id;
  await f.service.setGameApi(id, 'dx12'); await f.service.install(id, { version: '0.3.3.5' });
  const manifestFile = path.join(f.gameDir, '_DLSS5_Backup', 'xiaofeng-manager.json');
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8')); manifest.payloadVersion = '0.3.3.4'; fs.writeFileSync(manifestFile, JSON.stringify(manifest));
  const bundleFile = path.join(f.payloadDir, 'bundle.json'), bundle = JSON.parse(fs.readFileSync(bundleFile, 'utf8'));
  bundle.supersededVersions = { '0.3.3.4': '0.3.3.5' }; fs.writeFileSync(bundleFile, JSON.stringify(bundle)); await f.service.refresh();
  const repaired = await f.service.repair(id); assert.deepEqual(repaired.payloadReplacement, { from: '0.3.3.4', to: '0.3.3.5', reason: 'superseded' });
  assert.equal(JSON.parse(fs.readFileSync(manifestFile, 'utf8')).payloadVersion, '0.3.3.5');
  manifest.payloadVersion = '0.3.3.4'; fs.writeFileSync(manifestFile, JSON.stringify(manifest)); await f.service.refresh();
  await assert.rejects(f.service.repair(id, { version: '0.3.3.4' }), error => error.code === 'ERR_ADDON_NOT_FOUND' && error.details?.replacementVersion === '0.3.3.5');
  assert.equal(JSON.parse(fs.readFileSync(manifestFile, 'utf8')).payloadVersion, '0.3.3.4');
});
