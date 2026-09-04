'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { createInstaller } = require('../src/product/installer');
const { createBundle, inspectPayload } = require('../src/product/payload');
const { PAYLOAD_FILES } = require('../src/product/constants');

function makeJournal() {
  return {
    safePath(root, rel) {
      const target = path.resolve(root, rel);
      if (!path.relative(root, target) || path.relative(root, target).startsWith('..')) throw Error('unsafe');
      return target;
    },
    async capture() {},
    async atomicJson(file, data) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, JSON.stringify(data, null, 2)); },
    async transaction(_dir, work) { return work(); }
  };
}

function fixture() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-install-'));
  const gameDir = path.join(root, 'Game');
  const exeDir = path.join(gameDir, 'bin');
  const exePath = path.join(exeDir, 'game.exe');
  const payloadDir = path.join(root, 'payload');
  fs.mkdirSync(exeDir, { recursive: true });
  fs.mkdirSync(payloadDir, { recursive: true });
  fs.writeFileSync(exePath, 'exe');
  for (const name of Object.values(PAYLOAD_FILES)) {
    let body = name;
    if (name === PAYLOAD_FILES.reshade) body = 'PE64 ReShade Searching for add-ons';
    fs.writeFileSync(path.join(payloadDir, name), body);
  }
  fs.writeFileSync(path.join(payloadDir, 'bundle.json'), JSON.stringify(createBundle(payloadDir)));
  const payload = Object.fromEntries(inspectPayload(payloadDir).files.map(row => [row.kind, row]));
  const scan = {
    chosen: { path: exePath, rel: 'bin/game.exe', bitness: 64, api: 'dxgi', apiLabel: 'DirectX 12', emulator: null },
    primaryDlss: { name: 'nvngx_dlss.dll' }, emulator: null
  };
  return { root, gameDir, exeDir, exePath, payload, scan };
}

test('installs, diagnoses, repairs and uninstalls without deleting kept settings', async () => {
  const f = fixture();
  const scanModule = {
    async scanGame() { return f.scan; },
    inspectReShade(dir) {
      const file = path.join(dir, 'dxgi.dll');
      return fs.existsSync(file)
        ? { installed: true, addonSupport: fs.readFileSync(file).includes('Searching for add-ons'), file: 'dxgi.dll' }
        : { installed: false, addonSupport: false, file: null };
    }
  };
  const installer = createInstaller({
    journal: makeJournal(),
    scan: scanModule,
    guards: { antiCheatPresent: () => false, assertGameClosed: async () => {} },
    pe: { getBitness: () => 64 }
  });
  const installed = await installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(installed.complete, true);
  const ini = path.join(f.exeDir, PAYLOAD_FILES.config);
  fs.appendFileSync(ini, '\n; user setting');
  fs.unlinkSync(path.join(f.exeDir, PAYLOAD_FILES.bridge));
  assert.equal((await installer.diagnose({ gameDir: f.gameDir, payload: f.payload, scan: f.scan })).complete, false);
  assert.equal((await installer.repair({ gameDir: f.gameDir, payload: f.payload, scan: f.scan })).complete, true);
  const removed = await installer.uninstall({ gameDir: f.gameDir, scan: f.scan });
  assert.equal(removed.removed, true);
  assert.equal(fs.existsSync(ini), true, 'user settings are kept by default');
  assert.equal(fs.existsSync(path.join(f.exeDir, PAYLOAD_FILES.addon)), false);
});

test('refuses to overwrite an unknown dxgi.dll', async () => {
  const f = fixture();
  fs.writeFileSync(path.join(f.exeDir, 'dxgi.dll'), 'some unrelated proxy');
  const installer = createInstaller({
    journal: makeJournal(),
    scan: {
      async scanGame() { return f.scan; },
      inspectReShade() { return { installed: false, addonSupport: false, file: null }; }
    },
    guards: { antiCheatPresent: () => false, assertGameClosed: async () => {} },
    pe: { getBitness: () => 64 }
  });
  await assert.rejects(
    installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan }),
    error => error && error.code === 'ERR_RESHADER_CONFLICT'
  );
  assert.equal(fs.readFileSync(path.join(f.exeDir, 'dxgi.dll'), 'utf8'), 'some unrelated proxy');
});

test('keeps an existing add-on ReShade and restores an overwritten runtime', async () => {
  const f = fixture();
  const reshadePath = path.join(f.exeDir, 'dxgi.dll');
  const runtimePath = path.join(f.exeDir, PAYLOAD_FILES.runtime);
  fs.writeFileSync(reshadePath, 'existing ReShade Searching for add-ons');
  fs.writeFileSync(runtimePath, 'original runtime');
  const scanModule = {
    async scanGame() { return f.scan; },
    inspectReShade(dir) {
      const file = path.join(dir, 'dxgi.dll');
      return fs.existsSync(file) && fs.readFileSync(file).includes('Searching for add-ons')
        ? { installed: true, addonSupport: true, file: 'dxgi.dll' }
        : { installed: false, addonSupport: false, file: null };
    }
  };
  const installer = createInstaller({
    journal: makeJournal(),
    scan: scanModule,
    guards: { antiCheatPresent: () => false, assertGameClosed: async () => {} },
    pe: { getBitness: () => 64 }
  });
  await installer.install({ gameDir: f.gameDir, payload: f.payload, scan: f.scan });
  assert.equal(fs.readFileSync(reshadePath, 'utf8'), 'existing ReShade Searching for add-ons');
  assert.equal(fs.readFileSync(runtimePath, 'utf8'), PAYLOAD_FILES.runtime);
  const removed = await installer.uninstall({ gameDir: f.gameDir, scan: f.scan });
  assert.equal(removed.removed, true);
  assert.equal(fs.readFileSync(reshadePath, 'utf8'), 'existing ReShade Searching for add-ons');
  assert.equal(fs.readFileSync(runtimePath, 'utf8'), 'original runtime');
});
