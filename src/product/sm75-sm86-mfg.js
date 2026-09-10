'use strict';

// Optional external FG component service. Owns files/configuration only;
// never loads a graphics DLL into Electron and never changes NR/SR/driver keys.
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { noLinks, atomicJson } = require('./launch-safety');
const BACKEND = 'dlssg-native-sm75-sm86';
const VERSION = '0.2.4';
const INI = 'dlssg_sm86.ini';
const RECEIPT = '_DLSS5_Backup/xiaofeng-sm75-sm86-mfg.json';
const PROXIES = Object.freeze(['version.dll', 'winmm.dll', 'dinput8.dll', 'winhttp.dll', 'dxgi.dll']);
const CONFLICTS = Object.freeze(['RTX40MFGCore.dll', 'RTX40MFG.asi', 'RTX40MFG-Universal.asi',
  'RTX40MFG-Bridge.asi', 'RTX40MFG-Universal.json', 'dlssg_sm86.dll', 'dlssg_to_fsr3_amd_is_better.dll',
  'dlssg_to_fsr3.dll', 'OptiScaler.dll', 'OptiScaler.ini',
  'OptiScaler/dlssg_sm86/dlssg_sm86.dll', 'plugins/RTX40MFG.asi',
  'plugins/RTX40MFG-Universal.asi', 'plugins/RTX40MFG-Bridge.asi']);
const HASH = /^[a-f0-9]{64}$/;
const activeMutations = new Set();
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const samePath = (a, b) => typeof a === 'string' && typeof b === 'string' &&
  path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
function fail(code, message, details) { throw Object.assign(new Error(message), { code, details }); }
function blocked(message, details) { fail('SETTINGS_SM_MFG_BLOCKED', message, details); }
async function hashFile(file) {
  await noLinks(file);
  let stat;
  try { stat = await fsp.stat(file); } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  if (!stat.isFile()) blocked('目标不是普通文件，未修改。', { file });
  const hash = crypto.createHash('sha256');
  for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}
async function readJson(file, limit = 65536) {
  await noLinks(file);
  const stat = await fsp.stat(file);
  if (!stat.isFile() || stat.size > limit) blocked('组件清单或恢复记录过大/类型无效。');
  try { return JSON.parse(await fsp.readFile(file, 'utf8')); }
  catch { blocked('组件清单或恢复记录不是有效 JSON。'); }
}
async function pe64(file, requireDll) {
  await noLinks(file);
  const h = await fsp.open(file, 'r');
  try {
    const dos = Buffer.alloc(64), nt = Buffer.alloc(26);
    if ((await h.read(dos, 0, dos.length, 0)).bytesRead !== dos.length || dos.readUInt16LE(0) !== 0x5a4d) return false;
    const offset = dos.readUInt32LE(60);
    if (offset < 64 || offset > 1024 * 1024 || (await h.read(nt, 0, nt.length, offset)).bytesRead !== nt.length) return false;
    return nt.readUInt32LE(0) === 0x4550 && nt.readUInt16LE(4) === 0x8664 &&
      nt.readUInt16LE(24) === 0x20b && Boolean(nt.readUInt16LE(22) & 0x2000) === requireDll;
  } finally { await h.close(); }
}
function resolveRouter(hardware = {}) {
  const series = [...new Set(Array.isArray(hardware.series) ? hardware.series : [])];
  const names = Array.isArray(hardware.names) ? hardware.names : [];
  const discrete = names.filter(name => /RTX\s*\d{4}|Radeon\s+RX|Intel.*Arc\s+[AB]\d/i.test(String(name)));
  if (hardware.source === 'unavailable' || hardware.family === 'mixed' || discrete.length > 1 || series.length !== 1)
    blocked('无法确认单一渲染显卡；不会按第一张显卡猜测 MFG 路由。');
  if (series[0] === 'RTX20') return 'SM75';
  if (series[0] === 'RTX30') return 'SM86';
  blocked('此组件只用于 RTX20/30；RTX40/50 保持原路线。');
}
function configFor(router, input = {}) {
  if (!['SM75', 'SM86'].includes(router) || !input || typeof input !== 'object' || Array.isArray(input)) blocked('MFG 配置无效。');
  const allowed = ['maxGeneratedFrames', 'kernelImage', 'hardwareBilinear', 'logLevel'];
  if (Object.keys(input).some(key => !allowed.includes(key))) blocked('存在未知 MFG 配置项。');
  const value = { maxGeneratedFrames: 3, kernelImage: 'PTX', hardwareBilinear: false, logLevel: 1, ...input };
  if (!Number.isInteger(value.maxGeneratedFrames) || value.maxGeneratedFrames < 1 || value.maxGeneratedFrames > 3 ||
      !['PTX', 'Auto', 'Cubin'].includes(value.kernelImage) || typeof value.hardwareBilinear !== 'boolean' ||
      !Number.isInteger(value.logLevel) || value.logLevel < 0 || value.logLevel > 3 ||
      (router === 'SM75' && value.hardwareBilinear)) blocked('生成帧上限需为1–3；SM75不支持近似双线性采样。');
  // Cubin requires physical SM proof not provided by Win32_VideoController.
  if (value.kernelImage === 'Cubin') blocked('首版未提供物理 SM 精确证明，请使用 PTX 或 Auto。');
  return value;
}
function formatIni(router, settings = {}) {
  const c = configFor(router, settings);
  return `; Managed external MFG. Restart game after changes. Cap is NOT actual multiplier.\r\n` +
    `[Compatibility]\r\nRouter=${router}\r\nKernelImage=${c.kernelImage}\r\nHardwareBilinear=${Number(c.hardwareBilinear)}\r\n\r\n` +
    `[FrameGeneration]\r\nMaxGeneratedFrames=${c.maxGeneratedFrames}\r\n\r\n[Logging]\r\nLevel=${c.logLevel}\r\n`;
}
function createSm75Sm86Components(options = {}) {
  if (typeof options.gameDirectory !== 'function' || typeof options.gameExecutable !== 'function') blocked('缺少游戏 EXE 依赖。');
  const journal = options.journal || require('../core/file-journal');
  const guards = options.guards || ((!options.assertGameClosed || !options.antiCheatPresent) ? require('../core/install-guards').createInstallGuards() : null);
  const assertClosed = options.assertGameClosed || guards.assertGameClosed;
  const antiCheat = options.antiCheatPresent || guards.antiCheatPresent;
  const hardware = options.detectHardware || require('./gpu').detectGpuAsync;
  const scan = options.scan || (async () => ({ api: 'unknown', streamlineFg: false }));
  const packageDirectory = () => {
    if (options.sm75Sm86Resources) return options.sm75Sm86Resources;
    const packaged = options.resourcesPath && path.join(options.resourcesPath, 'fg-sm75-sm86');
    return packaged && fs.existsSync(path.join(packaged, 'manifest.json'))
      ? packaged : path.join(options.appDir || '', 'resources', 'fg-sm75-sm86');
  };
  function target(id) {
    const game = options.gameDirectory(id), exe = options.gameExecutable(id);
    if (!path.isAbsolute(game || '') || !path.isAbsolute(exe || '') || path.extname(exe).toLowerCase() !== '.exe') blocked('游戏 EXE 无效。');
    const t = { game: path.resolve(game), exe: path.resolve(exe), dir: path.dirname(path.resolve(exe)) };
    journal.safePath(t.game, path.relative(t.game, t.exe));
    t.receipt = journal.safePath(t.game, RECEIPT);
    t.file = name => journal.safePath(t.game, path.relative(t.game, path.join(t.dir, name)));
    return t;
  }
  async function receipt(t) {
    const hash = await hashFile(t.receipt);
    if (!hash) return null;
    const r = await readJson(t.receipt);
    if (r.version !== 1 || r.backend !== BACKEND || r.runtimeVersion !== VERSION || !samePath(r.exe, t.exe) ||
        !HASH.test(r.exeSha256 || '') || !PROXIES.includes(r.proxy) || !['SM75','SM86'].includes(r.router) ||
        !Array.isArray(r.files) || r.files.length !== 2) blocked('MFG 恢复记录无效或属于其他 EXE。');
    configFor(r.router, r.settings);
    const expected = [r.proxy, INI];
    for (let i = 0; i < 2; ++i) {
      const row = r.files[i];
      if (!row || row.mode !== 'created' || row.name !== expected[i] || !HASH.test(row.sha256 || '')) blocked('MFG 恢复文件记录无效。');
      t.file(row.name);
    }
    if (r.files[1].sha256 !== digest(formatIni(r.router, r.settings))) blocked('MFG 配置收据校验失败。');
    if (await hashFile(t.receipt) !== hash) blocked('读取期间恢复记录已改变。');
    Object.defineProperty(r, '_snapshotHash', { value: hash });
    return r;
  }
  async function manifest() {
    const root = path.resolve(packageDirectory());
    const file = path.join(root, 'manifest.json');
    let m, manifestHash;
    try { manifestHash = await hashFile(file); m = await readJson(file); } catch (error) {
      if (error.code === 'ENOENT') blocked('缺少外部 Native 0.2.4 组件。请导入已核对哈希的 fg-sm75-sm86 资源包。');
      throw error;
    }
    if (m.schemaVersion !== 1 || m.backend !== BACKEND || m.runtimeVersion !== VERSION ||
        m.upstream !== 'sdli1995/dlssg_for_sm86' || !/^[a-f0-9]{40}$/.test(m.sourceCommit || '') ||
        !m.proxies || typeof m.proxies !== 'object' || Array.isArray(m.proxies) || !Object.keys(m.proxies).length)
      blocked('Native MFG 组件清单不符合固定0.2.4配方。');
    for (const [name, row] of Object.entries(m.proxies)) {
      const expected = name === 'version.dll' ? name : `altnative/${name}`;
      if (!PROXIES.includes(name) || !row || row.file !== expected || !HASH.test(row.sha256 || ''))
        blocked('代理必须使用上游对应入口，不能把另一种 DLL 随意改名。');
    }
    if (!manifestHash || await hashFile(file) !== manifestHash) blocked('读取期间组件清单已改变。');
    return { root, value: m, hash: manifestHash };
  }
  async function snapshot(t, m) {
    const hashes = {};
    for (const name of [...PROXIES, INI, ...CONFLICTS]) hashes[name] = await hashFile(t.file(name));
    return { exe: await hashFile(t.exe), receipt: await hashFile(t.receipt),
      legacyReceipt: await hashFile(journal.safePath(t.game, '_DLSS5_Backup/xiaofeng-fg-components.json')),
      manifest: m.hash, packageRoot: m.root, hashes };
  }
  async function plan(id, settings, proxy) {
    const t = target(id); await noLinks(t.exe);
    const old = await receipt(t);
    const router = resolveRouter(await hardware());
    const observed = await scan(id);
    if (observed?.api !== 'dx12' || observed?.streamlineFg !== true) blocked('需要明确的 D3D12 和现有 DLSS FG/Streamline 证据；不向无FG游戏注入。');
    if (!await pe64(t.exe, false)) blocked('目标必须是 Windows x64 游戏 EXE。');
    const m = await manifest(), s = await snapshot(t, m);
    if (old && old._snapshotHash !== s.receipt) blocked('读取期间恢复记录已改变。');
    if (s.legacyReceipt)
      blocked('存在旧 FG 组件收据，请先恢复旧组件。');
    if (!s.exe || (old && old.exeSha256 !== s.exe)) blocked('所选 EXE 已变化，请先恢复旧组件并重新检查。');
    const collisions = CONFLICTS.filter(name => s.hashes[name]);
    if (collisions.length) blocked('存在其他 FG/OptiScaler 接管者，未叠加安装。请先核对并恢复其组件。', { collisions });
    if (old) {
      for (const row of old.files) if (s.hashes[row.name] && s.hashes[row.name] !== row.sha256)
        fail('SETTINGS_SM_MFG_EXTERNAL_CHANGE', `${row.name} 已被外部修改，未覆盖。`);
      if (old.router !== router) blocked('显卡路由已改变，请先恢复旧组件。');
    } else if (s.hashes[INI]) blocked('已有不受本管理器收据管理的 dlssg_sm86.ini，未覆盖/收编。');
    for (const name of PROXIES) if (name !== old?.proxy && s.hashes[name] &&
        Object.values(m.value.proxies).some(row => row.sha256 === s.hashes[name]))
      blocked('检测到另一份 Native MFG 代理，未重复加载。');
    const selected = proxy || old?.proxy || PROXIES.find(name => m.value.proxies[name] && !s.hashes[name]);
    if (!PROXIES.includes(selected) || !m.value.proxies[selected] || (old && old.proxy !== selected)) blocked('没有可用且受清单验证的代理入口。');
    if (!old && s.hashes[selected]) blocked(`${selected} 已被其他加载器占用，未覆盖。`);
    const row = m.value.proxies[selected], source = path.join(m.root, row.file);
    if (await hashFile(source) !== row.sha256 || !await pe64(source, true)) blocked('MFG DLL 哈希或 x64 PE/DLL 身份校验失败。');
    if (old && old.files[0].sha256 !== row.sha256) blocked('资源包版本/字节不同，请先恢复旧组件；不静默升级。');
    const config = configFor(router, settings === undefined ? old?.settings || {} : settings);
    const text = formatIni(router, config);
    const token = digest(JSON.stringify({ snapshot: s, router, proxy: selected, config, sourceHash: row.sha256 }));
    return { t, old, router, config, text, source, proxy: selected, hash: row.sha256, snapshot: s, token };
  }
  async function inspect(id, request = {}) {
    const t = target(id); let old;
    try { old = await receipt(t); }
    catch (error) { return { backend: BACKEND, route: 'external-sm75-sm86', ready: false, managed: true, canPrepare: false,
      canRestore: false, blockers: [error.message], missing: [], components: [], runtimeVerified: false }; }
    try {
      const p = await plan(id, request.settings, request.proxy);
      const desired = [{ name: p.proxy, sha256: p.hash }, { name: INI, sha256: digest(p.text) }];
      const ready = Boolean(old) && desired.every(row => p.snapshot.hashes[row.name] === row.sha256);
      return { backend: BACKEND, route: 'external-sm75-sm86', label: 'RTX 20 / 30 · 外部 MFG',
        ready, managed: Boolean(old), canRestore: Boolean(old), canPrepare: !ready, blockers: [],
        missing: desired.filter(row => !p.snapshot.hashes[row.name]).map(row => row.name),
        components: desired.map(row => ({ role: row.name === INI ? 'config' : 'proxy', name: row.name,
          status: p.snapshot.hashes[row.name] === row.sha256 ? 'installed' : 'missing', owned: Boolean(old) })),
        router: p.router, settings: p.config, proxy: p.proxy, previewToken: p.token, exe: t.exe,
        restartRequired: true, runtimeVerified: false, generatedFramesVerified: false,
        message: '仅准备外部组件；最多4×，实际倍率由游戏请求。重启后开启游戏DLSS FG，效果尚未验证。' };
    } catch (error) {
      return { backend: BACKEND, route: 'external-sm75-sm86', ready: false, managed: Boolean(old), canRestore: Boolean(old),
        canPrepare: false, blockers: [error.message], errorCode: error.code, missing: [], components: [], runtimeVerified: false,
        message: '未改变原生游戏链路；请收集管理器反馈与外部 MFG 日志。' };
    }
  }
  async function unchanged(t, file, expected, preserve = false) {
    if (await hashFile(file) !== expected) {
      const e = Object.assign(new Error('预览/准备后文件已变化，未覆盖；请核对并提交 BUG 反馈。'), { code: 'SETTINGS_SM_MFG_EXTERNAL_CHANGE' });
      if (preserve) e.preservePending = true;
      throw e;
    }
  }
  async function prepare(id, request = {}) {
    const p = await plan(id, request.settings, request.proxy);
    if (request.previewToken !== undefined && request.previewToken !== p.token) fail('SETTINGS_SM_MFG_STALE_PREVIEW', '组件预览已过期，请重新检查。');
    if (await antiCheat(p.t.game) && request.allowAntiCheat !== true)
      fail('ERR_ANTI_CHEAT_CONFIRM', '请先确认反作弊风险。', { operation: 'prepare-sm75-sm86-mfg' });
    await assertClosed(p.t.game, p.t.exe);
    const again = await plan(id, request.settings, request.proxy);
    if (again.token !== p.token) fail('SETTINGS_SM_MFG_STALE_PREVIEW', '检查期间硬件、EXE、资源或目标文件已改变。');
    const desired = [{ name: p.proxy, sha256: p.hash, source: p.source }, { name: INI, sha256: digest(p.text), text: p.text }];
    if (p.old && desired.every(row => p.snapshot.hashes[row.name] === row.sha256))
      return { prepared: false, unchanged: true, backend: BACKEND, runtimeVerified: false };
    return journal.transaction(p.t.game, async () => {
      // Recheck the entire selected-EXE file set inside the shared WAL before any write.
      for (const [name, hash] of Object.entries(p.snapshot.hashes)) await unchanged(p.t, p.t.file(name), hash);
      await unchanged(p.t, p.t.exe, p.snapshot.exe);
      await unchanged(p.t, p.t.receipt, p.snapshot.receipt);
      await unchanged(p.t, journal.safePath(p.t.game, '_DLSS5_Backup/xiaofeng-fg-components.json'), p.snapshot.legacyReceipt);
      for (const row of desired) {
        const file = p.t.file(row.name), expected = p.snapshot.hashes[row.name];
        if (expected === row.sha256) continue;
        await journal.capture(p.t.game, file);
        await unchanged(p.t, file, expected, true);
        if (row.source) {
          try { await fsp.copyFile(row.source, file, fs.constants.COPYFILE_EXCL); }
          catch (error) { if (await hashFile(file)) error.preservePending = true; throw error; }
        } else {
          try { await fsp.writeFile(file, row.text, { encoding: 'utf8', flag: expected === null ? 'wx' : 'w' }); }
          catch (error) { if (error.code === 'EEXIST') error.preservePending = true; throw error; }
        }
        await unchanged(p.t, file, row.sha256, true);
      }
      await journal.capture(p.t.game, p.t.receipt);
      await unchanged(p.t, p.t.receipt, p.snapshot.receipt, true);
      const saved = { version: 1, backend: BACKEND, runtimeVersion: VERSION, exe: p.t.exe, exeSha256: p.snapshot.exe,
        router: p.router, proxy: p.proxy, settings: p.config,
        files: desired.map(row => ({ name: row.name, mode: 'created', sha256: row.sha256 })) };
      await atomicJson(p.t.receipt, saved);
      return { prepared: true, backend: BACKEND, router: p.router, proxy: p.proxy, restartRequired: true,
        runtimeVerified: false, generatedFramesVerified: false };
    });
  }
  async function restore(id) {
    const t = target(id), old = await receipt(t);
    if (!old) return { restored: false, unchanged: true };
    // Recovery deliberately does not read GPU state, the external payload, or scan.
    const baseline = old._snapshotHash, hashes = {};
    await assertClosed(t.game, t.exe);
    await unchanged(t, t.receipt, baseline);
    for (const row of old.files) {
      hashes[row.name] = await hashFile(t.file(row.name));
      if (hashes[row.name] && hashes[row.name] !== row.sha256)
        fail('SETTINGS_SM_MFG_EXTERNAL_CHANGE', `${row.name} 已被外部修改，未删除。`);
    }
    return journal.transaction(t.game, async () => {
      await unchanged(t, t.receipt, baseline);
      for (const row of old.files) await unchanged(t, t.file(row.name), hashes[row.name]);
      for (const row of [...old.files].reverse()) {
        const file = t.file(row.name);
        if (!hashes[row.name]) continue;
        await journal.capture(t.game, file);
        await unchanged(t, file, hashes[row.name], true);
        await fsp.unlink(file);
      }
      await journal.capture(t.game, t.receipt);
      await unchanged(t, t.receipt, baseline, true);
      await fsp.unlink(t.receipt);
      return { restored: true, backend: BACKEND, runtimeVerified: false };
    });
  }
  async function hasReceipt(id) { return Boolean(await hashFile(target(id).receipt)); }
  async function mutate(id, work) {
    const key = target(id).game.toLowerCase();
    if (activeMutations.has(key)) fail('errJobBusy', '此游戏的 MFG 组件操作尚未完成。');
    activeMutations.add(key);
    try { return await work(); } finally { activeMutations.delete(key); }
  }
  return Object.freeze({ inspect, prepare: (id, request) => mutate(id, () => prepare(id, request)),
    restore: id => mutate(id, () => restore(id)), hasReceipt, receiptFile: id => target(id).receipt });
}

// Host seam: decorate the EXISTING component service, not its settings writer.
// A receipt wins over current hardware so recovery survives GPU changes.
function withSm75Sm86Components(legacy, native, detectHardware) {
  async function useNative(id) {
    if (await native.hasReceipt(id)) return true;
    const h = await detectHardware();
    return Array.isArray(h?.series) && h.series.some(series => ['RTX20', 'RTX30'].includes(series));
  }
  return Object.freeze({ ...legacy, hasNativeReceipt: id => native.hasReceipt(id),
    inspect: async (id, request) => (await useNative(id) ? native : legacy).inspect(id, request),
    prepare: async (id, request) => (await useNative(id) ? native : legacy).prepare(id, request),
    restore: async id => await native.hasReceipt(id) ? native.restore(id) : legacy.restore(id)
  });
}
module.exports = { BACKEND, VERSION, INI, RECEIPT, PROXIES, resolveRouter, configFor, formatIni,
  createSm75Sm86Components, withSm75Sm86Components };
