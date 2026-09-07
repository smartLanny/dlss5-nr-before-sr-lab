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

test('independent carrier choice disables only the carrier and requires explicit re-enable for DX11', async t => {
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
  const rows = await f.service.setGameCarrier(id, false);
  assert.equal(fs.existsSync(carrier), false);
  assert.deepEqual(fs.readFileSync(core), before[0]);
  assert.deepEqual(fs.readFileSync(bridge), before[1]);
  assert.equal(fs.readFileSync(unrelated, 'utf8'), 'keep unrelated addon');
  assert.equal(rows[0].components.dx11Carrier, false);
  assert.equal(rows[0].supportCode, 'ERR_CARRIER_NOT_SELECTED');
  assert.equal(rows[0].addonVersion, DX11_COMPAT_VERSION);
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, false);
  await assert.rejects(f.service.repair(id, { version: DX11_COMPAT_VERSION }), { code: 'ERR_CARRIER_NOT_SELECTED' });
  await f.service.setGameCarrier(id, true);
  assert.equal(fs.existsSync(carrier), true, 'applying auto uses the existing repair transaction');
  await f.service.repair(id, { version: DX11_COMPAT_VERSION });
  assert.equal(fs.existsSync(carrier), true);
  await f.service.setGameApi(id, 'dx12');
  await assert.rejects(f.service.setGameCarrier(id, true), { code: 'ERR_UNSUPPORTED_API' });
  await f.service.repair(id, { version: DX11_COMPAT_VERSION });
  assert.equal(fs.existsSync(carrier), false);
  assert.equal(fs.existsSync(bridge), true);
});

test('failed carrier disable preserves game files and restores the prior checkbox preference', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  const carrier = path.join(path.dirname(f.exe), DX11_COMPAT_CARRIER);
  fs.writeFileSync(carrier, 'user changed this carrier');
  await assert.rejects(f.service.setGameCarrier(id, false), { code: 'ERR_FILE_CHANGED' });
  assert.equal(fs.readFileSync(carrier, 'utf8'), 'user changed this carrier');
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, true);
  assert.equal(fs.existsSync(path.join(path.dirname(f.exe), PAYLOAD_FILES.bridge)), true);
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
  await assert.rejects(f.service.launch(id), { code: 'ERR_API_ROUTE_PENDING' });
  await f.service.repair(id, { version: imported.id });
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
  await fresh.setGameApi(id, 'dx11');
  await assert.rejects(fresh.launch(id), { code: 'ERR_API_ROUTE_PENDING' });
  await assert.rejects(fresh.repair(id, { version: imported.id }), error =>
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

test('enabling auto with a broken package rolls back the saved off preference', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  await f.service.setGameCarrier(id, false);
  const dir = path.dirname(f.exe), core = fs.readFileSync(path.join(dir, PAYLOAD_FILES.addon));
  fs.writeFileSync(path.join(f.payloadDir, 'versions', DX11_COMPAT_VERSION, DX11_COMPAT_CARRIER), 'tampered');
  await assert.rejects(f.service.setGameCarrier(id, true), { code: 'ERR_PAYLOAD_HASH' });
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, false);
  assert.equal(fs.existsSync(path.join(dir, DX11_COMPAT_CARRIER)), false);
  assert.deepEqual(fs.readFileSync(path.join(dir, PAYLOAD_FILES.addon)), core);
});

test('DX12 retirement preserves manual-off across a later DX11 selection', async t => {
  const f = makeService(t);
  await f.service.addManualGame(f.gameDir);
  const id = (await f.service.boot()).games[0].id;
  await f.service.setGameApi(id, 'dx11');
  await f.service.install(id, { version: DX11_COMPAT_VERSION });
  await f.service.setGameCarrier(id, false);
  await f.service.setGameApi(id, 'dx12');
  await f.service.setGameApi(id, 'dx11');
  assert.equal((await f.service.boot()).games[0].components.dx11Carrier, false);
  assert.equal((await f.service.diagnose(id)).complete, false);
});
