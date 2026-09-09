'use strict';
const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');
const crypto = require('crypto');
const journalDefault = require('../core/file-journal');
const peDefault = require('../core/pe');
const { createInstallGuards } = require('../core/install-guards');
const { detectGpuAsync } = require('./gpu');
const { noLinks, atomicJson } = require('./launch-safety');
const { appError } = require('./errors');
const { inspectAddonLayout, addonValues } = require('./reshade-layout');
const { createFgComponents: createLegacyFgComponents } = require('./fg-legacy-components');
const { ID, BACKEND, ADDON, SHA256, sha256, readMfgUnlockResources } = require('./fg-mfgunlock-resources');
const { createMigrationStore, copyNewFile, hashFile, same } = require('./fg-migration-components');

const COMPETING = /(?:rtx[-_ ]?40[-_ ]?mfg|mfg[-_ ]?unlock|mfgadaunlock|(?:^|[\\/._ -])mfg(?:[._ -]|$))/i;
function fail(code, message, details) { throw Object.assign(new Error(message), { code, details }); }

function createMfgUnlockComponents(options = {}) {
  if (typeof options.gameDirectory !== 'function' || typeof options.gameExecutable !== 'function') fail('SETTINGS_FG_INIT', 'FG 组件服务缺少游戏路径依赖。');
  const journal = options.journal || journalDefault, pe = options.pe || peDefault;
  const guards = options.guards || createInstallGuards();
  const assertGameClosed = options.assertGameClosed || guards.assertGameClosed;
  const antiCheatPresent = options.antiCheatPresent || guards.antiCheatPresent;
  const scan = options.scan || (async () => ({})), detectHardware = options.detectHardware || detectGpuAsync;
  const legacy = createLegacyFgComponents(options);
  const migrations = createMigrationStore({ journal, assertGameClosed });
  const undos = new Map();
  const resources = fs.existsSync(path.join(options.resourcesPath || '', 'fg-mfgunlock', 'manifest.json'))
    ? path.join(options.resourcesPath, 'fg-mfgunlock') : path.join(options.appDir || '', 'resources', 'fg-mfgunlock');
  function target(id) {
    const game = options.gameDirectory(id), exe = options.gameExecutable(id);
    if (typeof game !== 'string' || typeof exe !== 'string' || !path.isAbsolute(game) || !path.isAbsolute(exe) || path.extname(exe).toLowerCase() !== '.exe') fail('SETTINGS_FG_TARGET', '游戏 EXE 无效。');
    const rel = path.relative(path.resolve(game), path.resolve(exe));
    if (!rel || rel === '..' || rel.startsWith(`..${path.sep}`) || path.isAbsolute(rel)) fail('SETTINGS_FG_TARGET', '游戏 EXE 不在所选目录内。');
    return { game: path.resolve(game), exe: path.resolve(exe), dir: path.dirname(path.resolve(exe)) };
  }
  const receiptFile = t => journal.safePath(t.game, path.join('_DLSS5_Backup', 'xiaofeng-fg-components.json'));
  async function receipt(t) {
    const file = receiptFile(t); await noLinks(file); if (!fs.existsSync(file)) return null;
    let value;
    try { if (fs.statSync(file).size > 512 * 1024) throw new Error(); value = JSON.parse(fs.readFileSync(file, 'utf8')); }
    catch { fail('SETTINGS_FG_RECEIPT', 'FG 组件恢复记录损坏。'); }
    if (value?.version === 1) return value;
    const row = value?.files?.[0];
    if (value?.version !== 2 || value.backend !== BACKEND || value.id !== ID || !same(value.exe, t.exe) ||
        !Array.isArray(value.files) || value.files.length !== 1 || row.role !== 'addon' ||
        row.rel !== path.relative(t.game, path.join(t.dir, ADDON)) || !['created', 'adopted'].includes(row.mode) || row.after !== SHA256) fail('SETTINGS_FG_RECEIPT', 'MFG Unlock 恢复记录身份或文件摘要无效。');
    await noLinks(journal.safePath(t.game, row.rel)); return value;
  }
  async function checkRuntime(t, observed) {
    if (options.inspectFgRuntime) return options.inspectFgRuntime({ applicationDirectory: t.dir, gameDirectory: t.game, exe: t.exe, observed });
    const suppliedValue = observed?.fgRuntime?.file;
    const supplied = typeof suppliedValue === 'string' && path.isAbsolute(suppliedValue) ? suppliedValue : null;
    const candidates = [supplied, ...(observed?.dlssgFiles || []).map(row => typeof row === 'string' ? row : row.path), path.join(t.dir, 'nvngx_dlssg.dll')]
      .filter(value => typeof value === 'string' && path.isAbsolute(value));
    const found = [...new Set(candidates)].filter(file => fs.existsSync(file));
    const local = path.join(t.dir, 'nvngx_dlssg.dll');
    const file = supplied && fs.existsSync(supplied) ? supplied : fs.existsSync(local) ? local : found.length === 1 ? found[0] : null;
    if (!file) return { status: 'unknown', ready: false, message: found.length > 1 ? '发现多个 DLSS-G 运行库，尚未确定游戏采用的版本。' : '未找到可验证的 DLSS-G 运行库；需要游戏已有 310.x 或更新版本。' };
    try {
      await noLinks(file);
      const version = pe.getFileVersion?.(file), match = typeof version === 'string' && version.match(/^(\d+)\.(\d+)(?:\.\d+){0,2}$/);
      if (pe.getBitness(file) !== 64 || !match) return { status: 'unknown', ready: false, file, version: version || null, message: 'DLSS-G 的 x64 架构或版本无法验证。' };
      if (Number(match[1]) < 310) return { status: 'outdated', ready: false, file, version, message: `DLSS-G ${version} 不包含此方案要求的现代 MFG 运行库；请核对游戏配套文件。` };
      return { status: 'available', ready: true, file, version, runtimeVerified: false, message: '已验证磁盘上的 x64 DLSS-G 版本；实际加载版本和生成帧仍需游戏内验证。' };
    } catch { return { status: 'unknown', ready: false, file, message: '无法安全读取 DLSS-G 运行库。' }; }
  }
  async function inventory(t) {
    const files = [], problems = [], pending = [{ dir: t.dir, depth: 0 }]; let count = 0;
    while (pending.length) {
      const next = pending.pop(); await noLinks(next.dir);
      for (const entry of fs.readdirSync(next.dir, { withFileTypes: true })) {
        if (++count > 20000) return { files, problems: [...problems, '组件目录过大，无法排除第二套 MFG。'] };
        const file = path.join(next.dir, entry.name), rel = path.relative(t.game, file);
        if (entry.isSymbolicLink()) { if (COMPETING.test(rel) || /^(scripts|plugins|addons)$/i.test(entry.name)) problems.push(`组件路径是链接：${rel}`); continue; }
        if (entry.isFile() && COMPETING.test(rel) && /\.(?:dll|asi|addon64|addon32|json|lua)$/i.test(entry.name)) { await noLinks(file); files.push({ rel, file, hash: hashFile(file) }); }
        if (entry.isDirectory() && next.depth < 6 && (next.depth > 0 || /^(scripts|plugins|addons|asi|reframework)$/i.test(entry.name) || COMPETING.test(entry.name))) pending.push({ dir: file, depth: next.depth + 1 });
      }
    }
    return { files, problems };
  }
  async function inspect(id) {
    const t = target(id); await noLinks(t.exe);
    const observed = await scan(id), hardware = await detectHardware();
    const series = [...new Set(Array.isArray(hardware?.series) ? hardware.series : [])];
    const route = series.length === 1 && series[0] === 'RTX50' ? 'native' : series.length === 1 && series[0] === 'RTX40' ? 'compatibility' : 'unsupported';
    const conflicts = [], blockers = [], missing = [], components = [];
    let owned = null, pending = null, legacyPlan = null, manifest = null, fgRuntime = null;
    try { owned = await receipt(t); } catch (error) { conflicts.push(error.message); }
    try { pending = await migrations.read(t); } catch (error) { conflicts.push(error.message); }
    const legacyNeedsMigration = owned?.version === 1;
    if (legacyNeedsMigration) {
      try { legacyPlan = await legacy.inspectRestore(id, { retainSharedLoaders: true }); }
      catch (error) { conflicts.push(error.message); }
    }
    const found = await inventory(t); conflicts.push(...found.problems);
    const managedOld = new Map((legacyPlan?.receipt?.files || []).map(row => [row.rel.toLowerCase(), row]));
    for (const row of found.files) {
      if (same(row.file, path.join(t.dir, ADDON))) {
        if (row.hash !== SHA256) conflicts.push(`${ADDON} 已存在且不是已验证的 0.6.1，未覆盖。`);
        continue;
      }
      const old = managedOld.get(row.rel.toLowerCase());
      if (!old || old.mode === 'adopted' || old.after !== row.hash) conflicts.push(`检测到未受本工具管理或已修改的 MFG 组件：${row.rel}。`);
      components.push({ role: 'legacy', name: row.rel, status: old && old.mode !== 'adopted' ? 'legacy-managed' : 'external', owned: Boolean(old && old.mode !== 'adopted') });
    }
    const actual = hashFile(path.join(t.dir, ADDON));
    components.push({ role: 'addon', name: ADDON, status: actual === null ? 'missing' : actual === SHA256 ? 'ready' : 'external', owned: owned?.version === 2 && owned.files[0].mode === 'created' });
    if (route === 'compatibility') {
      try { manifest = readMfgUnlockResources(resources); } catch (error) { blockers.push(error.message); }
      if (pe.getBitness(t.exe) !== 64) blockers.push('所选程序不是 Windows x64 游戏 EXE。');
      if (observed?.api !== 'dx12') blockers.push(observed?.api === 'vulkan' ? 'MFG Unlock 的 Vulkan 路径仍属实验性，此准备入口仅支持 DirectX 12。' : '请先确认游戏实际使用 DirectX 12。');
      if (observed?.streamlineFg !== true) blockers.push('未确认游戏已有 Streamline DLSS Frame Generation；此组件不会为没有 FG 的游戏添加 FG。');
      if (observed?.reshadeAddon !== true) blockers.push('请先准备并确认支持完整 Add-on 的 ReShade。');
      if (observed?.reshadeAddonDirectory && !same(observed.reshadeAddonDirectory, t.dir)) blockers.push('当前 ReShade 使用自定义 Add-on 目录，此入口不会猜测或写入其他位置。');
      const iniFile = path.join(t.dir, 'ReShade.ini'); await noLinks(iniFile);
      const layout = inspectAddonLayout(t.dir, options.environment || process.env);
      // NR owns its own direct-load compatibility checks. MFG needs the actual
      // local INI/add-on directory and separately checks competing MFG entries.
      if (!layout.ok && layout.code !== 'ERR_ADDON_DIRECT_LOAD') blockers.push('ReShade 的活动配置或 Add-on 目录不是已确认的游戏 EXE 目录。');
      if (layout.ok || layout.code === 'ERR_ADDON_DIRECT_LOAD') {
        const direct = fs.existsSync(iniFile) ? addonValues(fs.readFileSync(iniFile, 'utf8')).get('LoadFromDllMain') || [] : [];
        for (const entry of direct.filter(name => COMPETING.test(name))) {
          const file = path.resolve(t.dir, entry);
          if (fs.existsSync(file) && !same(file, path.join(t.dir, ADDON)) && !managedOld.has(path.relative(t.game, file).toLowerCase())) conflicts.push(`ReShade 直接加载了另一套 MFG：${entry}。`);
        }
      }
      try { fgRuntime = await checkRuntime(t, observed); } catch { fgRuntime = { ready: false, status: 'unknown', message: '无法验证 DLSS-G 运行库。' }; }
      if (fgRuntime?.ready !== true || fgRuntime.status !== 'available') blockers.push(fgRuntime?.message || 'DLSS-G 运行库尚未验证。');
      if (actual === null) missing.push(ADDON);
    } else if (route === 'unsupported') blockers.push('仅 RTX 40 使用 MFG Unlock；RTX 50 使用原生帧生成。');
    if (pending?.state === 'removing') blockers.push('FG 文件事务尚未完成，需要先恢复共享文件日志。');
    const migrationReady = Boolean(legacyNeedsMigration && legacyPlan && conflicts.length === 0 && blockers.length === 0);
    const needsCleanup = route !== 'compatibility' && Boolean(owned || actual);
    if (route === 'native' && actual && !owned) conflicts.push('检测到外部 MFG Unlock；原生 FG 路线不会自动移除它。');
    const ready = !legacyNeedsMigration && !needsCleanup && !blockers.length && !conflicts.length && (route === 'native' || route === 'compatibility' && actual === SHA256);
    const allBlockers = [...blockers, ...conflicts, ...(legacyNeedsMigration ? ['旧 RTX40 MFG 需要先恢复旧设置并迁移受管组件。'] : [])];
    return { backend: BACKEND, id: ID, route, ready, managed: Boolean(owned), receipt: Boolean(owned), needsCleanup,
      legacyNeedsMigration, migrationReady, migrationPending: Boolean(pending), migrationToken: pending?.token || null, migrationState: pending?.state || null,
      conflicts: [...new Set(conflicts)], blockers: [...new Set(allBlockers)], missing, canPrepare: route === 'compatibility' && !legacyNeedsMigration && !blockers.length && !conflicts.length && !ready,
      components, api: observed?.api || 'unknown', exe: t.exe, fgRuntime, runtimeVerified: false, panel: 'MFG Unlock', configurationReadAt: 'process-attach',
      retained: legacyPlan?.retained || [], resourceId: manifest?.id || null };
  }
  async function guard(t, operation, allowAntiCheat) {
    await assertGameClosed(t.game, t.exe);
    if (antiCheatPresent(t.game) && allowAntiCheat !== true) throw appError('ERR_ANTI_CHEAT_CONFIRM', { operation });
  }
  async function prepare(id, { allowAntiCheat = false, migrationToken = null } = {}) {
    const t = target(id); await guard(t, 'prepare-fg-components', allowAntiCheat);
    const status = await inspect(id);
    if (status.legacyNeedsMigration) fail('SETTINGS_FG_MIGRATION_REQUIRED', '请先恢复旧 FG 设置并迁移受管组件。', status);
    if (status.route !== 'compatibility') fail('SETTINGS_FG_UNSUPPORTED', '当前无需 RTX40 MFG Unlock。');
    if (status.blockers.length) fail('SETTINGS_FG_BLOCKED', status.blockers.join('\n'), status);
    const pending = await migrations.read(t);
    if (pending && (pending.token !== migrationToken || pending.state !== 'removed')) fail('SETTINGS_FG_MIGRATION_PENDING', '请完成或恢复当前迁移，不能另起一轮组件安装。');
    if (!pending && migrationToken) fail('SETTINGS_FG_MIGRATION_TOKEN', 'FG 迁移凭据已失效。');
    const prior = await receipt(t);
    if (status.ready && prior?.version === 2) return { ...status, prepared: false, unchanged: true, changed: false, created: [], adopted: [], undoToken: null };
    const manifest = readMfgUnlockResources(resources), addon = journal.safePath(t.game, path.relative(t.game, path.join(t.dir, ADDON)));
    const before = hashFile(addon), receiptBeforeText = fs.existsSync(receiptFile(t)) ? fs.readFileSync(receiptFile(t), 'utf8') : null;
    const undoToken = crypto.randomUUID(); let delta;
    const result = await journal.transaction(t.game, async () => {
      await noLinks(addon); await noLinks(receiptFile(t));
      if (hashFile(addon) !== before || hashFile(receiptFile(t)) !== (receiptBeforeText === null ? null : sha256(Buffer.from(receiptBeforeText)))) fail('SETTINGS_FG_EXTERNAL_CHANGE', 'FG 文件在准备前改变。');
      const fresh = await inspect(id);
      if (fresh.blockers.length) fail('SETTINGS_FG_BLOCKED', fresh.blockers.join('\n'));
      await assertGameClosed(t.game, t.exe);
      if (before === null) await copyNewFile({ journal, game: t.game, source: manifest.files.addon.source, dest: addon, copyFile: options.copyFile });
      if (hashFile(addon) !== SHA256) fail('SETTINGS_FG_WRITE', 'MFG Unlock 写入后摘要不匹配。');
      const row = { role: 'addon', rel: path.relative(t.game, addon), mode: prior?.files?.[0]?.mode === 'created' || before === null ? 'created' : 'adopted', after: SHA256 };
      const next = { version: 2, backend: BACKEND, id: ID, releaseVersion: '0.6.1', exe: t.exe, preparedAt: new Date().toISOString(), files: [row] };
      await journal.capture(t.game, receiptFile(t)); await (options.writeReceipt || atomicJson)(receiptFile(t), next);
      const receiptAfter = hashFile(receiptFile(t));
      if (JSON.stringify(await receipt(t)) !== JSON.stringify(next)) fail('SETTINGS_FG_WRITE', 'MFG Unlock 收据写入校验失败。');
      delta = { undoToken, addonRel: row.rel, addonBefore: before, addonAfter: SHA256, receiptBeforeText, receiptAfter };
      if (pending) await migrations.prepared(t, migrationToken, delta);
      return { backend: BACKEND, id: ID, route: 'compatibility', prepared: true, changed: true, created: before === null ? [ADDON] : [],
        adopted: before === SHA256 ? [ADDON] : [], components: [row], undoToken, migrationToken, runtimeVerified: false, restartRequired: true };
    });
    undos.set(undoToken, { t, delta, migrationToken }); return result;
  }
  async function rollbackPrepare(id, token) {
    if (!token) return { restored: false, unchanged: true };
    const undo = undos.get(token), t = target(id);
    if (!undo || !same(undo.t.exe, t.exe)) fail('SETTINGS_FG_UNDO_TOKEN', '本轮 FG 准备撤销凭据无效。');
    await assertGameClosed(t.game, t.exe);
    const { delta } = undo, addon = journal.safePath(t.game, delta.addonRel);
    const check = async () => { await noLinks(addon); await noLinks(receiptFile(t));
      if (hashFile(addon) !== delta.addonAfter || hashFile(receiptFile(t)) !== delta.receiptAfter) fail('SETTINGS_FG_EXTERNAL_CHANGE', 'FG 准备后文件已被外部修改，未撤销。'); };
    await check();
    const result = await journal.transaction(t.game, async () => {
      await check();
      await assertGameClosed(t.game, t.exe);
      if (delta.addonBefore === null) { await journal.capture(t.game, addon); await fsp.unlink(addon); }
      await journal.capture(t.game, receiptFile(t));
      if (delta.receiptBeforeText === null) await fsp.unlink(receiptFile(t)); else await fsp.writeFile(receiptFile(t), delta.receiptBeforeText, 'utf8');
      if (undo.migrationToken) await migrations.unprepare(t, undo.migrationToken);
      return { restored: true, removed: delta.addonBefore === null ? [ADDON] : [], retained: delta.addonBefore === SHA256 ? [ADDON] : [], runtimeVerified: false };
    });
    undos.delete(token); return result;
  }
  async function migrateLegacy(id, { allowAntiCheat = false } = {}) {
    const t = target(id); await guard(t, 'migrate-fg-components', allowAntiCheat);
    const pending = await migrations.read(t);
    if (pending) fail('SETTINGS_FG_MIGRATION_PENDING', '已有待完成的 FG 迁移。', { migrationToken: pending.token });
    const status = await inspect(id);
    if (!status.legacyNeedsMigration) return { migrated: false, unchanged: true, migrationToken: null };
    if (!status.migrationReady) fail('SETTINGS_FG_MIGRATION_BLOCKED', status.blockers.join('\n'), status);
    let record;
    const result = await legacy.restore(id, { retainSharedLoaders: true,
      beforeMutation: async plan => { record = await migrations.begin(t, plan); await assertGameClosed(t.game, t.exe); },
      afterMutation: async () => { record = await migrations.removed(t); } });
    return { ...result, migrated: true, backend: BACKEND, id: ID, migrationToken: record.token, migrationPending: true };
  }
  async function restore(id) {
    const t = target(id); await noLinks(t.exe);
    if (await migrations.read(t)) fail('SETTINGS_FG_MIGRATION_PENDING', '迁移快照仍在，请使用迁移恢复入口。');
    const prior = await receipt(t); if (!prior) return { restored: false, unchanged: true };
    if (prior.version === 1) return legacy.restore(id, { retainSharedLoaders: true });
    await assertGameClosed(t.game, t.exe);
    const row = prior.files[0], addon = journal.safePath(t.game, row.rel);
    const check = async () => { await noLinks(addon); const now = await receipt(t);
      if (JSON.stringify(now) !== JSON.stringify(prior)) fail('SETTINGS_FG_EXTERNAL_CHANGE', 'FG 收据已改变。');
      const actual = hashFile(addon); if (row.mode === 'created' && actual !== null && actual !== row.after) fail('SETTINGS_FG_EXTERNAL_CHANGE', 'MFG Unlock 已被外部修改，未删除。'); };
    await check();
    return journal.transaction(t.game, async () => {
      await check();
      await assertGameClosed(t.game, t.exe);
      if (row.mode === 'created' && fs.existsSync(addon)) { await journal.capture(t.game, addon); await fsp.unlink(addon); }
      await journal.capture(t.game, receiptFile(t)); await fsp.unlink(receiptFile(t));
      return { restored: true, retained: row.mode === 'adopted' ? [row.rel] : [], runtimeVerified: false };
    });
  }
  return Object.freeze({ inspect, prepare, restore, migrateLegacy, rollbackPrepare,
    inspectMigration: async id => { const pending = await migrations.read(target(id)); return { migrationPending: Boolean(pending), migrationToken: pending?.token || null }; },
    commitPrepare: (id, token) => { const t = target(id), undo = undos.get(token); if (undo && !same(undo.t.exe, t.exe)) fail('SETTINGS_FG_UNDO_TOKEN', 'FG 撤销凭据属于另一游戏。'); undos.delete(token); return { committed: true }; },
    rollbackMigration: (id, token) => migrations.finish(id, target(id), token, true),
    commitMigration: (id, token) => migrations.finish(id, target(id), token, false),
    receiptFile: id => receiptFile(target(id)) });
}
module.exports = { createMfgUnlockComponents };
