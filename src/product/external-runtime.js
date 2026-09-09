'use strict';
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { promisify } = require('node:util');
const execFile = promisify(require('node:child_process').execFile);
const { noLinks, inside, atomicJson } = require('./launch-safety');
const { readManifest, manifestPath, assertManifestExecutable } = require('./manifest');
const { requireAddonLayout, inspectAddonLayout } = require('./reshade-layout');
const { externalConfig, localConfig, loaderConfig } = require('./external-profile-config');
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
    let exe = game.scan?.chosen?.path || game.chosen?.path || game.exe;
    if (recovering) exe = (readJson(path.join(gameRoot, PENDING)) || readJson(path.join(gameRoot, RECEIPT)))?.exe || exe;
    if (!exe) exe = (readJson(path.join(gameRoot, RECEIPT)) || readJson(path.join(gameRoot, PENDING)))?.exe;
    if (!path.isAbsolute(game.dir || game.gameRoot || '') || !path.isAbsolute(exe || '') ||
        !inside(gameRoot, exe) || !/\.exe$/i.test(exe)) fail('TARGET', '请先确认实际游戏 EXE。');
    exe = path.resolve(exe);
    const id = hash(Buffer.from(key(exe))), ownerRoot = path.join(base, id);
    return { id, gameId: game.id, gameRoot, exe, dir: path.dirname(exe), ownerRoot,
      runtimeDir: path.join(ownerRoot, 'active'), receipt: path.join(gameRoot, RECEIPT), pending: path.join(gameRoot, PENDING) };
  }
  function allowed(t, file, role) {
    const absolute = path.resolve(file);
    if (role === 'receipt') return key(absolute) === key(t.receipt);
    if (role === 'manifest') return key(absolute) === key(manifestPath(t.gameRoot));
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
    assertManifestExecutable(t.gameRoot, value.localManifest, t.exe);
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
    }
    const local = external ? { ok: true } : inspectAddonLayout(t.dir);
    return { mode: external ? 'external' : 'local', source: external ? PRODUCT : 'game-directory', exe: t.exe,
      gameRoot: t.gameRoot, loaderDir: t.dir, runtimeDir, addonDir: runtimeDir, addonDirectory: runtimeDir,
      activeConfigPath: path.join(runtimeDir, 'ReShade.ini'), reshadeConfigDir: runtimeDir, nrConfigDir: runtimeDir,
      logDirs: [runtimeDir], verified: !interrupted && local.ok, blockers: interrupted ? ['上次部署尚未恢复'] : local.ok ? [] : [local.code],
      needsRecovery: Boolean(interrupted), generation: saved?.generation || null, version: saved?.payloadVersion || null, api: saved?.api || null,
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
      if (row.before === null) { await closed(t); await fsp.unlink(row.file); }
      else await atomicCopy(t, historyFile(t, wal.operation, row.snapshot), row.file, row.before, current);
    }
    await fsp.unlink(t.pending);
    return { recovered: true, runtimeVerified: false };
  }
  async function transaction(t, plan) {
    const wal = { version: 1, product: PRODUCT, id: t.id, operation: plan.operation, exe: t.exe,
      gameRoot: t.gameRoot, pid: process.pid, createdAt: new Date().toISOString(), files: [] };
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
        const row = wal.files[i], current = await digest(row.file);
        if (current !== row.before) fail('FILE_CHANGED', '部署开始后目标被外部修改，未覆盖。', { file: row.file });
        if (row.after !== row.before) {
          await closed(t);
          if (row.after === null) await fsp.unlink(row.file);
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
  async function preview(game, request = {}, internal = {}) {
    const t = target(game); await closed(t); await assertReady(game);
    if (!['local', 'external'].includes(request.mode) || request.loadingMode !== undefined && !['proxy', 'helper'].includes(request.loadingMode))
      fail('INPUT', '请选择普通或外置部署及加载方式。');
    const saved = record(t), external = saved?.mode === 'external', manifest = readManifest(t.gameRoot);
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
      generation: operation, mode: request.mode, loadingMode, proxy, api, payloadVersion, updatedAt: new Date().toISOString(),
      localManifest: saved?.mode === 'external' ? saved.localManifest : manifest, currentManifest,
      originalReShadeConfig: originalConfig, originalConfigExisted, loaderConfigHash: hash(Buffer.from(rootConfig)),
      baseline: saved?.mode === 'external' ? saved.baseline : { installId: manifest.installId, version: manifest.payloadVersion,
        api: installedApi, ordinarySnapshot: operation, ordinarySnapshotDirectory: path.join(t.ownerRoot, 'history', operation),
        originalBackupDirectory: path.join(t.gameRoot, '_DLSS5_Backup', 'xiaofeng-originals', manifest.installId) },
      previous: { mode: external ? 'external' : 'local', version: saved?.payloadVersion || manifest.payloadVersion,
        generation: saved?.generation || null, snapshot: operation, snapshotDirectory: path.join(t.ownerRoot, 'history', operation) },
      files: rows.map(({ source, ...row }) => row) };
    await change(t.receipt, 'receipt', null, jsonBytes(next));
    if (operations.length > 256 || rows.length > 128) fail('LAYOUT', '迁移范围过大。');
    for (const file of [...operations.map(row => row.file), path.join(t.runtimeDir, 'nr-before-sr.previous.log')]) {
      if (file.length > 259) fail('PATH_LENGTH', '外置运行路径过长，请使用较短的管理器数据目录。');
      await noLinks(file);
    }
    const planId = crypto.randomUUID();
    plans.set(planId, { planId, game, target: t, operation, operations, next, requiresFgRestore, expires: Date.now() + 5 * 60 * 1000,
      exeHash: await digest(t.exe) });
    return { planId, gameId: game.id, fromMode: external ? 'external' : 'local', toMode: request.mode, mode: request.mode,
      api, version: payloadVersion, loadingMode, requiresFgRestore, requiresConfirmation: true, requiresAntiCheat: Boolean(guards.antiCheatPresent?.(t.gameRoot)),
      layout: { runtimeDir: toExternal ? t.runtimeDir : t.dir, addonDirectory: toExternal ? t.runtimeDir : t.dir,
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
  async function restore(game, consent = {}) {
    await recover(game);
    const t = target(game, true), saved = record(t);
    if (saved?.mode !== 'external') return { restored: false, unchanged: true };
    const boundGame = { ...game, scan: { ...game.scan, chosen: { ...game.scan?.chosen, path: t.exe, bitness: 64, apiResolution: { api: saved.api } } } };
    const plan = await preview(boundGame, { mode: 'local' });
    return { ...await apply(plan.planId, consent), restored: true };
  }
  function owned(game) { return record(target(game))?.mode === 'external'; }
  return { getLayout, inspect, preview, apply, restore, recover, assertReady, owned,
    location: game => { const t = target(game); return { runtimeDir: t.runtimeDir, addonDirectory: t.runtimeDir,
      activeConfigPath: path.join(t.runtimeDir, 'ReShade.ini') }; } };
}
module.exports = { createExternalRuntime, PRODUCT, RECEIPT, PENDING, digest };
