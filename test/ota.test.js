'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { readOtaPackage, sha256Buffer } = require('../src/product/ota');
const { CORE, CARRIER, BRIDGE, zip, tempZip, standardFixture, dx11Fixture } = require('./helpers/ota-fixture');

test('keeps the historical nr-branch-ota-v1 DX12 contract', async () => {
  const file = zip(tempZip(), standardFixture());
  const ota = await readOtaPackage(file);
  assert.equal(ota.manifest.schema, 'nr-branch-ota-v1');
  assert.equal(ota.addonName, 'core.addon64');
  assert.equal(ota.addon.toString(), 'standard-core');
  assert.equal(ota.addonSha256, sha256Buffer(ota.addon));
  assert.equal(ota.bridgeName, BRIDGE);
  assert.equal(ota.bridgeSha256, sha256Buffer(ota.bridge));
  assert.equal(ota.carrierName, null);
  assert.equal(ota.carrier, null);
  assert.equal(ota.carrierSha256, null);
  assert.equal(ota.compatibility, null);
  assert.equal(ota.instructions, 'standard instructions');
});

test('keeps the published Beta0.3.8 optional unhashed instructions exception bounded to text', async () => {
  const file = zip(tempZip(), standardFixture({ unhashedInstructions: true }));
  const ota = await readOtaPackage(file);
  assert.equal(ota.instructions, 'standard instructions');
  assert.equal(ota.compatibility, null);
});

test('identifies the published DX11 core, carrier and nrchain by role rather than ZIP order', async () => {
  const file = zip(tempZip(), dx11Fixture());
  const ota = await readOtaPackage(file);
  assert.equal(ota.manifest.version, 'beta0.4.5-dx11-compat');
  assert.equal(ota.manifest.source_commit, 'd'.repeat(40));
  assert.equal(ota.addonName, CORE);
  assert.equal(ota.addon.toString(), 'dx11-core');
  assert.equal(ota.carrierName, CARRIER);
  assert.equal(ota.carrier.toString(), 'matched-carrier');
  assert.equal(ota.bridgeName, BRIDGE);
  assert.equal(ota.compatibility, 'dx11');
  assert.equal(ota.addonSha256, sha256Buffer(ota.addon));
  assert.equal(ota.carrierSha256, sha256Buffer(ota.carrier));
  assert.equal(ota.bridgeSha256, sha256Buffer(ota.bridge));
  assert.equal(ota.instructions, '成套安装');
});

const realPackage = path.resolve(__dirname, '..', '..', 'nr-main-unify', 'build', 'dx11-compat-chinese-final',
  'DLSS5-AI渲染超分版-0.4.5-DX11-兼容增强-@野生的装机宅-Bilibili-OTA.zip');
test('reads the locally verified published Chinese compatibility ZIP', { skip: !fs.existsSync(realPackage) }, async () => {
  const ota = await readOtaPackage(realPackage);
  assert.equal(ota.compatibility, 'dx11');
  assert.equal(ota.addonName, CORE);
  assert.equal(ota.addonSha256, 'ffea8e3a92cf07388f71b1855f3157c9c959a7a094b6f1a35cb92c57bf2f06f9');
  assert.equal(ota.carrierName, CARRIER);
  assert.equal(ota.carrierSha256, 'f825ccc47c2bdf3e365606ba44760371f74ac0ec0fabbc0565ae98864fca05ce');
  assert.equal(ota.bridgeSha256, '46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb');
});

test('rejects tampered hashes and declared sizes', async t => {
  await t.test('standard hash', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), standardFixture({ badHash: true }))), /hash mismatch/);
  });
  await t.test('standard size', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), standardFixture({ badBytes: true }))), /size mismatch/);
  });
  await t.test('DX11 hash', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), dx11Fixture({ badHash: true }))), /hash mismatch/);
  });
  await t.test('ZIP size metadata', async () => {
    const rows = standardFixture(); rows[1].declaredSize = rows[1].data.length + 1;
    await assert.rejects(readOtaPackage(zip(tempZip(), rows)), /size|invalid/i);
  });
});

test('rejects missing or ambiguous component roles without cross-package fallback', async t => {
  await t.test('DX11 carrier missing', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), dx11Fixture({ noCarrier: true }))), /matched DX11 carrier/);
  });
  await t.test('DX11 nrchain missing', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), dx11Fixture({ noBridge: true }))), /nrchain_nvngx/);
  });
  await t.test('DX11 multiple cores', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), dx11Fixture({ extraCore: true }))), /ambiguous addon64 role/);
  });
  await t.test('DX12 carrier is never accepted as its core', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), standardFixture({ carrier: true }))), /addon64/);
  });
});

test('rejects duplicate paths, case collisions, duplicate records and unlisted payloads', async t => {
  await t.test('exact duplicate ZIP path', async () => {
    const rows = standardFixture(); rows.push({ ...rows[1] });
    await assert.rejects(readOtaPackage(zip(tempZip(), rows)), /duplicate zip path/);
  });
  await t.test('case-colliding ZIP path', async () => {
    const rows = standardFixture(); rows.push({ name: BRIDGE.toUpperCase(), data: 'collision' });
    await assert.rejects(readOtaPackage(zip(tempZip(), rows)), /duplicate zip path/);
  });
  await t.test('file and directory alias', async () => {
    const rows = standardFixture(); rows.push({ name: 'core.addon64/', data: '' });
    await assert.rejects(readOtaPackage(zip(tempZip(), rows)), /duplicate zip path/);
  });
  await t.test('case-colliding manifest record', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), standardFixture({ duplicateRow: true }))), /duplicate OTA file record/);
  });
  await t.test('unlisted payload', async () => {
    await assert.rejects(readOtaPackage(zip(tempZip(), standardFixture({ unlisted: true }))), /unlisted OTA file/);
  });
});
