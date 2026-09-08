'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const asar = require('@electron/asar');
const packageJson = require('../package.json');
const { verifyExternalPackage } = require('../scripts/verify-external-package.cjs');

test('external build inherits the normal app while excluding only bundled payload resources', () => {
  const before = structuredClone(packageJson.build), config = require('../scripts/external-build-config.cjs');
  assert.deepEqual(packageJson.build, before, 'loading the variant must not mutate the default package config');
  for (const key of ['appId', 'productName', 'files', 'fileAssociations']) assert.deepEqual(config[key], before[key]);
  assert.equal(config.extraResources.some(row => row.from === 'payload'), false);
  for (const from of ['src/product/nvapi-drs.ps1', 'src/product/nvapi-profile.ps1', 'resources/fg-components'])
    assert.equal(config.extraResources.some(row => row.from === from), true, `${from} retained`);
  assert.equal(config.win.requestedExecutionLevel, 'requireAdministrator'); assert.equal(config.portable.requestExecutionLevel, 'admin');
  assert.equal(config.portable.artifactName, 'DLSS5-Manager-${version}-external-portable.exe');
  assert.equal(config.nsis.artifactName, 'DLSS5-Manager-Setup-${version}-external.exe'); assert.equal(config.directories.output, 'dist-external');
});

async function packagedFixture(t, options = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-external-package-')), app = path.join(root, 'app'), resources = path.join(root, 'win-unpacked', 'resources');
  t.after(() => fs.rmSync(root, { recursive: true, force: true })); fs.mkdirSync(app, { recursive: true }); fs.mkdirSync(resources, { recursive: true });
  fs.writeFileSync(path.join(app, 'package.json'), JSON.stringify({ name: 'xiaofeng-dlss5-manager', main: 'main.js' })); fs.writeFileSync(path.join(app, 'main.js'), 'module.exports = true;');
  if (options.forbiddenAsar) fs.writeFileSync(path.join(app, 'nr-before-sr.zh-CN.addon64'), 'must not enter asar');
  await asar.createPackage(app, path.join(resources, 'app.asar'));
  for (const name of ['nvapi-drs.ps1', 'nvapi-profile.ps1']) fs.writeFileSync(path.join(resources, name), '# fixture');
  const fg = path.join(resources, 'fg-components'); fs.mkdirSync(fg);
  for (const name of ['LICENSE', 'MINHOOK-LICENSE.txt', 'UAL-LICENSE', 'global.ini', 'component.dll']) fs.writeFileSync(path.join(fg, name), name);
  fs.writeFileSync(path.join(fg, 'manifest.json'), JSON.stringify({ version: 1, files: { core: { file: 'component.dll', sha256: '0'.repeat(64) } } }));
  return { root, app, resources, unpacked: path.dirname(resources) };
}

test('package verifier accepts an external build with FG licenses and no NR payload', async t => {
  const f = await packagedFixture(t), result = verifyExternalPackage(f.unpacked);
  assert.equal(result.ok, true); assert.equal(result.payloadBundled, false); assert.ok(result.asarEntries >= 2);
});

test('package verifier rejects proprietary NR files in resources or app.asar', async t => {
  const f = await packagedFixture(t), leaked = path.join(f.resources, 'payload', 'nr-before-sr', 'fixed', 'RTX50'); fs.mkdirSync(leaked, { recursive: true });
  fs.writeFileSync(path.join(leaked, 'nvngx_dlssnr.dll'), 'must not ship');
  assert.throws(() => verifyExternalPackage(f.resources), { code: 'ERR_EXTERNAL_PACKAGE_INVALID' }); fs.rmSync(path.join(f.resources, 'payload'), { recursive: true });

  const second = await packagedFixture(t, { forbiddenAsar: true });
  assert.throws(() => verifyExternalPackage(path.join(second.resources, 'app.asar')), { code: 'ERR_EXTERNAL_PACKAGE_INVALID' });
});
