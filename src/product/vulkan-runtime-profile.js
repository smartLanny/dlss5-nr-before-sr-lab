'use strict';

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const peDefault = require('../core/pe');
const { inside, noLinks, digestFile, atomicJson } = require('./launch-safety');

const PRODUCT = 'xiaofeng-vulkan-runtime-profile';
const RECEIPT = '.xiaofeng-vulkan-runtime.json';
const HASH = /^[a-f0-9]{64}$/;
const REVISION = /^[a-f0-9]{7,64}$/i;
const ID = /^[a-z0-9][a-z0-9._-]{0,79}$/i;
const PE_EXTENSIONS = new Set(['.dll', '.exe', '.asi', '.addon64']);

function fail(code, message, details) {
  throw Object.assign(new Error(message), { code, details });
}
function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]));
  return value;
}
function jsonHash(value) {
  return crypto.createHash('sha256').update(JSON.stringify(canonical(value))).digest('hex');
}
function pathHash(value) {
  return crypto.createHash('sha256').update(path.resolve(value).toLowerCase()).digest('hex');
}
function samePath(a, b) { return path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase(); }
function localAbsolute(value) {
  if (typeof value !== 'string' || !path.isAbsolute(value)) return false;
  return process.platform !== 'win32' || /^[a-z]:[\\/]/i.test(value);
}
function safeRelative(value) {
  if (typeof value !== 'string' || !value || value.length > 512 || path.isAbsolute(value) || /[\0<>:"|?*]/.test(value) ||
      value.split(/[\\/]+/).some(part => part === '..' || part === '')) return false;
  const normalized = path.normalize(value);
  return normalized !== '.' && normalized !== '..' && !normalized.startsWith(`..${path.sep}`) && !path.isAbsolute(normalized);
}

function createVulkanRuntimeProfile(options = {}) {
  if (!localAbsolute(options.userData)) {
    fail('VULKAN_RUNTIME_BAD_CONFIG', 'Vulkan 运行目录需要绝对的用户数据路径。');
  }
  const userData = path.resolve(options.userData);
  const runtimeRoot = path.join(userData, 'vulkan-runtime');
  const archiveRoot = path.join(userData, 'vulkan-runtime-archive');
  const pe = options.pe || peDefault;
  const copyFile = options.copyFile || fsp.copyFile;

  function validateExe(exe) {
    if (!localAbsolute(exe)) fail('VULKAN_RUNTIME_BAD_EXE', '游戏 EXE 路径无效。');
    return path.resolve(exe);
  }

  function normalizeRecipe(recipe) {
    if (!recipe || recipe.version !== 1 || !ID.test(recipe.id || '') || !ID.test(recipe.coreVersion || '') || !REVISION.test(recipe.sourceRevision || '') || recipe.architecture !== 64 ||
        !Array.isArray(recipe.files) || recipe.files.length === 0 || recipe.files.length > 64) {
      fail('VULKAN_RUNTIME_RECIPE_INVALID', 'Vulkan 运行资产 recipe 身份、架构或文件列表无效。');
    }
    const sources = new Set(), targets = new Set();
    const files = recipe.files.map(row => {
      if (!row || !safeRelative(row.source) || !safeRelative(row.target) || !HASH.test(row.sha256 || '') || typeof row.mutable !== 'boolean') {
        fail('VULKAN_RUNTIME_RECIPE_INVALID', 'Vulkan 运行资产 recipe 包含无效路径或摘要。');
      }
      const source = path.normalize(row.source), target = path.normalize(row.target);
      const sourceKey = source.toLowerCase(), targetKey = target.toLowerCase();
      if (sources.has(sourceKey) || targets.has(targetKey) || targetKey === RECEIPT.toLowerCase()) {
        fail('VULKAN_RUNTIME_RECIPE_INVALID', 'Vulkan 运行资产 recipe 包含重复或保留路径。');
      }
      sources.add(sourceKey); targets.add(targetKey);
      return { source, target, sha256: row.sha256.toLowerCase(), mutable: row.mutable };
    });
    const identity = { version: 1, id: recipe.id, coreVersion: recipe.coreVersion, sourceRevision: recipe.sourceRevision.toLowerCase(), architecture: 64, files };
    return { ...identity, fingerprint: jsonHash(identity) };
  }

  function locations(exe, recipe) {
    const exeId = pathHash(exe);
    const packageId = `${recipe.id}-${recipe.fingerprint.slice(0, 16)}`;
    const exeRoot = path.join(runtimeRoot, exeId);
    const basePath = path.join(exeRoot, packageId);
    return { exeId, packageId, exeRoot, basePath };
  }

  function location(input) {
    const exe = validateExe(input?.exe), recipe = normalizeRecipe(input?.recipe), place = locations(exe, recipe);
    return { basePath: place.basePath, coreVersion: recipe.coreVersion, packageId: recipe.id };
  }

  function resolveContained(root, relative, code = 'VULKAN_RUNTIME_RECIPE_INVALID') {
    const result = path.resolve(root, relative);
    if (!inside(root, result) || samePath(root, result)) fail(code, 'Vulkan 运行资产路径越过允许目录。', { file: relative });
    return result;
  }

  async function assertX64(file) {
    if (!PE_EXTENSIONS.has(path.extname(file).toLowerCase())) return;
    let bitness;
    try { bitness = pe.getBitness(file); } catch { fail('VULKAN_RUNTIME_ARCH', 'Vulkan 运行二进制不是有效的 x64 PE。', { file: path.basename(file) }); }
    if (bitness !== 64) fail('VULKAN_RUNTIME_ARCH', 'Vulkan 运行二进制不是 x64。', { file: path.basename(file) });
  }

  function receiptFor(exe, recipe, place) {
    return {
      version: 1,
      product: PRODUCT,
      exe,
      exeId: place.exeId,
      packageId: place.packageId,
      recipe: {
        version: 1,
        id: recipe.id,
        coreVersion: recipe.coreVersion,
        sourceRevision: recipe.sourceRevision,
        architecture: 64,
        fingerprint: recipe.fingerprint,
        files: recipe.files
      }
    };
  }

  async function readReceipt(basePath) {
    const file = path.join(basePath, RECEIPT);
    await noLinks(file);
    let value;
    try {
      const stat = await fsp.stat(file);
      if (!stat.isFile() || stat.size > 128 * 1024) throw new Error('size');
      value = JSON.parse(await fsp.readFile(file, 'utf8'));
    } catch (error) {
      if (error.code === 'ENOENT') fail('VULKAN_RUNTIME_NOT_PREPARED', 'Vulkan 运行资产尚未准备。');
      fail('VULKAN_RUNTIME_RECEIPT_INVALID', 'Vulkan 运行资产记录损坏，已停止操作。');
    }
    if (!value || value.version !== 1 || value.product !== PRODUCT || typeof value.exe !== 'string' || !path.isAbsolute(value.exe) ||
        !/^[a-f0-9]{64}$/.test(value.exeId || '') || !ID.test(value.recipe?.id || '') || !ID.test(value.recipe?.coreVersion || '') || !REVISION.test(value.recipe?.sourceRevision || '') ||
        value.recipe?.architecture !== 64 || !HASH.test(value.recipe?.fingerprint || '') || !Array.isArray(value.recipe?.files)) {
      fail('VULKAN_RUNTIME_RECEIPT_INVALID', 'Vulkan 运行资产记录内容无效，已停止操作。');
    }
    const recipe = normalizeRecipe(value.recipe);
    if (recipe.fingerprint !== value.recipe.fingerprint || value.exeId !== pathHash(value.exe) || value.packageId !== `${recipe.id}-${recipe.fingerprint.slice(0, 16)}`) {
      fail('VULKAN_RUNTIME_RECEIPT_INVALID', 'Vulkan 运行资产记录身份不一致，已停止操作。');
    }
    return { value, recipe };
  }

  async function inspect(input) {
    const exe = validateExe(input?.exe);
    await noLinks(exe);
    let recipe = input?.recipe ? normalizeRecipe(input.recipe) : null;
    let basePath;
    if (input?.basePath !== undefined) {
      if (typeof input.basePath !== 'string' || !path.isAbsolute(input.basePath)) fail('VULKAN_RUNTIME_PATH_INVALID', 'Vulkan 运行资产目录无效。');
      basePath = path.resolve(input.basePath);
      const exeRoot = path.join(runtimeRoot, pathHash(exe));
      if (!inside(exeRoot, basePath) || samePath(exeRoot, basePath) || path.dirname(basePath).toLowerCase() !== exeRoot.toLowerCase()) {
        fail('VULKAN_RUNTIME_PATH_INVALID', 'Vulkan 运行资产目录不属于该 EXE。');
      }
    } else {
      if (!recipe) fail('VULKAN_RUNTIME_RECIPE_INVALID', '检查 Vulkan 运行资产需要 recipe 或准备结果目录。');
      basePath = locations(exe, recipe).basePath;
    }
    await noLinks(basePath);
    if (!fs.existsSync(basePath)) return { ready: false, exists: false, basePath, coreVersion: recipe?.coreVersion || null, packageId: recipe?.id || null, files: [], blockers: ['运行资产目录尚未准备。'] };
    const received = await readReceipt(basePath);
    if (!samePath(received.value.exe, exe)) fail('VULKAN_RUNTIME_EXE_CHANGED', '运行资产绑定到另一游戏 EXE，已停止操作。');
    if (recipe && recipe.fingerprint !== received.recipe.fingerprint) fail('VULKAN_RUNTIME_PACKAGE_CHANGED', '运行资产目录属于另一固定 package。');
    recipe = received.recipe;
    if (path.basename(basePath).toLowerCase() !== received.value.packageId.toLowerCase()) fail('VULKAN_RUNTIME_RECEIPT_INVALID', '运行资产目录与记录身份不一致。');
    const files = [], blockers = [];
    for (const row of recipe.files) {
      const file = resolveContained(basePath, row.target, 'VULKAN_RUNTIME_RECEIPT_INVALID');
      try {
        await noLinks(file);
        const stat = await fsp.stat(file);
        if (!stat.isFile()) throw Object.assign(new Error('not-file'), { code: 'ENOENT' });
        await assertX64(file);
        const actual = row.mutable ? null : await digestFile(file);
        const valid = row.mutable || actual === row.sha256;
        files.push({ target: row.target, file, mutable: row.mutable, valid });
        if (!valid) blockers.push(`${row.target} 的摘要与固定 recipe 不一致。`);
      } catch (error) {
        if (error.code !== 'ENOENT' && !['VULKAN_RUNTIME_ARCH', 'SETTINGS_LINK_BLOCKED'].includes(error.code)) throw error;
        files.push({ target: row.target, file, mutable: row.mutable, valid: false });
        blockers.push(error.code === 'VULKAN_RUNTIME_ARCH' ? error.message : `${row.target} 缺失或不可安全读取。`);
      }
    }
    return { ready: blockers.length === 0, exists: true, basePath, coreVersion: recipe.coreVersion, packageId: recipe.id, files, blockers };
  }

  async function validateSource(packageRoot, recipe) {
    if (!localAbsolute(packageRoot)) fail('VULKAN_RUNTIME_SOURCE_INVALID', '运行资产 packageRoot 必须是本机绝对目录。');
    const root = path.resolve(packageRoot);
    await noLinks(root);
    let stat;
    try { stat = await fsp.stat(root); } catch { fail('VULKAN_RUNTIME_SOURCE_MISSING', '找不到 Vulkan 运行资产来源目录。'); }
    if (!stat.isDirectory()) fail('VULKAN_RUNTIME_SOURCE_INVALID', 'Vulkan 运行资产来源不是目录。');
    const sources = [];
    for (const row of recipe.files) {
      const file = resolveContained(root, row.source);
      await noLinks(file);
      let fileStat;
      try { fileStat = await fsp.stat(file); } catch { fail('VULKAN_RUNTIME_SOURCE_MISSING', 'Vulkan 运行资产来源缺少文件。', { file: row.source }); }
      if (!fileStat.isFile() || await digestFile(file) !== row.sha256) fail('VULKAN_RUNTIME_SOURCE_HASH', 'Vulkan 运行资产来源文件校验不匹配。', { file: row.source });
      await assertX64(file);
      sources.push({ ...row, file });
    }
    return { root, sources };
  }

  async function prepare(input) {
    const exe = validateExe(input?.exe);
    await noLinks(exe);
    let exeStat;
    try { exeStat = await fsp.stat(exe); } catch { fail('VULKAN_RUNTIME_BAD_EXE', '找不到游戏 EXE。'); }
    if (!exeStat.isFile()) fail('VULKAN_RUNTIME_BAD_EXE', '游戏 EXE 不是普通文件。');
    await assertX64(exe);
    const recipe = normalizeRecipe(input?.recipe);
    const source = await validateSource(input?.packageRoot, recipe);
    const place = locations(exe, recipe);
    await noLinks(place.basePath);
    if (fs.existsSync(place.basePath)) {
      const current = await inspect({ exe, recipe, basePath: place.basePath });
      if (!current.ready) fail('VULKAN_RUNTIME_CHANGED', '已有 Vulkan 运行资产缺失或被修改，未覆盖用户文件。', { blockers: current.blockers });
      return { basePath: current.basePath, coreVersion: recipe.coreVersion, packageId: recipe.id, files: current.files, reused: true };
    }
    await noLinks(place.exeRoot);
    await fsp.mkdir(place.exeRoot, { recursive: true });
    const stage = path.join(place.exeRoot, `.${place.packageId}.${crypto.randomUUID()}.stage`);
    await noLinks(stage);
    await fsp.mkdir(stage, { recursive: false, mode: 0o700 });
    try {
      for (const row of source.sources) {
        const target = resolveContained(stage, row.target);
        await noLinks(target);
        await fsp.mkdir(path.dirname(target), { recursive: true });
        await noLinks(row.file);
        if (await digestFile(row.file) !== row.sha256) fail('VULKAN_RUNTIME_SOURCE_CHANGED', 'Vulkan 运行资产来源在复制前发生变化。', { file: row.source });
        await copyFile(row.file, target, fs.constants.COPYFILE_EXCL);
        await noLinks(row.file); await noLinks(target);
        if (await digestFile(row.file) !== row.sha256 || await digestFile(target) !== row.sha256) {
          fail('VULKAN_RUNTIME_SOURCE_CHANGED', 'Vulkan 运行资产在复制期间发生变化。', { file: row.source });
        }
        await assertX64(target);
      }
      await atomicJson(path.join(stage, RECEIPT), receiptFor(exe, recipe, place));
      await noLinks(place.basePath);
      await fsp.rename(stage, place.basePath);
    } catch (error) {
      await fsp.rm(stage, { recursive: true, force: true }).catch(() => {});
      throw error;
    }
    const result = await inspect({ exe, recipe, basePath: place.basePath });
    if (!result.ready) fail('VULKAN_RUNTIME_WRITE_VERIFY', 'Vulkan 运行资产发布后校验失败。', { blockers: result.blockers });
    return { basePath: result.basePath, coreVersion: recipe.coreVersion, packageId: recipe.id, files: result.files, reused: false };
  }

  async function archive(input) {
    const exe = validateExe(input?.exe);
    if (typeof input?.basePath !== 'string' || !path.isAbsolute(input.basePath)) fail('VULKAN_RUNTIME_PATH_INVALID', '归档目录无效。');
    const basePath = path.resolve(input.basePath), exeRoot = path.join(runtimeRoot, pathHash(exe));
    if (path.dirname(basePath).toLowerCase() !== exeRoot.toLowerCase()) fail('VULKAN_RUNTIME_PATH_INVALID', '归档目录不属于该 EXE。');
    await noLinks(exeRoot); await noLinks(basePath);
    const received = await readReceipt(basePath);
    if (!samePath(received.value.exe, exe) || path.basename(basePath).toLowerCase() !== received.value.packageId.toLowerCase()) {
      fail('VULKAN_RUNTIME_EXE_CHANGED', '归档记录与游戏 EXE 不一致，未移动文件。');
    }
    const destinationRoot = path.join(archiveRoot, received.value.exeId, received.value.packageId);
    await noLinks(destinationRoot);
    await fsp.mkdir(destinationRoot, { recursive: true });
    const archivePath = path.join(destinationRoot, `${new Date().toISOString().replace(/[:.]/g, '-')}-${crypto.randomUUID()}`);
    await noLinks(archivePath);
    await fsp.rename(basePath, archivePath);
    return { archived: true, archivePath, basePath, coreVersion: received.recipe.coreVersion, packageId: received.recipe.id };
  }

  return { location, prepare, inspect, archive, runtimeRoot, archiveRoot };
}

module.exports = { createVulkanRuntimeProfile };
