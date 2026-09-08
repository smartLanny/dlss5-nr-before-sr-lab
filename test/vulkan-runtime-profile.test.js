'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { createVulkanRuntimeProfile } = require('../src/product/vulkan-runtime-profile');

const hash = value => crypto.createHash('sha256').update(value).digest('hex');

function fixture(t, options = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-vulkan-runtime-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const userData = path.join(root, 'user'), packageRoot = path.join(root, 'package'), game = path.join(root, 'game');
  fs.mkdirSync(path.join(packageRoot, 'bin'), { recursive: true }); fs.mkdirSync(game);
  const exe = path.join(game, 'Game.exe'); fs.writeFileSync(exe, 'pe64-game');
  const contents = { 'bin/ReShade64.dll': 'pe64-runtime', 'ReShade.ini': '[INSTALL]\nBasePath=.\n', 'Addons/provider.addon64': 'pe64-addon' };
  for (const [rel, body] of Object.entries(contents)) { const file = path.join(packageRoot, rel); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, body); }
  const recipe = {
    version: 1, id: 'nr-vulkan-external-e7df0fc', coreVersion: '0.4.6-hotfix.1-vulkan-provider', sourceRevision: 'e7df0fc', architecture: 64,
    files: [
      { source: 'bin/ReShade64.dll', target: 'ReShade64.dll', sha256: hash(contents['bin/ReShade64.dll']), mutable: false },
      { source: 'ReShade.ini', target: 'ReShade.ini', sha256: hash(contents['ReShade.ini']), mutable: true },
      { source: 'Addons/provider.addon64', target: 'Addons/provider.addon64', sha256: hash(contents['Addons/provider.addon64']), mutable: false }
    ]
  };
  const pe = { getBitness(file) { return fs.readFileSync(file, 'utf8').startsWith('pe64') ? 64 : 32; } };
  const service = createVulkanRuntimeProfile({ userData, pe, ...options });
  return { root, userData, packageRoot, game, exe, recipe, service, contents };
}

test('prepare atomically publishes an EXE/package-bound external profile and mutable config may change', async t => {
  const f = fixture(t);
  const predicted = f.service.location({ exe: f.exe, recipe: f.recipe });
  assert.equal(fs.existsSync(f.service.runtimeRoot), false, 'location is synchronous calculation only');
  const absent = await f.service.inspect({ exe: f.exe, recipe: f.recipe });
  assert.deepEqual({ ready: absent.ready, basePath: absent.basePath, coreVersion: absent.coreVersion, packageId: absent.packageId, blockers: absent.blockers },
    { ready: false, basePath: predicted.basePath, coreVersion: f.recipe.coreVersion, packageId: f.recipe.id, blockers: ['运行资产目录尚未准备。'] });
  const prepared = await f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe });
  assert.deepEqual({ basePath: prepared.basePath, coreVersion: prepared.coreVersion, packageId: prepared.packageId }, predicted);
  assert.equal(prepared.coreVersion, f.recipe.coreVersion);
  assert.equal(prepared.packageId, f.recipe.id);
  assert.equal(prepared.reused, false);
  assert.equal(path.dirname(path.dirname(prepared.basePath)), f.service.runtimeRoot);
  assert.equal(fs.existsSync(path.join(prepared.basePath, 'ReShade64.dll')), true);
  assert.equal(fs.existsSync(path.join(f.game, 'ReShade64.dll')), false);

  fs.writeFileSync(path.join(prepared.basePath, 'ReShade.ini'), '[INSTALL]\nBasePath=user-choice\n');
  const inspected = await f.service.inspect({ exe: f.exe, basePath: prepared.basePath });
  assert.equal(inspected.ready, true);
  assert.equal(inspected.files.find(row => row.target === 'ReShade.ini').valid, true);

  const reused = await f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe });
  assert.equal(reused.reused, true);
  assert.match(fs.readFileSync(path.join(prepared.basePath, 'ReShade.ini'), 'utf8'), /user-choice/);
});

test('inspect rejects immutable drift and prepare does not overwrite the changed installed file', async t => {
  const f = fixture(t), prepared = await f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe });
  const runtime = path.join(prepared.basePath, 'ReShade64.dll'); fs.writeFileSync(runtime, 'pe64-external-change');
  const inspected = await f.service.inspect({ exe: f.exe, recipe: f.recipe });
  assert.equal(inspected.ready, false); assert.match(inspected.blockers.join(' '), /摘要/);
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe }), { code: 'VULKAN_RUNTIME_CHANGED' });
  assert.equal(fs.readFileSync(runtime, 'utf8'), 'pe64-external-change');
});

test('archive moves the complete bound profile without deleting mutable user config', async t => {
  const f = fixture(t), prepared = await f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe });
  const config = path.join(prepared.basePath, 'ReShade.ini'); fs.writeFileSync(config, 'user runtime state');
  const result = await f.service.archive({ exe: f.exe, basePath: prepared.basePath });
  assert.equal(result.archived, true); assert.equal(fs.existsSync(prepared.basePath), false);
  assert.equal(fs.readFileSync(path.join(result.archivePath, 'ReShade.ini'), 'utf8'), 'user runtime state');
  assert.equal(fs.existsSync(path.join(result.archivePath, 'Addons/provider.addon64')), true);
});

test('EXE and package identity prevent cross-game inspection or archive', async t => {
  const f = fixture(t), prepared = await f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe });
  const other = path.join(f.game, 'Other.exe'); fs.writeFileSync(other, 'pe64-other');
  await assert.rejects(f.service.inspect({ exe: other, basePath: prepared.basePath }), { code: 'VULKAN_RUNTIME_PATH_INVALID' });
  await assert.rejects(f.service.archive({ exe: other, basePath: prepared.basePath }), { code: 'VULKAN_RUNTIME_PATH_INVALID' });
  assert.equal(fs.existsSync(prepared.basePath), true);
  const changedRecipe = { ...f.recipe, coreVersion: '0.4.6-hotfix.2' };
  await assert.rejects(f.service.inspect({ exe: f.exe, basePath: prepared.basePath, recipe: changedRecipe }), { code: 'VULKAN_RUNTIME_PACKAGE_CHANGED' });
});

test('recipe traversal, source links, bad hashes and x86 binaries fail before publication', async t => {
  const f = fixture(t);
  assert.throws(() => f.service.location({ exe: f.exe, recipe: { ...f.recipe, sourceRevision: 'unknown' } }), { code: 'VULKAN_RUNTIME_RECIPE_INVALID' });
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: { ...f.recipe, files: [{ ...f.recipe.files[0], target: '../escape.dll' }] } }), { code: 'VULKAN_RUNTIME_RECIPE_INVALID' });
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: { ...f.recipe, files: [{ ...f.recipe.files[0], sha256: '0'.repeat(64) }] } }), { code: 'VULKAN_RUNTIME_SOURCE_HASH' });
  fs.writeFileSync(path.join(f.packageRoot, 'bin/ReShade64.dll'), 'pe32-runtime');
  const x86Recipe = { ...f.recipe, files: f.recipe.files.map(row => row.source === 'bin/ReShade64.dll' ? { ...row, sha256: hash('pe32-runtime') } : row) };
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: x86Recipe }), { code: 'VULKAN_RUNTIME_ARCH' });
  assert.equal(fs.existsSync(f.service.runtimeRoot) && fs.readdirSync(f.service.runtimeRoot).some(name => !name.startsWith('.')), false);

  fs.writeFileSync(path.join(f.packageRoot, 'hard-source.ini'), 'hard-linked');
  fs.linkSync(path.join(f.packageRoot, 'hard-source.ini'), path.join(f.packageRoot, 'hard.ini'));
  const hardLinked = { ...f.recipe, files: [{ source: 'hard.ini', target: 'hard.ini', sha256: hash('hard-linked'), mutable: true }] };
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: hardLinked }), { code: 'SETTINGS_LINK_BLOCKED' });

  if (process.platform === 'win32') {
    fs.writeFileSync(path.join(f.packageRoot, 'real.ini'), 'linked');
    try {
      fs.symlinkSync(path.join(f.packageRoot, 'real.ini'), path.join(f.packageRoot, 'linked.ini'), 'file');
      const linked = { ...f.recipe, files: [{ source: 'linked.ini', target: 'linked.ini', sha256: hash('linked'), mutable: true }] };
      await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: linked }), { code: 'SETTINGS_LINK_BLOCKED' });
    } catch (error) { if (!['EPERM', 'EACCES'].includes(error.code)) throw error; }
  }
});

test('source drift during copy is detected, staged files are removed, and game directory stays untouched', async t => {
  let copied = false;
  const f = fixture(t, { copyFile: async (source, target, flags) => {
    await fsp.copyFile(source, target, flags);
    if (!copied) { copied = true; await fsp.writeFile(source, 'pe64-drifted-source'); }
  } });
  await assert.rejects(f.service.prepare({ exe: f.exe, packageRoot: f.packageRoot, recipe: f.recipe }), { code: 'VULKAN_RUNTIME_SOURCE_CHANGED' });
  const exeRoots = fs.existsSync(f.service.runtimeRoot) ? fs.readdirSync(f.service.runtimeRoot) : [];
  for (const exeRoot of exeRoots) assert.deepEqual(fs.readdirSync(path.join(f.service.runtimeRoot, exeRoot)), []);
  assert.deepEqual(fs.readdirSync(f.game).sort(), ['Game.exe']);
});
