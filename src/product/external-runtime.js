'use strict';
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const { noLinks, inside, atomicJson } = require('./launch-safety');
const { readManifest, manifestPath, newManifest, backupPath, assertManifestExecutable } = require('./manifest');
const { requireAddonLayout, inspectAddonLayout } = require('./reshade-layout');
const { externalConfig, localConfig, loaderConfig, inspectProfile } = require('./external-profile-config');
const { INSTALLED_NAMES } = require('./constants');
const { classifyApi } = require('./game-support');
const { classifyAddon } = require('./conflicts');

const PRODUCT = 'xiaofeng-external-runtime';
const RECEIPT = '_DLSS5_Backup/xiaofeng-external.json';
const PENDING = '_DLSS5_Backup/xiaofeng-external-pending.json';
const HASH = /^[a-f0-9]{64}$/;
const UUID = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const MAX_FILE = 768 * 1024 * 1024;
const MANAGED = new Set(['addon', 'bridge', 'runtime', 'carrier', 'config']);
const MUTABLE = /\.(?:ini|json|toml|ya?ml)$/i;
const ADDON = /\.addon(?:32|64)?$/i;
const PROTECTED = /^(?:dxgi|d3d9|d3d10|d3d11|d3d12|opengl32|dinput8|version|winmm|dsound|nvngx_dlss|nvngx_dlssg|_nvngx)\.dll$/i;
const key = value => path.resolve(value).toLowerCase();
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const jsonBytes = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const fail = (code, message, details) => { throw Object.assign(new Error(message), { code: 'DEPLOYMENT_' + code, details }); };
const active = new Set();
function leaf(value) {
  return typeof value === 'string' && value.length > 0 && value.length < 220 &&
    path.basename(value) === value && !/[<>:"|?*\0\\/]/.test(value) && !/[. ]$/.test(value) &&
    !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value);
}
function noLinksSync(file) {
  const full = path.resolve(file), root = path.parse(full).root; let cursor = root;
  for (const part of full.slice(root.length).split(path.sep).filter(Boolean)) {
    cursor = path.join(cursor, part);
    try {
      const stat = fs.lstatSync(cursor);
      if (stat.isSymbolicLink() || stat.isFile() && stat.nlink > 1) fail('LINK', '运行目录含链接，已保留文件和记录。');
      if (key(cursor) !== key(full) && !stat.isDirectory()) fail('PATH_PREFIX', '目标路径的上级位置是文件，未创建部署记录。', { file: cursor });
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
}
function readJson(file) {
  noLinksSync(file);
  try {
    const stat = fs.statSync(file);
    if (!stat.isFile() || stat.size > 4 * 1024 * 1024) fail('RECORD', '部署记录大小无效。');
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
async function digest(file) {
  await noLinks(file);
  let before; try { before = await fsp.stat(file); } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  if (!before.isFile() || before.size > MAX_FILE) fail('FILE', '部署目标不是可检查的普通文件。', { file });
  const sha = crypto.createHash('sha256');
  for await (const bytes of fs.createReadStream(file)) sha.update(bytes);
  const after = await fsp.stat(file); await noLinks(file);
  if (before.size !== after.size || before.mtimeMs !== after.mtimeMs || before.ino !== after.ino) fail('FILE_CHANGED', '文件在检查期间改变。', { file });
  return sha.digest('hex');
}
function createExternalRuntime(options) {
  if (!path.isAbsolute(options.userData || '')) fail('CONFIG', '外置运行目录需要绝对用户目录。');
  const base = path.join(path.resolve(options.userData), 'external-runtime'), plans = new Map();
  const copy = options.copyFile || fsp.copyFile;
  const guards = options.guards || require('../core/install-guards');
  const pe = options.pe || require('../core/pe');
  function target(game, recovering = false) {
    const gameRoot = path.resolve(game.dir || game.gameRoot || '.');
    const previous = readJson(path.join(gameRoot, RECEIPT)), interrupted = readJson(path.join(gameRoot, PENDING));
    let exe = game.scan?.chosen?.path || game.chosen?.path || game.exe;
    if (recovering) exe = (interrupted || previous)?.exe || exe;
    if (!exe) exe = (previous || interrupted)?.exe;
    if (!path.isAbsolute(game.dir || game.gameRoot || '') || !path.isAbsolute(exe || '') ||
        !inside(gameRoot, exe) || !/\.exe$/i.test(exe)) fail('TARGET', '请先确认实际游戏 EXE。');
    exe = path.resolve(exe);
    const id = hash(Buffer.from(key(exe))), ownerRoot = path.join(base, id);
    const binding = recovering && interrupted ? interrupted : previous;
    const profileId = binding?.exe && key(binding.exe) === key(exe) ? binding.profileId : undefined;
    if (profileId !== undefined && !UUID.test(profileId)) fail('RECORD', '外置运行目录代次无效。');
    return { id, gameId: game.id, gameRoot, exe, dir: path.dirname(exe), ownerRoot,
      profileId, runtimeDir: path.join(ownerRoot, profileId ? 'active-' + profileId : 'active'), receipt: path.join(gameRoot, RECEIPT), pending: path.join(gameRoot, PENDING) };
  }
  function allowed(t, file, role) {
    const absolute = path.resolve(file);
    if (role === 'receipt') return key(absolute) === key(t.receipt);
    if (role === 'manifest') return key(absolute) === key(manifestPath(t.gameRoot));
    if (role === 'original-backup') {
      const rel = path.relative(t.gameRoot, absolute), match = rel.match(/^_DLSS5_Backup[\\/]xiaofeng-originals[\\/]([a-f0-9-]{36})[\\/](.+)$/i);
      if (!match || !UUID.test(match[1])) return false;
      const original = path.resolve(t.gameRoot, match[2]);
      return inside(t.gameRoot, original) && (allowed(t, original, 'managed') || allowed(t, original, 'game-proxy'));
    }
    if (role === 'game-proxy') return key(path.dirname(absolute)) === key(t.dir) && /^(?:dxgi|d3d12)\.dll$/i.test(path.basename(absolute));
    if (role === 'profile-loader') return key(absolute) === key(path.join(t.runtimeDir, 'ReShade64.dll'));
    if (![t.dir, t.runtimeDir].some(dir => key(path.dirname(absolute)) === key(dir))) return false;
    const name = path.basename(absolute);
    if (!leaf(name) || /\.exe$/i.test(name) || PROTECTED.test(name)) return false;
    if (role === 'reshade-config') return name === 'ReShade.ini';
    if (role === 'managed') return Object.entries(INSTALLED_NAMES).some(([kind, value]) => MANAGED.has(kind) && value === name);
    if (role === 'user-addon') return ADDON.test(name);
    if (role === 'user-sidecar') return MUTABLE.test(name);
    if (role === 'user-dependency') return /\.dll$/i.test(name);
    return false;
  }
  function validateReceipt(t, value) {
    if (!value) return null;
    if (value.product !== PRODUCT || value.version !== 1 || value.id !== t.id ||
        key(value.gameRoot || '.') !== key(t.gameRoot) || key(value.exe || '.') !== key(t.exe) ||
        key(value.runtimeDir || '.') !== key(t.runtimeDir) || !['local', 'external'].includes(value.mode) ||
        !UUID.test(value.generation || '') || !['dx11', 'dx12'].includes(value.api) ||
        !value.localManifest || !Array.isArray(value.files) || value.files.length > 128 ||
        typeof value.originalReShadeConfig !== 'string' || !HASH.test(value.loaderConfigHash || '') ||
        value.loadingMode !== undefined && !['helper', 'proxy'].includes(value.loadingMode) ||
        value.proxy && (!/^(?:dxgi|d3d12)\.dll$/.test(value.proxy.name || '') || !HASH.test(value.proxy.sha256 || '')))
      fail('RECORD', '外置部署记录与当前游戏不一致，已保留恢复入口。');
    if (value.origin !== undefined && value.origin !== 'direct') fail('RECORD', '外置来源记录无效。');
    if (value.origin === 'direct' && (!UUID.test(value.initialOperation || '') || !value.initialProxy ||
        value.initialProxy.name !== value.proxy?.name || !HASH.test(value.initialProxy.after || '') ||
        value.initialProxy.before !== null && !HASH.test(value.initialProxy.before || '') ||
        !/^before\/[0-9]+\.bin$/.test(value.initialProxy.snapshot || '') ||
        typeof value.initialProxy.owned !== 'boolean')) fail('RECORD', '首次外置部署的原始加载器记录无效。');
    assertManifestExecutable(t.gameRoot, value.localManifest, t.exe);
    if (value.origin === 'direct') {
      const initial = readJson(path.join(t.ownerRoot, 'history', value.initialOperation, 'operation.json'));
      const proxyFile = path.join(t.dir, value.initialProxy.name), configFile = path.join(t.dir, 'ReShade.ini');
      if (!initial || initial.product !== PRODUCT || initial.operation !== value.initialOperation || initial.id !== t.id ||
          key(initial.exe || '.') !== key(t.exe) || key(initial.gameRoot || '.') !== key(t.gameRoot) ||
          (initial.profileId || null) !== (value.profileId || null) || !Array.isArray(initial.files))
        fail('RECORD', '首次外置部署的原始事务映射缺失。');
      const proxy = initial.files.find(row => row.role === 'game-proxy' && key(row.file) === key(proxyFile));
      const config = initial.files.find(row => row.role === 'reshade-config' && key(row.file) === key(configFile));
      if (!proxy || !config || proxy.snapshot !== value.initialProxy.snapshot || proxy.before !== value.initialProxy.before ||
          proxy.after !== value.initialProxy.after || value.initialProxy.owned !== (proxy.before !== proxy.after) ||
          config.before !== (value.originalConfigExisted ? hash(Buffer.from(value.originalReShadeConfig)) : null) ||
          value.mode === 'external' && (proxy.after !== value.proxy.sha256 || config.after !== value.loaderConfigHash))
        fail('RECORD', '首次外置恢复目标与原事务映射不一致。');
    }
    const names = new Set();
    for (const row of value.files) {
      if (!row || !leaf(row.name) || names.has(row.name.toLowerCase()) || !HASH.test(row.sha256 || '') ||
          !HASH.test(row.originHash || '') || !allowed(t, path.join(t.runtimeDir, row.name), row.role) ||
          typeof row.mutable !== 'boolean' || typeof row.localOwned !== 'boolean')
        fail('RECORD', '外置部署记录含未知或重复文件，已保留现状。');
      names.add(row.name.toLowerCase());
    }
    return value;
  }
  function record(t) {
    const value = readJson(t.receipt);
    if (value?.mode === 'local' && typeof value.exe === 'string' && key(value.exe) !== key(t.exe)) {
      // An inactive external history for EXE A must not prevent a new,
      // separately confirmed EXE B after A's ordinary installation is removed.
      validateReceipt(target({ dir: t.gameRoot, exe: value.exe }), value); return null;
    }
    return validateReceipt(t, value);
  }
  function pending(t) {
    const value = readJson(t.pending);
    if (value && (value.product !== PRODUCT || value.version !== 1 || value.id !== t.id)) fail('RECOVERY_INVALID', '外置待恢复记录归属不符。');
    return value;
  }
  async function closed(t) { await noLinks(t.exe); await guards.assertGameClosed(t.gameRoot, t.exe); }
  async function assertReady(game) {
    const t = target(game);
    if (pending(t)) fail('RECOVERY_REQUIRED', '上次部署未完成，请先恢复未完成部署。');
    for (const rel of ['_DLSS5_Backup/pending-switch.json', '_DLSS5_Backup/xiaofeng-environment-pending.json']) {
      if (fs.existsSync(path.join(t.gameRoot, rel))) fail('OTHER_RECOVERY', '游戏有其他未完成文件操作，请先使用对应恢复入口。');
    }
    if (fs.existsSync(path.join(t.runtimeDir, '_DLSS5_Backup/pending-switch.json')))
      fail('OTHER_RECOVERY', '外置组件有未完成文件操作，请先使用对应恢复入口。');
  }
  function getLayout(game) {
    const t = target(game), saved = record(t), interrupted = pending(t), external = saved?.mode === 'external';
    const runtimeDir = external ? t.runtimeDir : t.dir;
    if (external) {
      noLinksSync(t.runtimeDir);
      const ini = path.join(t.dir, 'ReShade.ini');
      if (!interrupted && (!fs.existsSync(ini) || hash(fs.readFileSync(ini)) !== saved.loaderConfigHash))
        fail('FILE_CHANGED', '游戏的 ReShade 运行目录指向已被修改，未猜测配置位置。', { file: ini });
      if (!interrupted && saved.loadingMode === 'helper' && ['dxgi.dll', 'd3d12.dll'].some(name => fs.existsSync(path.join(t.dir, name))))
        fail('HELPER_PROXY_CONFLICT', '助手加载时游戏目录出现代理 DLL，已阻止重复加载。');
      if (!interrupted && saved.origin === 'direct' && saved.loadingMode !== 'helper') {
        const proxy = path.join(t.dir, saved.proxy.name); noLinksSync(proxy);
        const stat = fs.statSync(proxy);
        if (!stat.isFile() || stat.size > MAX_FILE || hash(fs.readFileSync(proxy)) !== saved.proxy.sha256)
          fail('FILE_CHANGED', '首次外置部署的游戏加载器已被修改，未接管新文件。', { file: proxy });
      }
    }
    const profile = inspectProfile(t.dir, options.environment || process.env);
    if (external && !interrupted && profile.ok && (key(profile.baseDir) !== key(t.runtimeDir) || key(profile.addonDir) !== key(t.runtimeDir)))
      fail('FILE_CHANGED', '当前 ReShade 运行或插件目录与受管外置目录不一致，请先核对配置。');
    const local = external ? { ok: profile.ok } : inspectAddonLayout(t.dir);
    const configured = profile.configured, addonDir = profile.addonDir;
    const desired = external ? { loaderDir: t.dir, baseDir: t.runtimeDir, addonDir: t.runtimeDir, activeConfigPath: path.join(t.runtimeDir, 'ReShade.ini') } : configured;
    return { mode: external ? 'external' : 'local', source: external ? PRODUCT : 'game-directory', exe: t.exe,
      gameRoot: t.gameRoot, loaderDir: t.dir, runtimeDir: external ? runtimeDir : profile.baseDir, addonDir, addonDirectory: addonDir,
      activeConfigPath: configured.activeConfigPath, reshadeConfigDir: configured.baseDir, nrConfigDir: addonDir,
      configured, sourceLayout: saved?.sourceLayout || profile.configured, desired,
      warnings: profile.warnings, origin: saved?.origin || 'native',
      logDirs: [...new Set([configured.baseDir, addonDir])], verified: !interrupted && local.ok && profile.ok,
      blockers: interrupted ? ['上次部署尚未恢复'] : !profile.ok ? profile.blockers : local.ok ? [] : [local.code],
      needsRecovery: Boolean(interrupted), generation: saved?.generation || null, version: external ? saved.payloadVersion : readManifest(t.gameRoot)?.payloadVersion || null, api: saved?.api || null,
      loadingMode: external ? saved.loadingMode || 'proxy' : 'proxy',
      loaderPath: external && saved.loadingMode === 'helper' ? path.join(t.runtimeDir, 'ReShade64.dll') : null,
      proxyPaths: saved?.proxy ? [path.join(t.dir, saved.proxy.name)] : [],
      moduleManifest: external ? saved.files.filter(row => !row.mutable).map(row => ({ path: path.join(t.runtimeDir, row.name),
        name: row.name, kind: row.kind, role: ({ addon: 'core', bridge: 'chain', runtime: 'nr-runtime', carrier: 'carrier', loader: 'reshade' })[row.kind] || row.role,
        sha256: row.sha256, architecture: row.architecture ?? (MANAGED.has(row.kind) || row.kind === 'loader' ? 64 : null) })) : [] };
  }
  async function inspect(game) {
    const t = target(game), saved = record(t), interrupted = pending(t), localManifest = readManifest(t.gameRoot);
    let layout; try { layout = getLayout(game); } catch (error) {
      layout = { mode: saved?.mode || 'local', verified: false, blockers: [error.message], needsRecovery: Boolean(interrupted) };
    }
    const files = [];
    if (saved?.mode === 'external' && !interrupted) for (const row of saved.files) {
      let actual = null, reason = null;
      try { actual = await digest(path.join(t.runtimeDir, row.name)); } catch (error) { reason = error.message; }
      files.push({ ...row, actual, valid: actual !== null && (row.mutable || actual === row.sha256), reason });
    }
    return { ...layout, installed: Boolean(saved?.mode === 'external' || localManifest), pending: Boolean(interrupted),
      canRestore: Boolean(interrupted || saved?.mode === 'external'), version: saved?.mode === 'external' ? saved.payloadVersion : localManifest?.payloadVersion || null,
      api: saved?.api || classifyApi(game.scan?.chosen || game.chosen), baseline: saved?.baseline || null,
      previous: saved?.previous || null, current: saved ? { generation: saved.generation, mode: saved.mode, version: saved.payloadVersion,
        runtimeDir: saved.mode === 'external' ? t.runtimeDir : t.dir } : null,
      files, ready: Boolean(saved?.mode === 'external' || localManifest) && !interrupted && layout.verified && files.every(row => row.valid), runtimeVerified: false };
  }
  async function publish(temp, destination, replacing) {
    if (options.publish) return options.publish(temp, destination, replacing);
    if (process.platform !== 'win32') { await fsp.rename(temp, destination); return; }
    const literal = file => "[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + Buffer.from(file).toString('base64') + "'))";
    const script = "$ErrorActionPreference='Stop'; [IO.File]::" + (replacing ? 'Replace' : 'Move') +
      '(' + literal(temp) + ',' + literal(destination) + (replacing ? ',[NullString]::Value' : '') + ');';
    await execFile(path.join(process.env.SystemRoot || 'C:\\Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe'),
      ['-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')],
      { windowsHide: true, timeout: 15000, maxBuffer: 8192 });
  }
  async function atomicCopy(t, source, destination, expected, before) {
    await noLinks(destination); await fsp.mkdir(path.dirname(destination), { recursive: true });
    const staging = path.join(path.dirname(destination), '.xiaofeng-' + crypto.randomUUID() + '.part');
    try {
      await copy(source, staging, fs.constants.COPYFILE_EXCL);
      if (await digest(staging) !== expected || await digest(source) !== expected) fail('FILE_CHANGED', '复制内容校验失败，未提交目标文件。');
      const handle = await fsp.open(staging, 'r+'); try { await handle.sync(); } finally { await handle.close(); }
      await closed(t);
      if (await digest(destination) !== before) fail('FILE_CHANGED', '提交前目标被外部修改，未覆盖。', { file: destination });
      await publish(staging, destination, before !== null);
    } finally { await fsp.unlink(staging).catch(error => { if (error.code !== 'ENOENT') throw error; }); }
  }
  async function snapshot(source, destination, expected) {
    await noLinks(source); await noLinks(destination); await fsp.mkdir(path.dirname(destination), { recursive: true });
    await fsp.copyFile(source, destination, fs.constants.COPYFILE_EXCL);
    if (await digest(destination) !== expected || await digest(source) !== expected) fail('FILE_CHANGED', '创建恢复快照时文件改变。');
    const file = await fsp.open(destination, 'r+'); try { await file.sync(); } finally { await file.close(); }
  }
  function historyFile(t, operation, name) {
    if (!UUID.test(operation) || !/^(?:before|after)\/[0-9]+\.bin$/.test(name)) fail('RECOVERY_INVALID', '外置快照位置无效。');
    return path.join(t.ownerRoot, 'history', operation, name);
  }
  async function validateWal(t, wal) {
    if (!wal || wal.product !== PRODUCT || wal.version !== 1 || wal.id !== t.id || !UUID.test(wal.operation || '') ||
        key(wal.exe || '.') !== key(t.exe) || key(wal.gameRoot || '.') !== key(t.gameRoot) ||
        (wal.profileId || null) !== (t.profileId || null) ||
        !Array.isArray(wal.files) || wal.files.length > 256) fail('RECOVERY_INVALID', '外置恢复记录不完整。');
    if (wal.pid !== process.pid && Number.isInteger(wal.pid)) {
      try { process.kill(wal.pid, 0); fail('BUSY', '另一管理器仍在执行部署。'); } catch (error) { if (error.code !== 'ESRCH') throw error; }
    }
    const seen = new Set();
    for (let i = 0; i < wal.files.length; i++) {
      const row = wal.files[i];
      if (!allowed(t, row.file, row.role) || seen.has(key(row.file)) || row.before !== null && !HASH.test(row.before || '') ||
          row.after !== null && !HASH.test(row.after || '') || row.snapshot !== 'before/' + i + '.bin' || row.prepared !== 'after/' + i + '.bin')
        fail('RECOVERY_INVALID', '外置恢复记录含未授权目标。');
      seen.add(key(row.file));
      if (row.before !== null && await digest(historyFile(t, wal.operation, row.snapshot)) !== row.before)
        fail('BACKUP_CHANGED', '原始快照缺失或被修改，已保留恢复记录。');
      const current = await digest(row.file);
      if (current !== row.before && current !== row.after) fail('FILE_CHANGED', '部署目标被外部改变，未覆盖。', { file: row.file });
    }
  }
  async function rollback(t, wal) {
    await closed(t); await validateWal(t, wal);
    for (const row of [...wal.files].reverse()) {
      const current = await digest(row.file); if (current === row.before) continue;
      if (current !== row.after) fail('FILE_CHANGED', '恢复前目标已变化，已保留记录。', { file: row.file });
      if (row.before === null) {
        await closed(t); await noLinks(row.file);
        if (await digest(row.file) !== row.after) throw Object.assign(new Error('删除前目标已变化，已保留外部文件和恢复记录。'),
          { code: 'DEPLOYMENT_FILE_CHANGED', details: { file: row.file }, preservePending: true });
        await fsp.unlink(row.file);
      }
      else await atomicCopy(t, historyFile(t, wal.operation, row.snapshot), row.file, row.before, current);
    }
    await fsp.unlink(t.pending);
    return { recovered: true, runtimeVerified: false };
  }
  async function transaction(t, plan) {
    const wal = { version: 1, product: PRODUCT, id: t.id, operation: plan.operation, exe: t.exe,
      gameRoot: t.gameRoot, profileId: t.profileId, pid: process.pid, createdAt: new Date().toISOString(), files: [] };
    for (let i = 0; i < plan.operations.length; i++) {
      const row = plan.operations[i];
      if (!allowed(t, row.file, row.role)) fail('TARGET', '部署计划含未知目标。');
      if (await digest(row.file) !== row.before) fail('PLAN_CHANGED', '部署预览后的文件已经改变，请重新检查。', { file: row.file });
      const snapshotName = 'before/' + i + '.bin', prepared = 'after/' + i + '.bin';
      if (row.before !== null) await snapshot(row.file, historyFile(t, plan.operation, snapshotName), row.before);
      if (row.after !== null) {
        const preparedFile = historyFile(t, plan.operation, prepared);
        if (row.bytes !== undefined) {
          await noLinks(preparedFile); await fsp.mkdir(path.dirname(preparedFile), { recursive: true });
          await fsp.writeFile(preparedFile, row.bytes, { flag: 'wx' });
          const file = await fsp.open(preparedFile, 'r+'); try { await file.sync(); } finally { await file.close(); }
        } else await snapshot(row.source, preparedFile, row.after);
      }
      wal.files.push({ file: row.file, role: row.role, before: row.before, after: row.after, snapshot: snapshotName, prepared });
    }
    await validateWal(t, wal);
    // Keep the mapping from every saved byte snapshot to its original target,
    // including successful operations. A previous-version identity is not
    // useful recovery evidence if only anonymous numbered .bin files remain.
    await atomicJson(path.join(t.ownerRoot, 'history', plan.operation, 'operation.json'), wal);
    await atomicJson(t.pending, wal);
    try {
      for (let i = 0; i < wal.files.length; i++) {
        const row = wal.files[i];
        if (plan.sourceGuard && (row.role === 'game-proxy' || row.role === 'reshade-config' && key(row.file) === key(path.join(t.dir, 'ReShade.ini'))))
          await plan.sourceGuard();
        const current = await digest(row.file);
        if (current !== row.before) fail('FILE_CHANGED', '部署开始后目标被外部修改，未覆盖。', { file: row.file });
        if (row.after !== row.before) {
          await closed(t);
          if (row.after === null) {
            await noLinks(row.file);
            if (await digest(row.file) !== row.before) throw Object.assign(new Error('删除前目标已变化，已保留外部文件和恢复记录。'),
              { code: 'DEPLOYMENT_FILE_CHANGED', details: { file: row.file }, preservePending: true });
            await fsp.unlink(row.file);
          }
          else await atomicCopy(t, historyFile(t, wal.operation, row.prepared), row.file, row.after, current);
        }
        await options.afterWrite?.({ index: i, row, target: t });
      }
      await fsp.unlink(t.pending);
    } catch (error) {
      if (!error.preservePending) try { await rollback(t, wal); } catch (recoveryError) { error.recoveryError = recoveryError; error.preservePending = true; }
      if (error.preservePending) { error.details = { ...error.details, needsRecovery: true }; }
      throw error;
    }
  }
  async function collectLocal(t, manifest) {
    const files = [], names = new Set();
    for (const row of manifest.files) {
      if (!MANAGED.has(row.kind)) continue;
      const file = path.resolve(t.gameRoot, row.rel);
      if (key(path.dirname(file)) !== key(t.dir) || !allowed(t, file, 'managed')) fail('LAYOUT', '当前安装含其他运行目录，请先恢复普通安装。');
      const actual = await digest(file);
      if (!actual || row.kind !== 'config' && actual !== row.installedSha256) fail('FILE_CHANGED', '已安装文件缺失或被修改，未迁移。', { file });
      files.push({ name: path.basename(file), role: 'managed', kind: row.kind, mutable: row.kind === 'config',
        sha256: actual, originHash: actual, localOwned: true, source: file }); names.add(path.basename(file).toLowerCase());
    }
    const config = path.join(t.dir, INSTALLED_NAMES.config);
    if (!names.has(INSTALLED_NAMES.config.toLowerCase()) && await digest(config)) {
      const actual = await digest(config);
      files.push({ name: INSTALLED_NAMES.config, role: 'managed', kind: 'config', mutable: true, sha256: actual,
        originHash: actual, localOwned: false, source: config }); names.add(INSTALLED_NAMES.config.toLowerCase());
    }
    const entries = await fsp.readdir(t.dir, { withFileTypes: true });
    for (const entry of entries) if (ADDON.test(entry.name) && !names.has(entry.name.toLowerCase())) {
      if (!entry.isFile()) fail('LAYOUT', 'Add-on 不是普通文件，未迁移。');
      const file = path.join(t.dir, entry.name);
      if (classifyAddon(entry.name, file)) fail('CONFLICT', '目录仍有另一 NR 或旧桥接器，请先检查冲突。', { file });
      const actual = await digest(file);
      files.push({ name: entry.name, role: 'user-addon', kind: 'user-addon', mutable: false,
        sha256: actual, originHash: actual, localOwned: false, source: file }); names.add(entry.name.toLowerCase());
      const stem = entry.name.replace(ADDON, '').toLowerCase();
      for (const sidecar of entries) if (sidecar.isFile() && MUTABLE.test(sidecar.name) &&
          sidecar.name.replace(/\.[^.]+$/, '').toLowerCase() === stem && !names.has(sidecar.name.toLowerCase())) {
        const source = path.join(t.dir, sidecar.name), sideHash = await digest(source);
        files.push({ name: sidecar.name, role: 'user-sidecar', kind: 'user-sidecar', mutable: true,
          sha256: sideHash, originHash: sideHash, localOwned: false, source }); names.add(sidecar.name.toLowerCase());
      }
    }
    // Preserve statically imported sidecar DLLs alongside copied user Add-ons.
    // Their original files remain in place; system/game loaders are never copied.
    for (let index = 0; index < files.length; index++) if (['user-addon', 'user-dependency'].includes(files[index].role)) {
      let imports = []; try { imports = pe.getImports(files[index].source) || []; } catch {}
      for (const imported of imports) {
        const name = path.basename(imported);
        if (!leaf(name) || PROTECTED.test(name) || names.has(name.toLowerCase()) || !/\.dll$/i.test(name)) continue;
        const source = path.join(t.dir, name), actual = await digest(source); if (!actual) continue;
        files.push({ name, role: 'user-dependency', kind: 'user-dependency', mutable: false,
          sha256: actual, originHash: actual, localOwned: false, source }); names.add(name.toLowerCase());
      }
      if (files.length > 128) fail('LAYOUT', '同目录组件过多，未扩大迁移范围。');
    }
    return files;
  }
  async function sourceAddons(profile) {
    const rows = [], names = new Map(), directNames = [], checks = [];
    const entries = fs.existsSync(profile.addonDir) ? await fsp.readdir(profile.addonDir, { withFileTypes: true }) : [];
    const searched = row => ADDON.test(row.name) && !/\.addon32$/i.test(row.name);
    const inventory = entries.filter(searched).map(row => `${row.name}:${row.isFile() ? 'file' : 'other'}`).sort();
    async function add(source, role, mutable = false) {
      const name = path.basename(source), previous = names.get(name.toLowerCase());
      if (!leaf(name) || PROTECTED.test(name) || name.toLowerCase() === 'reshade.ini' ||
          Object.values(INSTALLED_NAMES).some(value => value.toLowerCase() === name.toLowerCase()))
        fail('SOURCE_CONFLICT', '原活动插件与新外置配套存在同名文件，未覆盖。', { file: source });
      if (previous) { if (key(previous.source) !== key(source)) fail('SOURCE_CONFLICT', '多个原插件目录有同名文件，无法安全合并。', { file: source }); return previous; }
      const actual = await digest(source); if (!actual) fail('SOURCE_CHANGED', '原活动插件缺失，未修改加载目录。', { file: source });
      if (role === 'user-addon' && classifyAddon(name, source)) fail('SOURCE_CONFLICT', '原活动目录含另一 NR 或 NGX 插件，请先核对冲突。', { file: source });
      const row = { name, role, kind: role, mutable, sha256: actual, originHash: actual, originPath: source, localOwned: false, source };
      names.set(name.toLowerCase(), row); rows.push(row); checks.push({ file: source, sha256: actual });
      if (rows.length > 128) fail('LAYOUT', '原活动插件范围过大。');
      return row;
    }
    for (const entry of entries) if (ADDON.test(entry.name) && !/\.addon32$/i.test(entry.name)) {
      if (!entry.isFile()) fail('SOURCE_LAYOUT', '原插件目录包含链接或非普通插件文件。');
      await add(path.join(profile.addonDir, entry.name), 'user-addon');
    }
    for (const explicit of profile.directLoads) {
      if (!ADDON.test(explicit.path) && !/\.dll$/i.test(explicit.path)) fail('SOURCE_LAYOUT', '无法识别显式加载的插件类型。');
      if (classifyAddon(path.basename(explicit.path), explicit.path)) fail('SOURCE_CONFLICT', '显式加载列表含另一 NR 或 NGX 插件，未重新加载。', { file: explicit.path });
      const row = await add(explicit.path, ADDON.test(explicit.path) ? 'user-addon' : 'user-dependency'); directNames.push(row.name);
    }
    for (let index = 0; index < rows.length; index++) {
      const row = rows[index];
      if (!['user-addon', 'user-dependency'].includes(row.role)) continue;
      const directory = path.dirname(row.source), stem = row.name.replace(/\.(?:addon(?:32|64)?|dll)$/i, '').toLowerCase();
      const siblings = await fsp.readdir(directory, { withFileTypes: true });
      for (const sibling of siblings) if (sibling.isFile() && MUTABLE.test(sibling.name) && sibling.name.replace(/\.[^.]+$/, '').toLowerCase() === stem)
        await add(path.join(directory, sibling.name), 'user-sidecar', true);
      let imports = []; try { imports = pe.getImports(row.source) || []; } catch {}
      for (const imported of imports) {
        if (!leaf(imported) || PROTECTED.test(imported) || !/\.dll$/i.test(imported)) continue;
        const source = path.join(directory, imported);
        if (fs.existsSync(source)) await add(source, 'user-dependency'); else checks.push({ file: source, sha256: null });
      }
    }
    const guard = async () => {
      for (const identity of profile.identities) if (await digest(identity.file) !== identity.sha256)
        fail('PLAN_CHANGED', '预览后原 ReShade 配置改变，请重新核对。');
      const current = fs.existsSync(profile.addonDir) ? await fsp.readdir(profile.addonDir, { withFileTypes: true }) : [];
      if (JSON.stringify(current.filter(searched).map(row => `${row.name}:${row.isFile() ? 'file' : 'other'}`).sort()) !== JSON.stringify(inventory))
        fail('PLAN_CHANGED', '预览后原插件目录内容改变，请重新核对。');
      for (const check of checks) if (await digest(check.file) !== check.sha256) fail('PLAN_CHANGED', '预览后原插件文件改变。', { file: check.file });
    };
    await guard();
    return { rows, directNames, guard };
  }
  async function directPlan(t, game, next, operations, extras = {}) {
    if (operations.length > 256) fail('LAYOUT', '外置变更范围过大。');
    for (const row of operations) {
      if (!allowed(t, row.file, row.role)) fail('TARGET', '外置预览含未知目标。');
      if (row.file.length > 259) fail('PATH_LENGTH', '外置路径过长。');
      noLinksSync(row.file); await noLinks(row.file);
    }
    const planId = crypto.randomUUID();
    plans.set(planId, { planId, game, target: t, operation: next.generation, operations, next,
      ...extras, exeHash: await digest(t.exe), expires: Date.now() + 5 * 60 * 1000 });
    const desired = { loaderDir: t.dir, baseDir: next.mode === 'external' ? t.runtimeDir : next.sourceLayout?.baseDir || t.dir,
      addonDir: next.mode === 'external' ? t.runtimeDir : next.sourceLayout?.addonDir || t.dir,
      activeConfigPath: next.mode === 'external' ? path.join(t.runtimeDir, 'ReShade.ini') : next.sourceLayout?.activeConfigPath || path.join(t.dir, 'ReShade.ini') };
    return { planId, gameId: game.id, fromMode: extras.fromMode || 'local', toMode: next.mode, mode: next.mode,
      api: next.api, version: next.payloadVersion, loadingMode: next.loadingMode, origin: next.origin,
      requiresFgRestore: extras.requiresFgRestore === true, requiresConfirmation: true,
      requiresAntiCheat: Boolean(guards.antiCheatPresent?.(t.gameRoot)), blockers: [], warnings: extras.warnings || [],
      configured: extras.configured || next.sourceLayout, sourceLayout: next.sourceLayout, desired,
      layout: { mode: next.mode, source: PRODUCT, runtimeDir: desired.baseDir, addonDir: desired.addonDir, addonDirectory: desired.addonDir,
        activeConfigPath: desired.activeConfigPath, configured: desired, sourceLayout: next.sourceLayout, desired,
        loadingMode: next.loadingMode, loaderPath: next.loadingMode === 'helper' ? path.join(t.runtimeDir, 'ReShade64.dll') : null,
        proxyPaths: next.proxy && next.loadingMode !== 'helper' ? [path.join(t.dir, next.proxy.name)] : [], verified: true, blockers: [] },
      retainedAddons: next.files.filter(row => row.role === 'user-addon').map(row => ({ name: row.name, origin: row.originPath, searched: true })),
      inactiveAddons: extras.inactiveAddons || [],
      changes: operations.map(row => ({ path: row.file, name: path.basename(row.file), role: row.role, beforeSha256: row.before, afterSha256: row.after,
        action: row.before === row.after ? 'keep' : row.after === null ? 'remove' : row.before === null ? 'create' : 'replace' })), runtimeVerified: false };
  }
  async function preserveInactiveProfile(t, profile) {
    const saved = record(t);
    if (!saved || saved.mode !== 'local' || inside(t.runtimeDir, profile.baseDir) || inside(t.runtimeDir, profile.addonDir) ||
        profile.directLoads.some(row => inside(t.runtimeDir, row.path)))
      fail('PROFILE_EXISTS', '旧外置目录没有完整停用记录，或当前配置仍在使用它，请先核对。');
    const history = readJson(path.join(t.ownerRoot, 'history', saved.generation, 'operation.json'));
    if (!history || history.product !== PRODUCT || history.id !== t.id || history.operation !== saved.generation ||
        key(history.exe || '.') !== key(t.exe) || key(history.gameRoot || '.') !== key(t.gameRoot) ||
        (history.profileId || null) !== (t.profileId || null) || !Array.isArray(history.files) ||
        !history.files.some(row => row.role === 'receipt' && key(row.file) === key(t.receipt) && row.after === hash(jsonBytes(saved))))
      fail('PROFILE_EXISTS', '旧停用收据无法由原事务映射核对，已保留旧目录。');
    const expected = new Map();
    for (const row of saved.files) {
      const file = path.join(t.runtimeDir, row.name), last = history.files.find(item => key(item.file) === key(file));
      expected.set(row.name.toLowerCase(), { file, sha256: last?.after === null ? null : row.sha256 });
    }
    const profileIni = path.join(t.runtimeDir, 'ReShade.ini'), iniRow = history.files.find(row => key(row.file) === key(profileIni));
    let iniHash, beta1IniHash;
    if (iniRow) iniHash = iniRow.after;
    else {
      // Older local restorations retained the profile INI rather than touching
      // it. Reconstruct its expected bytes from the committed local INI copy.
      const restored = history.files.find(row => row.role === 'reshade-config' && key(row.file) === key(path.join(t.dir, 'ReShade.ini')));
      if (!restored) fail('PROFILE_EXISTS', '旧配置没有可核对的恢复快照。');
      let text = '';
      if (restored.after !== null) {
        const file = historyFile(t, saved.generation, restored.prepared);
        if (await digest(file) !== restored.after) fail('BACKUP_CHANGED', '旧配置恢复快照已变化。');
        text = await fsp.readFile(file, 'utf8');
      }
      iniHash = hash(Buffer.from(externalConfig(text, t.dir, options.environment || process.env)));
      beta1IniHash = hash(Buffer.from(externalConfig(text, t.dir, options.environment || process.env, { pathVersion: 'beta1' })));
    }
    expected.set('reshade.ini', { file: profileIni, sha256: iniHash, beta1Sha256: beta1IniHash });
    for (const row of expected.values()) {
      const actual = await digest(row.file);
      if (actual !== row.sha256 && actual !== row.beta1Sha256)
        fail('PROFILE_EXISTS', '旧停用运行文件身份发生变化，未接管或移动。', { file: row.file });
    }
    const logs = new Set(['nr-before-sr.log', 'nr-before-sr.previous.log', 'reshade.log']);
    for (const entry of await fsp.readdir(t.runtimeDir, { withFileTypes: true })) {
      const file = path.join(t.runtimeDir, entry.name); noLinksSync(file);
      if (entry.name === '_DLSS5_Backup' && entry.isDirectory()) continue;
      if (entry.isFile() && (expected.has(entry.name.toLowerCase()) || logs.has(entry.name.toLowerCase()))) continue;
      fail('PROFILE_EXISTS', '旧外置目录含未归属的额外文件，已保留现状。', { file });
    }
    const backup = path.join(t.runtimeDir, '_DLSS5_Backup');
    if (['pending-switch.json', 'xiaofeng-fg-components.json', 'xiaofeng-fg-migration.json'].some(name => fs.existsSync(path.join(backup, name))))
      fail('OTHER_RECOVERY', '旧外置目录仍有活动组件或未完成记录，请先恢复对应组件。');
    return { ...t, profileId: saved.generation, runtimeDir: path.join(t.ownerRoot, 'active-' + saved.generation) };
  }
  async function previewInitial(t, game, request) {
    if (request.loadingMode && request.loadingMode !== 'proxy') fail('INITIAL_LOADING_MODE', '首次外置部署请先使用代理加载，再按独立预览切换助手加载。');
    const profile = inspectProfile(t.dir, options.environment || process.env);
    if (!profile.ok) fail('SOURCE_LAYOUT', profile.blockers.map(row => row.message).join('；'));
    const api = request.api || classifyApi(game.scan?.chosen || game.chosen), payload = request.payload;
    if (!['dx11', 'dx12'].includes(api)) fail('API', '首次外置部署需要明确的 DX11 或 DX12 路线。');
    if (fs.existsSync(t.runtimeDir) && (await fsp.readdir(t.runtimeDir)).length) {
      // Derive the next profile from a committed removal, so separately
      // compiled previews refer to the same paths. The old history stays put.
      t = await preserveInactiveProfile(t, profile);
    }
    const operation = crypto.randomUUID(), operations = [], rows = [], kinds = ['addon', 'bridge', 'runtime', 'config', 'reshade', ...(api === 'dx11' ? ['carrier'] : [])];
    for (const kind of kinds) {
      const spec = payload?.[kind];
      if (!spec?.file || !HASH.test(spec.actual || '') || await digest(spec.file) !== spec.actual) fail('PACKAGE', '首次外置部署缺少完整且可验证的配套。', { kind });
      if (kind !== 'config' && typeof pe.getBitness === 'function' && pe.getBitness(spec.file) !== 64) fail('ARCHITECTURE', '外置配套必须为 x64。', { kind });
      if (kind !== 'reshade') rows.push({ name: INSTALLED_NAMES[kind], kind, role: 'managed', mutable: kind === 'config',
        sha256: spec.actual, originHash: spec.actual, localOwned: false, originAbsent: true, source: spec.file, architecture: kind === 'config' ? null : 64 });
    }
    const marker = file => { const bytes = fs.readFileSync(file); return bytes.includes(Buffer.from('Searching for add-ons')) || bytes.includes(Buffer.from('Searching for add-ons', 'utf16le')); };
    if (!marker(payload.reshade.file)) fail('PACKAGE', '首次外置部署需要支持 Add-on 的 ReShade。');
    const proxies = [];
    for (const name of ['dxgi.dll', 'd3d12.dll']) {
      const file = path.join(t.dir, name), actual = await digest(file); if (!actual) continue;
      if (!marker(file) || typeof pe.getBitness === 'function' && pe.getBitness(file) !== 64)
        fail('PROXY_CONFLICT', '游戏目录含无法确认的代理，未替换。', { file });
      proxies.push({ name, actual });
    }
    if (proxies.length > 1) fail('PROXY_CONFLICT', '游戏目录存在多个加载器，未猜测当前入口。');
    const source = await sourceAddons(profile); rows.push(...source.rows);
    const personalConfig = path.join(profile.addonDir, INSTALLED_NAMES.config);
    let personalConfigHash = null;
    if (fs.existsSync(personalConfig)) {
      const stat = await fsp.stat(personalConfig);
      if (!stat.isFile() || stat.size > 1024 * 1024) fail('SOURCE_LAYOUT', '原 NR 配置不是可检查的文件。');
      personalConfigHash = await digest(personalConfig);
      Object.assign(rows.find(row => row.kind === 'config'), { source: personalConfig, sha256: personalConfigHash,
        originHash: personalConfigHash, originPath: personalConfig, originAbsent: false });
    }
    const sourceGuard = async () => {
      await source.guard();
      if (await digest(personalConfig) !== personalConfigHash) fail('PLAN_CHANGED', '原 NR 配置在预览后改变，未覆盖。');
    };
    await sourceGuard();
    if (fs.existsSync(t.runtimeDir) && (await fsp.readdir(t.runtimeDir)).length) fail('PROFILE_EXISTS', '目标外置目录已有未接管文件，请先核对旧记录。');
    async function change(file, role, source, bytes, after) {
      const before = await digest(file), value = bytes === undefined ? undefined : Buffer.from(bytes);
      operations.push({ file, role, source, ...(value === undefined ? {} : { bytes: value }), before, after: value === undefined ? after : hash(value) });
    }
    for (const row of rows) await change(path.join(t.runtimeDir, row.name), row.role, row.source, undefined, row.sha256);
    const activeConfig = externalConfig(profile.config, profile.baseDir, options.environment || process.env, { directLoads: source.directNames });
    await change(path.join(t.runtimeDir, 'ReShade.ini'), 'reshade-config', null, activeConfig);
    const proxy = { name: proxies[0]?.name || 'dxgi.dll', sha256: payload.reshade.actual }, proxyBefore = proxies[0]?.actual || null;
    const proxyIndex = operations.length;
    await change(path.join(t.dir, proxy.name), 'game-proxy', payload.reshade.file, undefined, proxy.sha256);
    if (operations[proxyIndex].before !== proxyBefore) fail('PLAN_CHANGED', '检查期间原加载器改变。');
    const rootConfig = loaderConfig(profile.rootConfig, t.runtimeDir);
    await change(profile.rootConfigPath, 'reshade-config', null, rootConfig);
    const manifest = newManifest(t.gameRoot, t.exe, api); manifest.payloadVersion = payload.version; manifest.deploymentApi = api; manifest.metadataOnly = true;
    manifest.files = rows.filter(row => row.role === 'managed').map(row => ({ rel: path.relative(t.gameRoot, path.join(t.dir, row.name)), kind: row.kind,
      installedSha256: row.sha256, original: { existed: false } }));
    const next = { version: 1, product: PRODUCT, id: t.id, exe: t.exe, gameRoot: t.gameRoot, runtimeDir: t.runtimeDir,
      profileId: t.profileId,
      generation: operation, mode: 'external', loadingMode: 'proxy', proxy, api, payloadVersion: payload.version, updatedAt: new Date().toISOString(),
      origin: 'direct', initialOperation: operation,
      initialProxy: { name: proxy.name, before: proxyBefore, after: proxy.sha256, owned: proxyBefore !== proxy.sha256, snapshot: `before/${proxyIndex}.bin` },
      localManifest: manifest, currentManifest: structuredClone(manifest), sourceLayout: profile.configured,
      originalReShadeConfig: profile.rootConfig, originalConfigExisted: profile.identities.find(row => row.file === profile.rootConfigPath)?.sha256 !== null, loaderConfigHash: hash(Buffer.from(rootConfig)),
      baseline: { installed: false, version: null, ordinarySnapshot: operation, ordinarySnapshotDirectory: path.join(t.ownerRoot, 'history', operation) },
      previous: { mode: 'local', version: null, generation: null }, files: rows.map(({ source, ...row }) => row) };
    await change(t.receipt, 'receipt', null, jsonBytes(next));
    const activeOrigins = new Set(source.rows.map(row => key(row.originPath))), inactiveAddons =
      (await fsp.readdir(t.dir, { withFileTypes: true })).filter(row => row.isFile() && ADDON.test(row.name) && !activeOrigins.has(key(path.join(t.dir, row.name))))
        .map(row => ({ name: row.name, path: path.join(t.dir, row.name), active: false, action: 'preserve' }));
    return directPlan(t, game, next, operations, { sourceGuard, warnings: profile.warnings, inactiveAddons, configured: profile.configured });
  }
  async function previewLocalDirect(t, game, saved, request, internal) {
    if (key(saved.sourceLayout?.baseDir || '.') !== key(t.dir) || key(saved.sourceLayout?.addonDir || '.') !== key(t.dir))
      fail('DIRECT_LOCAL_UNSUPPORTED', '原配置使用自定义插件目录，普通目录路线尚不能接入。可继续外置部署，或使用独立卸载恢复原配置。');
    getLayout(game);
    const requiresFgRestore = ['xiaofeng-fg-components.json', 'xiaofeng-fg-migration.json'].some(name => fs.existsSync(path.join(t.runtimeDir, '_DLSS5_Backup', name)));
    if (requiresFgRestore && internal.plannedFgRestore !== true) fail('FG_RESTORE_FIRST', '请先恢复外置补帧组件。');
    const operation = crypto.randomUUID(), operations = [], manifest = newManifest(t.gameRoot, t.exe, saved.api);
    manifest.installId = saved.generation;
    manifest.payloadVersion = request.payload?.version || saved.payloadVersion; manifest.deploymentApi = saved.api;
    async function change(file, role, source, bytes, after) {
      const before = await digest(file), value = bytes === undefined ? undefined : Buffer.from(bytes);
      if (role === 'original-backup' && before !== null) fail('BACKUP_CONFLICT', '新普通安装的备份位置已有文件，未覆盖。', { file });
      operations.push({ file, role, source, ...(value === undefined ? {} : { bytes: value }), before, after: value === undefined ? after : hash(value) });
    }
    for (const row of saved.files) {
      const source = path.join(t.runtimeDir, row.name), actual = await digest(source);
      if (!actual || !row.mutable && actual !== row.sha256) fail('FILE_CHANGED', '外置组件已变动，未迁移到普通目录。', { file: source });
      if (row.role === 'profile-loader') continue;
      const destination = path.join(t.dir, row.name), before = await digest(destination);
      if (row.role === 'managed') {
        // Existing same-byte DLLs remain unowned after a direct installation.
        // A local conversion must not acquire them merely by matching a hash.
        if (before && !row.mutable) fail('DIRECT_LOCAL_CONFLICT', '普通目录已有未归本管理器所有的配套文件，请继续外置或核对原文件。', { file: destination });
        const entry = { rel: path.relative(t.gameRoot, destination), kind: row.kind, installedSha256: actual, original: { existed: before !== null } };
        if (before) {
          const backup = backupPath(t.gameRoot, manifest.installId, entry.rel);
          entry.original = { existed: true, sha256: before, backupRel: path.relative(t.gameRoot, backup) };
          await change(backup, 'original-backup', destination, undefined, before);
        }
        const replacement = request.payload?.[row.kind];
        if (replacement && !row.mutable) {
          if (await digest(replacement.file) !== replacement.actual || typeof pe.getBitness === 'function' && pe.getBitness(replacement.file) !== 64) fail('PACKAGE', '普通目录配套来源改变。');
          entry.installedSha256 = replacement.actual;
        }
        await change(destination, row.role, replacement && !row.mutable ? replacement.file : source, undefined, entry.installedSha256);
        manifest.files.push(entry);
      } else if (row.originAbsent || row.mutable && actual !== row.originHash) {
        if (before !== (row.originAbsent ? null : row.originHash)) fail('FILE_CHANGED', '原用户插件或设置已变化，未覆盖。', { file: destination });
        await change(destination, row.role, source, undefined, actual);
      }
    }
    const initial = saved.initialProxy, proxyFile = path.join(t.dir, initial.name);
    if (saved.loadingMode === 'helper') await change(proxyFile, 'game-proxy', path.join(t.runtimeDir, 'ReShade64.dll'), undefined, saved.proxy.sha256);
    if (initial.owned) {
      const entry = { rel: path.relative(t.gameRoot, proxyFile), kind: 'reshade', installedSha256: saved.proxy.sha256, original: { existed: initial.before !== null } };
      if (initial.before) {
        const source = historyFile(t, saved.initialOperation, initial.snapshot), backup = backupPath(t.gameRoot, manifest.installId, entry.rel);
        if (await digest(source) !== initial.before) fail('BACKUP_CHANGED', '原加载器快照已变化。');
        entry.original = { existed: true, sha256: initial.before, backupRel: path.relative(t.gameRoot, backup) };
        await change(backup, 'original-backup', source, undefined, initial.before);
      }
      manifest.files.push(entry); manifest.reshadeRoute = path.basename(proxyFile, '.dll');
    }
    const currentConfig = await fsp.readFile(path.join(t.runtimeDir, 'ReShade.ini'), 'utf8');
    const restoredConfig = localConfig(currentConfig, saved.originalReShadeConfig, t.dir, options.environment || process.env);
    await change(path.join(t.dir, 'ReShade.ini'), 'reshade-config', null, restoredConfig || saved.originalConfigExisted ? restoredConfig : undefined, null);
    await change(manifestPath(t.gameRoot), 'manifest', null, jsonBytes(manifest));
    for (const row of saved.files) await change(path.join(t.runtimeDir, row.name), row.role, null, undefined, null);
    await change(path.join(t.runtimeDir, 'ReShade.ini'), 'reshade-config', null, undefined, null);
    const next = { ...saved, mode: 'local', loadingMode: 'proxy', generation: operation, payloadVersion: manifest.payloadVersion,
      localManifest: manifest, currentManifest: manifest, loaderConfigHash: hash(Buffer.from(restoredConfig)),
      previous: { mode: 'external', version: saved.payloadVersion, generation: saved.generation, snapshot: operation,
        snapshotDirectory: path.join(t.ownerRoot, 'history', operation) } };
    await change(t.receipt, 'receipt', null, jsonBytes(next));
    const result = await directPlan(t, game, next, operations, { fromMode: 'external', requiresFgRestore });
    return { ...result, projectedManifest: manifest };
  }
  async function preview(game, request = {}, internal = {}) {
    const t = target(game); await closed(t); await assertReady(game);
    if (!['local', 'external'].includes(request.mode) || request.loadingMode !== undefined && !['proxy', 'helper'].includes(request.loadingMode))
      fail('INPUT', '请选择普通或外置部署及加载方式。');
    const saved = record(t), external = saved?.mode === 'external';
    let manifest = readManifest(t.gameRoot);
    if (!manifest && !external && request.mode === 'external') return previewInitial(t, game, request);
    if (external && saved.origin === 'direct') {
      if (request.mode === 'local') return previewLocalDirect(t, game, saved, request, internal);
      manifest = saved.currentManifest || saved.localManifest;
    }
    if (!manifest) fail('INSTALL_FIRST', '请先完成本管理器的普通安装，再迁移运行目录。');
    assertManifestExecutable(t.gameRoot, manifest, t.exe);
    const api = request.api || saved?.api || manifest.deploymentApi || classifyApi(game.scan?.chosen || game.chosen);
    if (!['dx11', 'dx12'].includes(api)) fail('API', '本外置部署仅支持已有 DLSS 的 DX11 / DX12 路线。');
    const installedApi = external ? saved.api : manifest.deploymentApi || classifyApi(game.scan?.chosen || game.chosen);
    if (installedApi !== api) fail('API_CHANGE', '请先恢复普通目录，再应用新的 API 配套。');
    let requiresFgRestore = false;
    if (external !== (request.mode === 'external')) {
      const fromRoot = external ? t.runtimeDir : t.gameRoot;
      for (const name of ['xiaofeng-fg-components.json', 'xiaofeng-fg-migration.json']) {
        if (fs.existsSync(path.join(fromRoot, '_DLSS5_Backup', name))) {
          requiresFgRestore = true;
          if (internal.plannedFgRestore !== true) fail('FG_RESTORE_FIRST', '切换运行目录前请先恢复当前补帧组件，再在新目录重新准备。');
        }
      }
    }
    const loadingMode = request.mode === 'external' ? request.loadingMode || saved?.loadingMode || 'proxy' : 'proxy';
    const operation = crypto.randomUUID(), operations = [], rows = external ? saved.files.map(row => ({ ...row, source: path.join(t.runtimeDir, row.name) })) : await collectLocal(t, manifest);
    if (external) {
      getLayout(game);
      for (const row of rows) {
        const actual = await digest(row.source);
        if (!actual && !request.payload?.[row.kind] || actual && !row.mutable && actual !== row.sha256)
          fail('FILE_CHANGED', '外置组件缺失或被修改，未覆盖。', { file: row.source });
        row.sha256 = actual;
      }
      // Newly installed independent Add-ons are copied back as user-owned
      // files, rather than discarded merely because the NR receipt predates them.
      const known = new Set(rows.map(row => row.name.toLowerCase()));
      for (const entry of await fsp.readdir(t.runtimeDir, { withFileTypes: true })) {
        if (known.has(entry.name.toLowerCase()) || entry.name.toLowerCase() === 'reshade.ini' || !ADDON.test(entry.name) && !MUTABLE.test(entry.name)) continue;
        if (!entry.isFile()) fail('LAYOUT', '外置目录含无法迁移的组件。');
        const role = ADDON.test(entry.name) ? 'user-addon' : 'user-sidecar', source = path.join(t.runtimeDir, entry.name);
        const actual = await digest(source), origin = await digest(path.join(t.dir, entry.name));
        if (origin) fail('CONFLICT', '新外置组件在游戏目录已有同名文件，未覆盖。', { file: entry.name });
        rows.push({ name: entry.name, role, kind: role, mutable: MUTABLE.test(entry.name), sha256: actual,
          originHash: actual, originAbsent: true, localOwned: false, source });
      }
    } else requireAddonLayout(t.dir);
    const originalConfig = external ? saved.originalReShadeConfig : await fsp.readFile(path.join(t.dir, 'ReShade.ini'), 'utf8').catch(error => {
      if (error.code === 'ENOENT') return ''; throw error;
    });
    const currentConfig = external ? await fsp.readFile(path.join(t.runtimeDir, 'ReShade.ini'), 'utf8') : originalConfig;
    const currentManifest = external ? structuredClone(saved.currentManifest || saved.localManifest) : structuredClone(manifest);
    let proxy = saved?.proxy || null;
    if (loadingMode === 'helper' && (!external || saved.loadingMode !== 'helper')) {
      const candidates = [];
      for (const name of ['dxgi.dll', 'd3d12.dll']) {
        const file = path.join(t.dir, name), actual = await digest(file);
        if (!actual) continue;
        let addonLoader = false;
        try {
          const bytes = await fsp.readFile(file);
          addonLoader = bytes.includes(Buffer.from('Searching for add-ons')) || bytes.includes(Buffer.from('Searching for add-ons', 'utf16le'));
        } catch {}
        if (!addonLoader) fail('HELPER_PROXY_CONFLICT', '游戏目录含无法确认的代理 DLL，未切换助手加载。', { file });
        candidates.push({ name, file, sha256: actual });
      }
      if (candidates.length !== 1) fail('HELPER_LOADER', '助手加载需要唯一、已确认支持 Add-on 的 ReShade 加载器。');
      proxy = { name: candidates[0].name, sha256: candidates[0].sha256 };
      const existingLoader = rows.find(row => row.role === 'profile-loader');
      const loader = { name: 'ReShade64.dll', role: 'profile-loader', kind: 'loader', mutable: false,
        sha256: proxy.sha256, originHash: proxy.sha256, localOwned: false, source: candidates[0].file };
      if (existingLoader) Object.assign(existingLoader, loader); else rows.push(loader);
    }
    let payloadVersion = external ? saved.payloadVersion : manifest.payloadVersion;
    if (request.payload) {
      const payload = request.payload;
      if (api === 'dx11' && !payload.carrier) fail('PACKAGE', 'DX11 外置部署需要完整配套桥接器。');
      for (const kind of ['addon', 'bridge', 'runtime', ...(api === 'dx11' ? ['carrier'] : [])]) {
        const spec = payload[kind];
        if (!spec?.file || !HASH.test(spec.actual || '') || await digest(spec.file) !== spec.actual) fail('PACKAGE', '配套来源或摘要无效。', { kind });
        if (typeof pe.getBitness === 'function' && pe.getBitness(spec.file) !== 64) fail('ARCHITECTURE', '外置 Core 配套必须为 x64。', { kind });
        const row = rows.find(value => value.kind === kind);
        if (!row) fail('PACKAGE', '当前安装记录缺少必要组件，请先修复普通安装。', { kind });
        row.source = spec.file; row.sha256 = spec.actual;
        const entry = currentManifest.files.find(value => value.kind === kind);
        if (entry) entry.installedSha256 = spec.actual;
      }
      payloadVersion = payload.version || request.version || payloadVersion;
      currentManifest.payloadVersion = payloadVersion;
      currentManifest.updatedAt = new Date().toISOString();
    }
    for (const row of rows.filter(row => !row.mutable)) {
      if (typeof pe.getBitness === 'function') {
        row.architecture = pe.getBitness(row.source);
        if ((MANAGED.has(row.kind) || row.kind === 'loader') && row.architecture !== 64)
          fail('ARCHITECTURE', '当前 Core 或 ReShade 加载器不是 x64。', { file: row.name });
      }
    }
    async function change(file, role, source, bytes, after) {
      const before = await digest(file), value = bytes === undefined ? undefined : Buffer.from(bytes);
      operations.push({ file, role, source, ...(value === undefined ? {} : { bytes: value }), before,
        after: value !== undefined ? hash(value) : after });
    }
    const toExternal = request.mode === 'external', activeConfig = toExternal ? external ? currentConfig : externalConfig(originalConfig, t.dir)
      : localConfig(currentConfig, originalConfig, t.dir);
    if (toExternal) {
      for (const row of rows) await change(path.join(t.runtimeDir, row.name), row.role, row.source, undefined, row.sha256);
      await change(path.join(t.runtimeDir, 'ReShade.ini'), 'reshade-config', null, activeConfig);
      if (!external) for (const row of rows.filter(value => value.localOwned)) await change(path.join(t.dir, row.name), row.role, null, undefined, null);
    } else if (external) {
      for (const row of rows.filter(value => value.role !== 'profile-loader')) {
        const destination = path.join(t.dir, row.name), current = await digest(destination);
        const allowedOrigin = row.localOwned || row.originAbsent ? null : row.originHash;
        if (current !== allowedOrigin) fail('FILE_CHANGED', '普通目录出现外部修改，未覆盖。', { file: destination });
        await change(destination, row.role, row.source, undefined, row.sha256);
      }
      await change(manifestPath(t.gameRoot), 'manifest', null, jsonBytes(currentManifest));
    }
    if (proxy && loadingMode === 'helper' && (!external || saved.loadingMode !== 'helper'))
      await change(path.join(t.dir, proxy.name), 'game-proxy', null, undefined, null);
    else if (proxy && external && saved.loadingMode === 'helper' && loadingMode !== 'helper') {
      const destination = path.join(t.dir, proxy.name);
      if (await digest(destination) !== null) fail('FILE_CHANGED', '游戏目录出现新的加载器，未覆盖。', { file: destination });
      await change(destination, 'game-proxy', path.join(t.runtimeDir, 'ReShade64.dll'), undefined, proxy.sha256);
    }
    const rootConfig = toExternal ? loaderConfig(originalConfig, t.runtimeDir) : activeConfig;
    const originalConfigExisted = external ? saved.originalConfigExisted !== false : fs.existsSync(path.join(t.dir, 'ReShade.ini'));
    if (toExternal || external) {
      if (!toExternal && !originalConfigExisted && !rootConfig) await change(path.join(t.dir, 'ReShade.ini'), 'reshade-config', null, undefined, null);
      else await change(path.join(t.dir, 'ReShade.ini'), 'reshade-config', null, rootConfig);
    }
    const next = { version: 1, product: PRODUCT, id: t.id, exe: t.exe, gameRoot: t.gameRoot, runtimeDir: t.runtimeDir,
      profileId: t.profileId,
      generation: operation, mode: request.mode, loadingMode, proxy, api, payloadVersion, updatedAt: new Date().toISOString(),
      localManifest: saved?.mode === 'external' ? saved.localManifest : manifest, currentManifest,
      sourceLayout: saved?.mode === 'external' && saved.sourceLayout || { loaderDir: t.dir, baseDir: t.dir, addonDir: t.dir, activeConfigPath: path.join(t.dir, 'ReShade.ini') },
      originalReShadeConfig: originalConfig, originalConfigExisted, loaderConfigHash: hash(Buffer.from(rootConfig)),
      baseline: saved?.mode === 'external' ? saved.baseline : { installId: manifest.installId, version: manifest.payloadVersion,
        api: installedApi, ordinarySnapshot: operation, ordinarySnapshotDirectory: path.join(t.ownerRoot, 'history', operation),
        originalBackupDirectory: path.join(t.gameRoot, '_DLSS5_Backup', 'xiaofeng-originals', manifest.installId) },
      previous: { mode: external ? 'external' : 'local', version: saved?.payloadVersion || manifest.payloadVersion,
        generation: saved?.generation || null, snapshot: operation, snapshotDirectory: path.join(t.ownerRoot, 'history', operation) },
      files: rows.map(({ source, ...row }) => row) };
    if (external && saved?.origin === 'direct') Object.assign(next, { origin: 'direct', initialOperation: saved.initialOperation,
      initialProxy: saved.initialProxy, sourceLayout: saved.sourceLayout });
    await change(t.receipt, 'receipt', null, jsonBytes(next));
    if (operations.length > 256 || rows.length > 128) fail('LAYOUT', '迁移范围过大。');
    for (const file of [...operations.map(row => row.file), path.join(t.runtimeDir, 'nr-before-sr.previous.log')]) {
      if (file.length > 259) fail('PATH_LENGTH', '外置运行路径过长，请使用较短的管理器数据目录。');
      noLinksSync(file); await noLinks(file);
    }
    const planId = crypto.randomUUID();
    plans.set(planId, { planId, game, target: t, operation, operations, next, requiresFgRestore, expires: Date.now() + 5 * 60 * 1000,
      exeHash: await digest(t.exe) });
    const configured = getLayout(game).configured;
    const desired = { loaderDir: t.dir, baseDir: toExternal ? t.runtimeDir : t.dir, addonDir: toExternal ? t.runtimeDir : t.dir,
      activeConfigPath: path.join(toExternal ? t.runtimeDir : t.dir, 'ReShade.ini') };
    return { planId, gameId: game.id, fromMode: external ? 'external' : 'local', toMode: request.mode, mode: request.mode,
      api, version: payloadVersion, loadingMode, requiresFgRestore, requiresConfirmation: true, requiresAntiCheat: Boolean(guards.antiCheatPresent?.(t.gameRoot)),
      configured, sourceLayout: next.sourceLayout, desired, blockers: [], warnings: getLayout(game).warnings,
      layout: { mode: request.mode, runtimeDir: toExternal ? t.runtimeDir : t.dir, addonDir: desired.addonDir, addonDirectory: toExternal ? t.runtimeDir : t.dir,
        configured: desired, sourceLayout: next.sourceLayout, desired, verified: true, blockers: [],
        activeConfigPath: path.join(toExternal ? t.runtimeDir : t.dir, 'ReShade.ini'),
        loadingMode, loaderPath: loadingMode === 'helper' ? path.join(t.runtimeDir, 'ReShade64.dll') : null,
        proxyPaths: proxy ? [path.join(t.dir, proxy.name)] : [] },
      retainedAddons: rows.filter(row => row.role === 'user-addon').map(row => ({ name: row.name, origin: path.join(t.dir, row.name) })),
      changes: operations.map(row => ({ path: row.file, name: path.basename(row.file), role: row.role,
        beforeSha256: row.before, afterSha256: row.after, action: row.before === row.after ? 'keep' : row.after === null ? 'remove' : row.before === null ? 'create' : 'replace' })),
      projectedManifest: currentManifest, runtimeVerified: false };
  }
  async function apply(planId, consent = {}) {
    const plan = plans.get(planId); plans.delete(planId);
    if (!plan || plan.expires < Date.now()) fail('PLAN_EXPIRED', '部署预览已失效，请重新检查。');
    const t = plan.target;
    if (active.has(t.id)) fail('BUSY', '该游戏正在部署。');
    active.add(t.id);
    try {
      await closed(t); await assertReady(plan.game);
      if (plan.requiresFgRestore) fail('PLAN_CHANGED', '补帧恢复后需要按同一请求重新生成部署计划。');
      if (guards.antiCheatPresent?.(t.gameRoot) && consent.allowAntiCheat !== true) {
        const error = new Error('该游戏含反作弊组件，请先明确确认部署风险。');
        error.code = 'ERR_ANTI_CHEAT_CONFIRM'; throw error;
      }
      if (await digest(t.exe) !== plan.exeHash) fail('PLAN_CHANGED', '预览后游戏 EXE 已改变。');
      if (plan.sourceGuard) await plan.sourceGuard();
      await transaction(t, plan);
      return { applied: true, mode: plan.next.mode, version: plan.next.payloadVersion, api: plan.next.api,
        generation: plan.next.generation, layout: getLayout(plan.game), runtimeVerified: false };
    } finally { active.delete(t.id); }
  }
  async function recover(game) {
    const t = target(game, true); if (active.has(t.id)) fail('BUSY', '该游戏正在部署。');
    active.add(t.id); try { const wal = pending(t); return wal ? await rollback(t, wal) : { recovered: false }; }
    finally { active.delete(t.id); }
  }
  function direct(game) { const saved = record(target(game)); return saved?.mode === 'external' && saved.origin === 'direct'; }
  async function previewRemove(game, mode = 'restore', internal = {}) {
    const t = target(game), saved = record(t); await closed(t); await assertReady(game);
    if (saved?.origin !== 'direct' || saved.mode !== 'external' || !['clean', 'restore'].includes(mode)) fail('REMOVE', '该部署没有可使用的首次外置卸载记录。');
    getLayout(game);
    const requiresFgRestore = ['xiaofeng-fg-components.json', 'xiaofeng-fg-migration.json'].some(name => fs.existsSync(path.join(t.runtimeDir, '_DLSS5_Backup', name)));
    if (requiresFgRestore && internal.plannedFgRestore !== true) fail('FG_RESTORE_FIRST', '请先恢复外置目录中的补帧组件。');
    const operation = crypto.randomUUID(), operations = [];
    async function change(file, role, source, bytes, after) {
      const before = await digest(file), value = bytes === undefined ? undefined : Buffer.from(bytes);
      operations.push({ file, role, source, ...(value === undefined ? {} : { bytes: value }), before, after: value === undefined ? after : hash(value) });
    }
    for (const row of saved.files) {
      const file = path.join(t.runtimeDir, row.name), actual = await digest(file);
      if (!actual || !row.mutable && actual !== row.sha256) fail('FILE_CHANGED', '外置文件已变动，未删除或覆盖。', { file });
      await change(file, row.role, null, undefined, null);
    }
    await change(path.join(t.runtimeDir, 'ReShade.ini'), 'reshade-config', null, undefined, null);
    const initial = saved.initialProxy, proxyFile = path.join(t.dir, initial.name), currentProxy = await digest(proxyFile);
    const expectedProxy = saved.loadingMode === 'helper' ? null : saved.proxy.sha256;
    if (currentProxy !== expectedProxy) fail('FILE_CHANGED', '当前游戏加载器已变动，未覆盖。', { file: proxyFile });
    const restoreOriginal = mode === 'restore' || !initial.owned;
    if (restoreOriginal && initial.before !== null) {
      const source = historyFile(t, saved.initialOperation, initial.snapshot);
      if (await digest(source) !== initial.before) fail('BACKUP_CHANGED', '原加载器快照缺失或已改变。');
      await change(proxyFile, 'game-proxy', source, undefined, initial.before);
    } else await change(proxyFile, 'game-proxy', null, undefined, null);
    await change(path.join(t.dir, 'ReShade.ini'), 'reshade-config', null,
      saved.originalConfigExisted ? saved.originalReShadeConfig : undefined, null);
    const next = { ...saved, generation: operation, mode: 'local', loadingMode: 'proxy', updatedAt: new Date().toISOString(),
      previous: { mode: 'external', version: saved.payloadVersion, generation: saved.generation, snapshot: operation,
        snapshotDirectory: path.join(t.ownerRoot, 'history', operation) }, removed: true, uninstallMode: mode,
      loaderConfigHash: hash(Buffer.from(saved.originalReShadeConfig)) };
    await change(t.receipt, 'receipt', null, jsonBytes(next));
    const result = await directPlan(t, game, next, operations, { fromMode: 'external', requiresFgRestore });
    return { ...result, mode, phases: ['external-uninstall'], changes: result.changes.map(row => ({ ...row, phase: 'external-uninstall' })), removed: true, archiveDirectory: path.join(t.ownerRoot, 'history', operation) };
  }
  async function remove(game, mode = 'restore', consent = {}) {
    await recover(game);
    const plan = await previewRemove(game, mode), result = await apply(plan.planId, consent);
    return { ...result, mode, deploymentMode: 'local', removed: true, restored: mode === 'restore', uninstallMode: mode, archiveDirectory: plan.archiveDirectory };
  }
  async function restore(game, consent = {}) {
    await recover(game);
    const t = target(game, true), saved = record(t);
    if (saved?.mode !== 'external') return { restored: false, unchanged: true };
    const boundGame = { ...game, scan: { ...game.scan, chosen: { ...game.scan?.chosen, path: t.exe, bitness: 64, apiResolution: { api: saved.api } } } };
    if (saved.origin === 'direct') return remove(boundGame, 'restore', consent);
    const plan = await preview(boundGame, { mode: 'local' });
    return { ...await apply(plan.planId, consent), restored: true };
  }
  function owned(game) { return record(target(game))?.mode === 'external'; }
  return { getLayout, inspect, preview, apply, restore, recover, assertReady, owned, direct, previewRemove, remove,
    location: game => { const t = target(game), desired = { loaderDir: t.dir, baseDir: t.runtimeDir, addonDir: t.runtimeDir,
      activeConfigPath: path.join(t.runtimeDir, 'ReShade.ini') };
      return { runtimeDir: t.runtimeDir, addonDir: t.runtimeDir, addonDirectory: t.runtimeDir,
        activeConfigPath: desired.activeConfigPath, desired }; } };
}
module.exports = { createExternalRuntime, PRODUCT, RECEIPT, PENDING, digest };
