'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { createLegacyRuntime } = require('../src/product/legacy-runtime');
const { fingerprint } = require('../src/product/feeder-runtime');
const runtime = createLegacyRuntime({ appDir: path.resolve(__dirname, '..') });

test('legacy host recipes keep the x86 provider separate from x64 Core and runtime', () => {
  const pkg = runtime.load({ api: 'dx11', architecture: 'x86', hardwareFamily: 'RTX40' });
  assert.equal(pkg.recipe.schema, 2); assert.equal(pkg.recipe.hostRequired, true);
  const file = role => pkg.recipe.files.find(row => row.role === role);
  assert.equal(file('provider').architecture, 'x86'); assert.equal(file('provider').target, 'dlss5-feed.addon32');
  assert.equal(file('core').architecture, 'x64'); assert.ok(file('core').target.startsWith('host64/addons/'));
  assert.equal(file('nr-runtime').target, 'host64/addons/nvngx_dlssnr.dll');
  assert.equal(pkg.recipe.srInjected, false); assert.equal(pkg.recipe.fgInjected, false);
});

test('both D3D10 architectures use the shared-texture host route without an external wrapper', () => {
  for (const architecture of ['x86', 'x64']) {
    const pkg = runtime.load({ api: 'dx10', architecture, hardwareFamily: 'RTX50' });
    assert.equal(pkg.recipe.hostRequired, true); assert.equal(pkg.recipe.wrapper, null);
    assert.equal(pkg.recipe.files.find(row => row.role === 'provider').id, architecture === 'x86' ? 'provider-x86' : 'provider-relay-x64');
  }
});

test('HoYo package delegates its loader and keeps runtime choices independently pinned', () => {
  const load = hardwareFamily => runtime.load({ api: 'dx12', architecture: 'x64', hardwareFamily, loadingBackend: 'hoyoshade' });
  const a = load('RTX40'), b = load('RTX50');
  assert.ok(a.recipe.files.every(row => row.role !== 'game-loader'));
  assert.equal(a.recipe.files.find(row => row.role === 'core').sha256, b.recipe.files.find(row => row.role === 'core').sha256);
  assert.notEqual(a.recipe.files.find(row => row.role === 'nr-runtime').sha256, b.recipe.files.find(row => row.role === 'nr-runtime').sha256);
  assert.equal(a.recipe.coreVersion, b.recipe.coreVersion); assert.equal(a.recipe.coreVariant.genericCoreInterchangeable, false);
});

test('a receipt cannot replace the fixed Core or downgrade its variant identity', () => {
  const pkg = runtime.load({ api: 'dx12', architecture: 'x64', hardwareFamily: 'RTX50' });
  assert.equal(fingerprint(runtime.validate(pkg.recipe)), pkg.fingerprint);
  const changed = structuredClone(pkg.recipe); changed.files.find(row => row.role === 'core').sha256 = 'a'.repeat(64);
  assert.throws(() => runtime.validate(changed), { code: 'LEGACY_RECEIPT_INVALID' });
  const downgraded = structuredClone(pkg.recipe); downgraded.coreVariant.requiredInterface = null;
  assert.throws(() => runtime.validate(downgraded), { code: 'LEGACY_RECEIPT_INVALID' });
});

test('DX9 uses only the source-owned shim and Windows runtime, with an x64 host for both architectures', () => {
  for (const architecture of ['x86', 'x64']) {
    const recipe = runtime.load({ api: 'dx9', architecture, hardwareFamily: 'RTX50' }).recipe;
    assert.equal(recipe.hostRequired, true); assert.equal(recipe.wrapper.privateRuntimeBundled, false);
    assert.equal(recipe.files.find(row => row.role === 'api-wrapper').target, 'd3d9.dll');
    assert.ok(recipe.files.every(row => !/dgvoodoo/i.test(row.source)));
    assert.equal(recipe.files.find(row => row.role === 'game-loader').base, 'runtime');
  }
});

test('code-pinned receipts remain restorable with their resource pool absent', () => {
  const pkg = runtime.load({ api: 'dx10', architecture: 'x86', hardwareFamily: 'RTX40' });
  const offline = createLegacyRuntime({ appDir: path.resolve(__dirname, '..'), root: path.join(__dirname, 'missing-legacy-pool'), lock: runtime.lock });
  assert.equal(offline.validateStored(pkg.recipe), pkg.recipe);
  const forged = structuredClone(pkg.recipe); forged.files[0].target = '../outside.dll';
  assert.throws(() => offline.validateStored(forged));
  const changed = structuredClone(pkg.recipe); changed.selection.hardwareFamily = 'RTX50';
  assert.throws(() => offline.validateStored(changed));
});
test('adapter inventory exposes only the pinned host and fixed read-only argument', () => {
  const probe = runtime.adapterProbe();
  const host = runtime.load({ api: 'dx10', architecture: 'x86', hardwareFamily: 'RTX50' }).recipe.files.find(row => row.role === 'host');
  assert.equal(probe.sha256, host.sha256); assert.equal(probe.file, path.join(runtime.root, host.source));
  assert.deepEqual(probe.args, ['--list-adapters-json']); assert.ok(Object.isFrozen(probe) && Object.isFrozen(probe.args));
});
