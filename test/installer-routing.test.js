'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');
const { createInstaller } = require('../src/product/installer');
const journal = require('../src/core/file-journal');
const { sha256 } = require('../src/product/payload');
const { PAYLOAD_FILES, INSTALLED_NAMES, DX11_COMPAT_VERSION, DX11_COMPAT_CARRIER } = require('../src/product/constants');
const { manifestPath } = require('../src/product/manifest');

function fixture(t, api = 'dx12') {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-route-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const gameDir = path.join(root, 'Game');
  const exeDir = path.join(gameDir, 'bin');
  const payloadDir = path.join(root, 'payload');
  fs.mkdirSync(exeDir, { recursive: true });
  fs.mkdirSync(payloadDir, { recursive: true });
  const exePath = path.join(exeDir, 'game.exe');
  fs.writeFileSync(exePath, 'game');
  const payload = {};
  for (const [kind, name] of Object.entries(PAYLOAD_FILES)) {
    const file = path.join(payloadDir, name);
    fs.writeFileSync(file, kind === 'reshade' ? 'PE64 ReShade Searching for add-ons' : `payload-${kind}`);
    payload[kind] = { file, name, actual: sha256(file) };
  }
  const carrierFile = path.join(payloadDir, DX11_COMPAT_CARRIER);
  fs.writeFileSync(carrierFile, 'payload-carrier');
  payload.carrier = { file: carrierFile, name: DX11_COMPAT_CARRIER, actual: sha256(carrierFile) };
  payload.version = DX11_COMPAT_VERSION;
  payload.versionInfo = { compatibility: 'dx11' };
  const scan = {
    chosen: {
      path: exePath, rel: 'bin/game.exe', bitness: 64, api: 'dxgi',
      apiLabel: api === 'dx11' ? 'DirectX 11' : 'DirectX 12',
      apiResolution: { api, source: 'test', evidence: [] }, emulator: null
    },
    primaryDlss: { name: 'nvngx_dlss.dll' }, emulator: null
  };
  const scanModule = {
    async scanGame() { return scan; },
    inspectReShade(dir) {
      const file = path.join(dir, 'dxgi.dll');
      return fs.existsSync(file)
        ? { installed: true, addonSupport: fs.readFileSync(file).includes('Searching for add-ons'), file: 'dxgi.dll' }
        : { installed: false, addonSupport: false, file: null };
    }
  };
  const installer = createInstaller({
    journal,
    scan: scanModule,
    guards: { antiCheatPresent: () => false, assertGameClosed: async () => {} },
    pe: { getBitness: () => 64 }
  });
  return { root, gameDir, exeDir, exePath, payload, scan, installer };
}

test('DX12 keeps nrchain, never deploys carrier, and restores an unmanaged old carrier on uninstall', async t => {
  const f = fixture(t, 'dx12');
  const oldCarrier = path.join(f.exeDir, 'r3-nr-native-neutral.addon64');
  fs.writeFileSync(oldCarrier, 'user old carrier');

  const result = await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(result.complete, true);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.bridge)), true);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.carrier)), false);
  assert.equal(fs.existsSync(oldCarrier), false);

  const removed = await f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan });
  assert.equal(removed.removed, true);
  assert.equal(fs.readFileSync(oldCarrier, 'utf8'), 'user old carrier');
});

test('DX11 deploys the matched carrier and restores the prior carrier on uninstall', async t => {
  const f = fixture(t, 'dx11');
  const target = path.join(f.exeDir, INSTALLED_NAMES.carrier);
  fs.writeFileSync(target, 'prior carrier');
  const reshadeIni = path.join(f.exeDir, 'ReShade.ini');
  fs.writeFileSync(reshadeIni,
    `[ADDON]\r\nDisabledAddons=@${INSTALLED_NAMES.carrier},@user,,name.addon64\r\n` +
    `[OTHER]\r\nDisabledAddons=@${INSTALLED_NAMES.carrier}\r\n` +
    `[ADDON]\r\nDisabledAddons=@keep.addon64,@${INSTALLED_NAMES.carrier.toUpperCase()}\r\n`);

  const result = await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(result.complete, true);
  assert.equal(fs.readFileSync(target, 'utf8'), 'payload-carrier');
  const enabledIni = fs.readFileSync(reshadeIni, 'utf8');
  assert.match(fs.readFileSync(`${reshadeIni}.bak`, 'utf8'), new RegExp(INSTALLED_NAMES.carrier, 'i'));
  assert.match(enabledIni, /DisabledAddons=@user,,name\.addon64/);
  assert.match(enabledIni, /\[OTHER\]\r\nDisabledAddons=@dlss5-native-carrier/i);
  assert.match(enabledIni, /DisabledAddons=@keep\.addon64/);
  assert.equal((enabledIni.match(new RegExp(`@${INSTALLED_NAMES.carrier.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}`, 'ig')) || []).length, 1,
    'only the exact token outside ADDON sections remains');

  fs.writeFileSync(reshadeIni, `[ADDON]\nDisabledAddons=@${INSTALLED_NAMES.carrier}\n`);
  const disabled = await f.installer.diagnose({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(disabled.complete, false);
  assert.match(disabled.components.find(row => row.key === 'carrier').detail, /禁用/);
  await f.installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const removed = await f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan });
  assert.equal(removed.removed, true);
  assert.equal(fs.readFileSync(target, 'utf8'), 'prior carrier');
});

test('DX11 clears exact bare and named carrier exclusions using ReShade section rules', async t => {
  const f = fixture(t, 'dx12');
  const reshadeIni = path.join(f.exeDir, 'ReShade.ini');
  const dx12 = await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(dx12.complete, true);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.bridge)), true);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.carrier)), false);
  f.scan.chosen.apiLabel = 'DirectX 11';
  f.scan.chosen.apiResolution = { api: 'dx11', source: 'manual', evidence: [] };
  const original = Buffer.from(
    `\uFEFF[ADDON] accepted trailing text\r\nDisabledAddons=@core.addon64,@${INSTALLED_NAMES.carrier},用户,,条目\r\n` +
    `[ADDON]\r\nDisabledAddons=用户命名@${INSTALLED_NAMES.carrier},name-only,@keep.addon64\r\n` +
    `LoadFromDllMain=manual-third-party.addon64\r\n`, 'utf8');
  fs.writeFileSync(reshadeIni, original);

  const result = await f.installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(result.complete, true);
  const enabledIni = fs.readFileSync(reshadeIni, 'utf8');
  assert.doesNotMatch(enabledIni, /用户命名@/);
  assert.doesNotMatch(enabledIni, new RegExp(INSTALLED_NAMES.carrier.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'));
  assert.match(enabledIni, /DisabledAddons=@core\.addon64,用户,,条目/);
  assert.match(enabledIni, /DisabledAddons=name-only,@keep\.addon64/);
  assert.match(enabledIni, /LoadFromDllMain=manual-third-party\.addon64/);
  assert.equal(enabledIni.charCodeAt(0), 0xFEFF, 'UTF-8 BOM is preserved');
  assert.equal(fs.readFileSync(`${reshadeIni}.bak`).equals(original), true, 'Manager keeps its own exact INI backup');
});

test('real PR160 receipt refuses restore after Manager changes its applied INI', {
  skip: !process.env.PR160_CARRIER_TOOL
}, async t => {
  const f = fixture(t, 'dx12');
  const tool = path.resolve(process.env.PR160_CARRIER_TOOL);
  const python = process.env.PYTHON || 'python';
  const runTool = args => spawnSync(python, [tool, ...args], {
    encoding: 'utf8', env: { ...process.env, PYTHONIOENCODING: 'utf-8' }
  });
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const reshadeIni = path.join(f.exeDir, 'ReShade.ini');
  fs.copyFileSync(f.payload.carrier.file, path.join(f.exeDir, INSTALLED_NAMES.carrier));
  fs.writeFileSync(reshadeIni, '[ADDON] trailing text\r\nAddonPath=.\r\nDisabledAddons=Custom,,Name,@keep.addon64\r\n');
  const beforeApply = fs.readFileSync(reshadeIni);

  const appliedRun = runTool(['--ini', reshadeIni, '--api', 'd3d12', '--apply']);
  assert.equal(appliedRun.status, 0, appliedRun.stderr || appliedRun.stdout);
  const applied = JSON.parse(appliedRun.stdout);
  assert.equal(applied.status, 'applied');
  assert.equal(applied.schema, 1);
  const receiptFile = path.join(f.exeDir, applied.receipt);
  const receipt = JSON.parse(fs.readFileSync(receiptFile, 'utf8'));
  assert.equal(receipt.operation, 'carrier-exclusion');
  assert.equal(receipt.afterSha256, sha256(reshadeIni));
  const afterApply = fs.readFileSync(reshadeIni);

  f.scan.chosen.apiLabel = 'DirectX 11';
  f.scan.chosen.apiResolution = { api: 'dx11', source: 'manual', evidence: [] };
  const repaired = await f.installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(repaired.complete, true);
  assert.match(fs.readFileSync(reshadeIni, 'utf8'), /AddonPath=\./);
  assert.equal(fs.existsSync(receiptFile), true);
  assert.equal(fs.existsSync(path.join(f.exeDir, applied.backup)), true);

  const restoreRun = runTool(['--ini', reshadeIni, '--restore', applied.receipt]);
  assert.equal(restoreRun.status, 2, restoreRun.stderr || restoreRun.stdout);
  const refused = JSON.parse(restoreRun.stdout);
  assert.equal(refused.status, 'error');
  assert.equal(refused.error, 'current-hash-mismatch');

  if (process.env.MANAGER_INTEROP_EVIDENCE_DIR) {
    const evidenceDir = path.resolve(process.env.MANAGER_INTEROP_EVIDENCE_DIR);
    fs.mkdirSync(evidenceDir, { recursive: true });
    fs.writeFileSync(path.join(evidenceDir, '01-before-pr160.ini'), beforeApply);
    fs.writeFileSync(path.join(evidenceDir, '02-after-pr160.ini'), afterApply);
    fs.copyFileSync(reshadeIni, path.join(evidenceDir, '03-after-manager-dx11-repair.ini'));
    fs.copyFileSync(receiptFile, path.join(evidenceDir, path.basename(receiptFile)));
    fs.copyFileSync(path.join(f.exeDir, applied.backup), path.join(evidenceDir, applied.backup));
    fs.copyFileSync(`${reshadeIni}.bak`, path.join(evidenceDir, 'ReShade.ini.manager.bak'));
    fs.writeFileSync(path.join(evidenceDir, 'evidence.json'), `${JSON.stringify({
      managerBase: '8acf31f9cd6245a32256f28accbb2c0a49bc5386',
      pr160Tool: tool,
      addonPath: '.',
      carrier: INSTALLED_NAMES.carrier,
      beforePr160Sha256: sha256(path.join(evidenceDir, '01-before-pr160.ini')),
      afterPr160Sha256: sha256(path.join(evidenceDir, '02-after-pr160.ini')),
      afterManagerSha256: sha256(path.join(evidenceDir, '03-after-manager-dx11-repair.ini')),
      restoreError: refused.error
    }, null, 2)}\n`);
  }
});

test('repairing a DX11 install on a resolved DX12 route retires carrier but retains nrchain', async t => {
  const f = fixture(t, 'dx11');
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const bridge = path.join(f.exeDir, INSTALLED_NAMES.bridge);
  f.scan.chosen.apiLabel = 'DirectX 12';
  f.scan.chosen.apiResolution = { api: 'dx12', source: 'manual', evidence: [] };

  const beforeRepair = await f.installer.diagnose({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(beforeRepair.routeMismatch, true);
  assert.equal(beforeRepair.deploymentApi, 'dx11');
  assert.equal(beforeRepair.selectedApi, 'dx12');
  assert.equal(beforeRepair.complete, false);
  const repaired = await f.installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(repaired.complete, true);
  assert.equal(repaired.routeMismatch, false);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.carrier)), false);
  assert.equal(fs.readFileSync(bridge, 'utf8'), 'payload-bridge');
});

test('addon upgrades apply carrier only to a complete DX11 set and preserve DX12 nrchain', async t => {
  const f = fixture(t, 'dx12');
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const bridge = path.join(f.exeDir, INSTALLED_NAMES.bridge);
  const originalBridge = fs.readFileSync(bridge, 'utf8');
  const addonFile = path.join(f.root, 'upgrade.addon64');
  const carrierFile = path.join(f.root, 'upgrade-carrier.addon64');
  const bridgeFile = path.join(f.root, 'upgrade-bridge.dll');
  fs.writeFileSync(addonFile, 'upgrade-addon');
  fs.writeFileSync(carrierFile, 'upgrade-carrier');
  fs.writeFileSync(bridgeFile, 'upgrade-bridge');

  await f.installer.upgradeAddon({
    gameDir: f.gameDir,
    addon: {
      file: addonFile, carrierFile, compatibility: 'dx11',
      addonSha256: sha256(addonFile), carrierSha256: sha256(carrierFile)
    },
    version: 'dx12-upgrade', scan: f.scan
  });
  assert.equal(fs.readFileSync(bridge, 'utf8'), originalBridge);
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.carrier)), false);

  f.scan.chosen.apiLabel = 'DirectX 11';
  f.scan.chosen.apiResolution = { api: 'dx11', source: 'manual', evidence: [] };
  await assert.rejects(f.installer.upgradeAddon({
    gameDir: f.gameDir,
    addon: { file: addonFile, compatibility: 'dx11' },
    version: 'incomplete-dx11', scan: f.scan
  }), { code: 'ERR_PAYLOAD_MISSING' });
  await assert.rejects(f.installer.upgradeAddon({
    gameDir: f.gameDir,
    addon: {
      file: addonFile, bridgeFile, carrierFile, compatibility: 'dx11',
      addonSha256: '0'.repeat(64), bridgeSha256: sha256(bridgeFile), carrierSha256: sha256(carrierFile)
    },
    version: 'bad-hash', scan: f.scan
  }), { code: 'ERR_ADDON_INVALID' });
  await f.installer.upgradeAddon({
    gameDir: f.gameDir,
    addon: {
      file: addonFile, bridgeFile, carrierFile, compatibility: 'dx11',
      addonSha256: sha256(addonFile), bridgeSha256: sha256(bridgeFile), carrierSha256: sha256(carrierFile)
    },
    version: 'complete-dx11', scan: f.scan
  });
  assert.equal(fs.readFileSync(bridge, 'utf8'), 'upgrade-bridge');
  assert.equal(fs.readFileSync(path.join(f.exeDir, INSTALLED_NAMES.carrier), 'utf8'), 'upgrade-carrier');
});

test('changed managed carrier is not overwritten by repair or uninstall', async t => {
  const f = fixture(t, 'dx11');
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const carrier = path.join(f.exeDir, INSTALLED_NAMES.carrier);
  fs.writeFileSync(carrier, 'user changed carrier');

  await assert.rejects(f.installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan }), { code: 'ERR_FILE_CHANGED' });
  assert.equal(fs.readFileSync(carrier, 'utf8'), 'user changed carrier');
  assert.equal(fs.existsSync(path.join(f.exeDir, `${PAYLOAD_FILES.addon}.bak`)), false, 'failed repair rolls back sidecar backups');

  const removed = await f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan });
  assert.equal(removed.removed, false);
  assert.equal(removed.warnings.some(row => row.rel.endsWith(INSTALLED_NAMES.carrier)), true);
  assert.equal(fs.readFileSync(carrier, 'utf8'), 'user changed carrier');
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.bridge)), true, 'uninstall preflight prevents partial removal');
});

test('real journal restores earlier conflict moves and managed writes after a later install failure', async t => {
  const f = fixture(t, 'dx12');
  const conflict = path.join(f.exeDir, 'old-nr-before-sr.addon64');
  fs.writeFileSync(conflict, 'old conflict');
  f.payload.addon.file = path.join(f.root, 'missing.addon64');

  await assert.rejects(f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan }), { code: 'ENOENT' });
  assert.equal(fs.readFileSync(conflict, 'utf8'), 'old conflict');
  assert.equal(fs.existsSync(path.join(f.exeDir, INSTALLED_NAMES.reshade)), false);
  assert.equal(fs.existsSync(manifestPath(f.gameDir)), false);
  assert.equal(fs.existsSync(journal.pendingPath(f.gameDir)), false);
});

test('late uninstall commit failure restores install state and retains persistent original backups', async t => {
  const f = fixture(t, 'dx12');
  const runtime = path.join(f.exeDir, INSTALLED_NAMES.runtime);
  fs.writeFileSync(runtime, 'original runtime');
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const manifest = JSON.parse(fs.readFileSync(manifestPath(f.gameDir), 'utf8'));
  const runtimeEntry = manifest.files.find(row => row.kind === 'runtime');
  const originalBackup = path.join(f.gameDir, runtimeEntry.original.backupRel);
  assert.equal(fs.readFileSync(originalBackup, 'utf8'), 'original runtime');

  const realUnlink = fs.promises.unlink;
  let injected = false;
  fs.promises.unlink = async file => {
    if (!injected && path.resolve(file) === path.resolve(journal.pendingPath(f.gameDir))) {
      injected = true;
      throw new Error('injected late commit failure');
    }
    return realUnlink.call(fs.promises, file);
  };
  try {
    await assert.rejects(f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan }), /injected late commit failure/);
  } finally {
    fs.promises.unlink = realUnlink;
  }
  assert.equal(fs.existsSync(manifestPath(f.gameDir)), true);
  assert.equal(fs.readFileSync(runtime, 'utf8'), 'payload-runtime');
  assert.equal(fs.readFileSync(originalBackup, 'utf8'), 'original runtime');
  assert.equal(fs.existsSync(journal.pendingPath(f.gameDir)), false);
});

test('corrupt original or conflict backups stop uninstall before altering the installation', async t => {
  const f = fixture(t, 'dx12');
  const runtime = path.join(f.exeDir, INSTALLED_NAMES.runtime);
  const oldCarrier = path.join(f.exeDir, 'r3-nr-native-neutral.addon64');
  fs.writeFileSync(runtime, 'original runtime'); fs.writeFileSync(oldCarrier, 'original carrier');
  await f.installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  const manifest = JSON.parse(fs.readFileSync(manifestPath(f.gameDir), 'utf8'));
  const backup = path.join(f.gameDir, manifest.files.find(row => row.kind === 'runtime').original.backupRel);
  fs.writeFileSync(backup, 'corrupt');
  await assert.rejects(f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan }), { code: 'ERR_BACKUP_INVALID' });
  assert.equal(fs.readFileSync(runtime, 'utf8'), 'payload-runtime');
  fs.writeFileSync(backup, 'original runtime');
  fs.writeFileSync(path.join(f.gameDir, manifest.conflicts[0].backupRel), 'corrupt conflict');
  await assert.rejects(f.installer.uninstall({ gameDir: f.gameDir, scan: f.scan }), { code: 'ERR_BACKUP_INVALID' });
  assert.equal(fs.existsSync(oldCarrier), false);
  assert.equal(fs.readFileSync(runtime, 'utf8'), 'payload-runtime');
});
