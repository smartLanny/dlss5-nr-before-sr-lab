'use strict';
const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');
const crypto = require('crypto');
const journalDefault = require('../core/file-journal');
const policy = require('./launch-settings-policy');
const { createNvapiProfileAdapter } = require('./nvapi-profile');
const { fail, inside, noLinks, digestFile, atomicJson } = require('./launch-safety');
const { detectGpuAsync: detectGpu } = require('./gpu');
const { createInstallGuards } = require('../core/install-guards');
const pe = require('../core/pe');

const DRIVER_IDS = [...policy.IDS.sr, ...policy.IDS.fg];
const clone = value => structuredClone(value);
const samePath = (a, b) => typeof a === 'string' && typeof b === 'string' && path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
const subset = (snapshot, ids) => Object.fromEntries(ids.map(id => [id, snapshot.settings[id]]));
const stateEmpty = () => ({ version: 1, games: {} });

function createLaunchSettingsService(options = {}) {
  const { userData, appDir, resourcesPath, legacySrModel = null } = options;
  if (!userData || !appDir || typeof options.gameDirectory !== 'function' || typeof options.gameExecutable !== 'function')
    fail('SETTINGS_INIT', '启动设置服务缺少游戏路径依赖。');
  const journal = options.journal || journalDefault;
  const detectHardware = options.detectHardware || detectGpu;
  const environment = options.environment || null;
  const assertGameClosed = options.assertGameClosed || createInstallGuards().assertGameClosed;
  const peBitness = options.peBitness || pe.getBitness;
  const scriptPath = resourcesPath && fs.existsSync(path.join(resourcesPath, 'nvapi-profile.ps1'))
    ? path.join(resourcesPath, 'nvapi-profile.ps1') : path.join(appDir, 'src', 'product', 'nvapi-profile.ps1');
  const driver = options.driver || createNvapiProfileAdapter({ scriptPath });
  const requestFile = path.join(userData, 'launch-settings-requests.json');
  const plans = new Map();
  let serial = Promise.resolve();
  const serialize = work => { const next = serial.then(work, work); serial = next.catch(() => {}); return next; };

  function target(id) {
    const dir = options.gameDirectory(id), exe = options.gameExecutable(id);
    if (typeof dir !== 'string' || typeof exe !== 'string' || !path.isAbsolute(dir) || !path.isAbsolute(exe) ||
        !inside(dir, exe) || path.extname(exe).toLowerCase() !== '.exe') fail('ERR_UNKNOWN_GAME', '游戏 EXE 绑定无效。');
    return { id, dir: path.resolve(dir), exe: path.resolve(exe), key: path.resolve(dir).toLowerCase() };
  }
  function receiptFile(t) { return path.join(t.dir, '_DLSS5_Backup', 'xiaofeng-launch-settings.json'); }
  function driverPendingFile(t) { return path.join(t.dir, '_DLSS5_Backup', 'launch-driver-pending.json'); }
  function readJson(file, fallback) { try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { return clone(fallback); } }
  function requests() {
    if (!fs.existsSync(requestFile)) return stateEmpty();
    let value; try { value = JSON.parse(fs.readFileSync(requestFile, 'utf8')); } catch { fail('SETTINGS_REQUESTS_INVALID', '保存的启动设置无法解析。'); }
    if (!value || value.version !== 1 || !value.games || typeof value.games !== 'object' || Array.isArray(value.games)) fail('SETTINGS_REQUESTS_INVALID', '保存的启动设置结构无效。');
    for (const [key, game] of Object.entries(value.games)) {
      if (!path.isAbsolute(key) || !game || typeof game !== 'object' || Array.isArray(game) || Object.keys(game).some(name => !['sr', 'fg', '_srManaged'].includes(name))) fail('SETTINGS_REQUESTS_INVALID', '保存的游戏设置范围无效。');
      for (const domain of ['sr', 'fg']) if (game[domain] && (typeof game[domain].exe !== 'string' || !path.isAbsolute(game[domain].exe) || !game[domain].request)) fail('SETTINGS_REQUESTS_INVALID', '保存的 EXE 请求无效。'); else if (game[domain]) policy.validateRequest(domain, game[domain].request);
      if (game._srManaged && (typeof game._srManaged.exe !== 'string' || !path.isAbsolute(game._srManaged.exe))) fail('SETTINGS_REQUESTS_INVALID', 'SR 接管标记无效。');
    }
    return value;
  }
  function validProfile(value, t) { return value === null || value && typeof value === 'object' && typeof value.name === 'string' && typeof value.appName === 'string' && samePath(value.appName, t.exe) && value.exclusive === true && typeof value.owned === 'boolean'; }
  function validSetting(value) { return value && typeof value === 'object' && ['explicit', 'inherited', 'absent'].includes(value.kind) &&
    (value.kind === 'absent' ? value.value === null && value.location === null && value.predefined === null : Number.isInteger(value.value) && value.value >= 0 && value.value <= 0xffffffff && Number.isInteger(value.location) && value.location >= 0 && value.location <= 3 && typeof value.predefined === 'boolean' && (value.kind !== 'explicit' || value.location === 0 && value.predefined === false)); }
  function validSnapshot(value, t) { return value && typeof value === 'object' && validProfile(value.profile, t) && value.settings &&
    Object.keys(value.settings).length === DRIVER_IDS.length && DRIVER_IDS.every(id => validSetting(value.settings[id])); }
  function receipt(t, { allowEmptyRebind = false } = {}) {
    const file = receiptFile(t); journal.safePath(t.dir, path.relative(t.dir, file));
    let value;
    if (!fs.existsSync(file)) return { version: 1, exe: t.exe, applied: {}, driverOriginalProfile: null, lastTransaction: null };
    try { value = JSON.parse(fs.readFileSync(file, 'utf8')); } catch { fail('SETTINGS_RECEIPT_INVALID', '启动设置恢复记录无法解析。'); }
    if (value?.version === 1 && typeof value.exe === 'string' && path.isAbsolute(value.exe) && !samePath(value.exe, t.exe) &&
        value.applied && typeof value.applied === 'object' && !Array.isArray(value.applied) && Object.keys(value.applied).length === 0 &&
        (allowEmptyRebind || !fs.existsSync(journal.pendingPath(t.dir))) && !fs.existsSync(driverPendingFile(t))) {
      return { version: 1, exe: t.exe, applied: {}, driverOriginalProfile: null, lastTransaction: null };
    }
    if (value.version !== 1 || !samePath(value.exe, t.exe) || !value.applied || typeof value.applied !== 'object' || !validProfile(value.driverOriginalProfile ?? null, t) || Object.keys(value.applied).some(domain => !['sr', 'fg'].includes(domain)) ||
        value.lastTransaction != null && (!value.lastTransaction || !/^[a-f0-9-]{36}$/.test(value.lastTransaction.id || '') || !['sr', 'fg'].includes(value.lastTransaction.domain) || typeof value.lastTransaction.restoring !== 'boolean'))
      fail('SETTINGS_RECEIPT_INVALID', '启动设置恢复记录与当前 EXE 不一致。');
    for (const [domain, row] of Object.entries(value.applied)) {
      const allowed = domain === 'sr' ? ['native', 'optiscaler'] : ['nvidia', 'rtx40'];
      if (!row || !allowed.includes(row.backend) || !samePath(row.exe, t.exe) || !row.request || typeof row.request !== 'object') fail('SETTINGS_RECEIPT_INVALID', '启动设置域记录无效。');
      policy.validateRequest(domain, row.request);
      if (['native', 'nvidia'].includes(row.backend)) {
        if (!Array.isArray(row.ids) || !row.ids.length || row.ids.some(id => !policy.IDS[domain].includes(id)) || new Set(row.ids).size !== row.ids.length || !validSnapshot(row.baseline, t) || !validProfile(row.profile, t) ||
            !row.lastValues || Object.keys(row.lastValues).length !== row.ids.length || row.ids.some(id => !validSetting(row.lastValues[id]))) fail('SETTINGS_RECEIPT_INVALID', '驱动设置恢复范围无效。');
      } else {
        if (!policy.validConfigName(row.name, row.backend) || typeof row.baselineText !== 'string' || Buffer.byteLength(row.baselineText) > 1024 * 1024 || !row.lastValues || typeof row.lastValues !== 'object') fail('SETTINGS_RECEIPT_INVALID', '配置文件恢复范围无效。');
        const allowedKeys = Object.keys(policy.values(row.baselineText, row.backend));
        if (Object.keys(row.lastValues).some(key => !allowedKeys.includes(key))) fail('SETTINGS_RECEIPT_INVALID', '配置文件恢复键无效。');
      }
    }
    return value;
  }
  async function saveRequests(value) { await noLinks(requestFile); await atomicJson(requestFile, value); }
  async function writeReceipt(t, value) { await noLinks(receiptFile(t)); await atomicJson(receiptFile(t), value); }
  async function pending(id) {
    const t = target(id), rows = [];
    if (fs.existsSync(journal.pendingPath(t.dir))) rows.push({ kind: 'file-journal', file: journal.pendingPath(t.dir) });
    if (fs.existsSync(driverPendingFile(t))) rows.push({ kind: 'driver-receipt', file: driverPendingFile(t) });
    return rows;
  }
  async function assertReady(id) { const rows = await pending(id); if (rows.length) fail('SETTINGS_RECOVERY_FIRST', '上次文件或驱动设置尚未恢复，请先完成恢复。', { pending: rows }); return true; }
  async function hasOwnedState(id) {
    const dir = options.gameDirectory(id);
    if (typeof dir !== 'string' || !path.isAbsolute(dir)) fail('ERR_UNKNOWN_GAME', '游戏目录无效。');
    const meta = journal.safePath(dir, '_DLSS5_Backup/xiaofeng-launch-settings.json');
    if (fs.existsSync(journal.pendingPath(dir)) || fs.existsSync(path.join(dir, '_DLSS5_Backup/launch-driver-pending.json'))) return true;
    if (!fs.existsSync(meta)) return false;
    let value; try { value = JSON.parse(fs.readFileSync(meta, 'utf8')); } catch { fail('SETTINGS_RECEIPT_INVALID', '启动设置恢复记录无法解析。'); }
    if (!value || value.version !== 1 || !value.applied || typeof value.applied !== 'object') fail('SETTINGS_RECEIPT_INVALID', '启动设置恢复记录无效。');
    return Object.keys(value.applied).length > 0;
  }
  function requestRow(id, domain) { const t = target(id), state = requests(), row = state.games[t.key]?.[domain]; return row && samePath(row.exe, t.exe) ? row : null; }
  // Once the new editor owns a game directory, never revive the old directory-
  // based model writer, including after a safe EXE change. Requests stay EXE-bound.
  async function hasSrRequest(id) { const t = target(id), row = requests().games[t.key]; return Boolean(row?.sr || row?._srManaged); }
  async function markSrManaged(t) { const state = requests(); state.games[t.key] = { ...(state.games[t.key] || {}), _srManaged: { exe: t.exe, managedAt: new Date().toISOString() } }; await saveRequests(state); }
  async function save(id, domain, input) {
    return serialize(async () => { const t = target(id), request = policy.validateRequest(domain, input), state = requests();
      state.games[t.key] = { ...(state.games[t.key] || {}), [domain]: { exe: t.exe, request, savedAt: new Date().toISOString() } };
      if (domain === 'sr') state.games[t.key]._srManaged = { exe: t.exe, managedAt: new Date().toISOString() };
      await saveRequests(state); return { saved: true, applied: false, nextLaunch: true };
    });
  }
  async function inspect(id) {
    const t = target(id), row = requests().games[t.key] || {}, pendingRows = await pending(id), applied = {};
    let stored = {}, receiptError = null;
    try { stored = receipt(t).applied; }
    catch (error) {
      if (!pendingRows.length) throw error;
      receiptError = { code: error.code || 'SETTINGS_RECEIPT_INVALID', message: error.message };
    }
    let driverCurrent = null;
    for (const [domain, value] of Object.entries(stored)) {
      let readbackVerified = false;
      try {
        if (['native', 'nvidia'].includes(value.backend)) { driverCurrent ||= await driver.read(t.exe, DRIVER_IDS); readbackVerified = policy.same(driverCurrent.profile, value.profile) && policy.same(subset(driverCurrent, value.ids), value.lastValues); }
        else { const text = await policy.readText(path.join(path.dirname(t.exe), value.name)); const now = policy.values(text, value.backend); readbackVerified = Object.keys(value.lastValues).every(key => policy.same(now[key], value.lastValues[key])); }
      } catch {}
      applied[domain] = { ...value, readbackVerified };
    }
    let legacy = null;
    if (legacySrModel?.migrationInfo) {
      try { legacy = { ...await legacySrModel.migrationInfo(id), managed: await hasSrRequest(id) }; }
      catch (error) { legacy = { managed: await hasSrRequest(id), error: { code: error.code || 'SETTINGS_LEGACY_MIGRATION', message: error.message } }; }
    }
    const visibleRequests = Object.fromEntries(['sr', 'fg'].filter(domain => row[domain]).map(domain => [domain, row[domain]]));
    const notice = receiptError ? '检测到未完成事务，当前启动设置记录需要先恢复。' : legacy?.error ? '旧 SR 设置需要核对；FG 状态和恢复功能仍可使用。' : null;
    return { hardware: await detectHardware(), requests: visibleRequests, applied, legacy, pending: pendingRows, notice, receiptError, runtimeVerified: false,
      note: '配置读回只证明文件或驱动请求已保存，不代表游戏实际采用。' };
  }
  async function assertStopped(t) {
    if (!environment) { await assertGameClosed(t.dir, t.exe); return; }
    const env = await environment(t.dir, t.exe);
    if (!env || env.verified !== true) fail('PROCESS_UNKNOWN', '无法确认游戏已退出，暂不写入启动设置。');
    if (Array.isArray(env.running) && env.running.length) fail('GAME_RUNNING', '游戏仍在运行，请完全退出后重试。');
  }
  async function preview(id, domain, input) {
    const t = target(id); await assertReady(id); await noLinks(t.exe);
    const saved = requestRow(id, domain), request = policy.validateRequest(domain, input || saved?.request);
    const currentReceipt = receipt(t), old = currentReceipt.applied[domain] || null, restoring = policy.isRestore(domain, request);
    if (domain === 'fg' && !restoring && options.assertComponents) await options.assertComponents(id);
    if (old && !samePath(old.exe, t.exe)) fail('SETTINGS_EXE_CHANGED', '请先恢复原 EXE 的启动设置。');
    if (old && old.backend !== request.backend && !restoring) fail('SETTINGS_RESTORE_FIRST', '请先恢复当前设置后端。');
    const plan = { id: crypto.randomUUID(), expires: Date.now() + 120000, gameId: id, domain, request, backend: restoring && old ? old.backend : request.backend,
      target: t, exeHash: await digestFile(t.exe), restoring, old: clone(old), receipt: currentReceipt, warnings: [], blockers: [], driver: null, change: null, operations: [], noOp: false, requiresReview: false };
    const hardware = await detectHardware();
    const series = [...new Set(Array.isArray(hardware?.series) ? hardware.series : [])];
    if (restoring && !old) plan.noOp = true;
    else if (['native', 'nvidia'].includes(plan.backend)) {
      if (plan.backend === 'nvidia' && !restoring && !(series.length === 1 && series[0] === 'RTX50')) plan.blockers.push('未唯一确认 RTX 50，官方 FG 请求不会写入。');
      const before = await driver.read(t.exe, DRIVER_IDS), after = clone(before);
      const operations = restoring ? [] : (plan.backend === 'native' ? policy.nativeSr(request) : policy.nativeFg(request)).operations;
      plan.operations = clone(operations);
      const ids = restoring ? old.ids : [...new Set([...(old?.ids || []), ...operations.map(row => row.id)])];
      if (old && (!policy.same(before.profile, old.profile) || !policy.same(subset(before, old.ids), old.lastValues)))
        fail('SETTINGS_EXTERNAL_CHANGE', '驱动配置身份或设置已被其他工具改变。');
      const baseline = clone(old?.baseline || before);
      for (const settingId of ids) if (!old?.ids.includes(settingId)) baseline.settings[settingId] = clone(before.settings[settingId]);
      if (restoring) {
        for (const settingId of ids) after.settings[settingId] = clone(baseline.settings[settingId]);
        const other = Object.entries(currentReceipt.applied).some(([name, value]) => name !== domain && ['native', 'nvidia'].includes(value.backend));
        if (!other && !currentReceipt.driverOriginalProfile && !Object.values(after.settings).some(value => value.kind === 'explicit')) after.profile = null;
      } else for (const operation of operations) after.settings[operation.id] = { kind: 'explicit', value: operation.value, location: 0, predefined: false };
      plan.driver = { before, after, ids, baseline };
      plan.noOp = !restoring && policy.same(subset(before, ids), subset(after, ids));
    } else {
      if (plan.backend === 'rtx40' && !restoring && !(series.length === 1 && series[0] === 'RTX40')) plan.blockers.push('未唯一确认 RTX 40，社区 FG 请求不会写入。');
      const controlRoot = path.dirname(t.exe);
      const name = old?.name || await policy.controlPath(controlRoot, plan.backend), file = path.join(controlRoot, name);
      const before = await policy.readText(file, true);
      if (before === null) fail('SETTINGS_BACKEND_MISSING', '没有找到该后端的现有配置。');
      const currentValues = policy.values(before, plan.backend);
      if (old && !policy.same(Object.fromEntries(Object.keys(old.lastValues).map(key => [key, currentValues[key]])), old.lastValues)) fail('SETTINGS_EXTERNAL_CHANGE', '本工具接管的配置键已被外部修改。');
      let content, baselineText = old?.baselineText || before;
      if (restoring) content = policy.restoreText(before, baselineText, old.lastValues, plan.backend);
      else { const result = policy.compileFile(before, request); content = result.content; plan.warnings.push(...result.warnings); plan.requiresReview = result.requiresReview === true; }
      const afterValues = policy.values(content, plan.backend), keys = Object.keys(afterValues).filter(key => !policy.same(currentValues[key], afterValues[key]));
      if (old && !restoring) {
        const newlyOwned = keys.filter(key => !Object.hasOwn(old.lastValues, key));
        if (newlyOwned.length) {
          // A key first changed by this request belongs to its current value,
          // not to an unrelated value captured when another key was acquired.
          const baselineValues = policy.values(baselineText, plan.backend);
          baselineText = policy.restoreText(baselineText, before,
            Object.fromEntries(newlyOwned.map(key => [key, baselineValues[key]])), plan.backend);
        }
      }
      plan.operations = keys.map(key => ({ action: restoring ? 'restore-config-key' : 'set-config-key', key }));
      const owned = [...new Set([...Object.keys(old?.lastValues || {}), ...keys])];
      plan.change = { name, file, beforeHash: policy.hash(before), afterHash: policy.hash(content), content, baselineText,
        lastValues: Object.fromEntries(owned.map(key => [key, afterValues[key]])) };
      plan.noOp = !restoring && plan.change.beforeHash === plan.change.afterHash;
    }
    plans.set(plan.id, plan);
    return { id: plan.id, gameId: id, domain, backend: plan.backend, request, exe: t.exe,
      destination: plan.driver ? 'NVIDIA 每游戏配置' : plan.change ? path.relative(t.dir, plan.change.file) : null,
      operations: clone(plan.operations), warnings: plan.warnings, blockers: plan.blockers,
      noOp: plan.noOp, requiresReview: plan.requiresReview, driverSettings: plan.driver?.ids || [], runtimeVerified: false };
  }
  function sameOwned(actual, expected, ids) { return ids.every(id => expected.settings[id].kind === 'explicit'
    ? policy.same(actual.settings[id], expected.settings[id]) : actual.settings[id].kind !== 'explicit'); }
  async function restoreDriverPending(t, forceRestore = false) {
    const file = driverPendingFile(t); if (!fs.existsSync(file)) return false;
    const row = readJson(file, null);
    if (!row || row.version !== 1 || !/^[a-f0-9-]{36}$/.test(row.transactionId || '') || !samePath(row.exe, t.exe) || !['sr', 'fg'].includes(row.domain) || typeof row.restoring !== 'boolean' ||
        !Array.isArray(row.ids) || !row.ids.length || new Set(row.ids).size !== row.ids.length || row.ids.some(id => !policy.IDS[row.domain].includes(id)) || !validSnapshot(row.before, t) || !validSnapshot(row.desired, t) || row.after != null && !validSnapshot(row.after, t)) fail('SETTINGS_RECEIPT_INVALID', '驱动恢复记录无效。');
    const current = await driver.read(t.exe, DRIVER_IDS);
    if (!forceRestore && !fs.existsSync(journal.pendingPath(t.dir))) {
      const state = receipt(t), applied = state.applied[row.domain];
      const committed = state.lastTransaction?.id === row.transactionId && state.lastTransaction?.domain === row.domain &&
        (row.restoring ? !applied : applied && samePath(applied.exe, t.exe) && sameOwned(current, row.after || row.desired, row.ids));
      if (committed) { await fsp.unlink(file); return false; }
    }
    if (!sameOwned(current, row.before, row.ids) && !sameOwned(current, row.after || row.desired, row.ids)) fail('SETTINGS_EXTERNAL_CHANGE', '驱动设置在中断后被外部修改。');
    if (!sameOwned(current, row.before, row.ids)) { const desired = clone(current); for (const id of row.ids) desired.settings[id] = clone(row.before.settings[id]); desired.profile = clone(row.before.profile); if (desired.profile === null && Object.values(desired.settings).some(value => value.kind === 'explicit')) desired.profile = clone(current.profile); await driver.write(t.exe, current, desired); }
    await fsp.unlink(file); return true;
  }
  async function recover(id) {
    return serialize(async () => { const t = target(id); await assertStopped(t); const driverRestored = await restoreDriverPending(t);
      const filesRestored = fs.existsSync(journal.pendingPath(t.dir)) ? await journal.recover(t.dir) : false;
      return { recovered: driverRestored || filesRestored, driverRestored, filesRestored };
    });
  }
  async function apply(planId, consent = {}) {
    return serialize(async () => {
      let plan = plans.get(planId); plans.delete(planId);
      if (!plan || plan.expires < Date.now()) fail('PLAN_EXPIRED', '预览已过期。');
      if (consent.confirm !== true) fail('CONFIRM_REQUIRED', '请确认预览后再应用。');
      if (consent.automatic === true && plan.requiresReview && !plan.noOp) fail('SETTINGS_REVIEW_REQUIRED', '现有配置含仅生成帧调试输出，需要手动预览确认。');
      if (plan.blockers.length) fail('SETTINGS_BLOCKED', plan.blockers.join('\n'));
      await assertReady(plan.gameId); const t = target(plan.gameId);
      if (!samePath(t.exe, plan.target.exe)) fail('SETTINGS_EXE_CHANGED', '游戏 EXE 已改变。');
      await noLinks(t.exe);
      if (!plan.restoring && peBitness(t.exe) !== 64) fail('SETTINGS_EXE_INVALID', '启动设置只支持当前绑定的 Windows x64 游戏 EXE。');
      if (!plan.restoring && (!plan.exeHash || await digestFile(t.exe) !== plan.exeHash)) fail('SETTINGS_EXE_CHANGED', '游戏 EXE 在预览后改变。');
      await assertStopped(t);
      if (plan.domain === 'fg' && !plan.restoring && options.assertComponents) await options.assertComponents(plan.gameId);
      if (plan.domain === 'sr' && legacySrModel?.migrationInfo) {
        const info = await legacySrModel.migrationInfo(plan.gameId);
        if (info?.baselineCaptured) {
          await legacySrModel.prepareMigration(plan.gameId);
          const replacement = await preview(plan.gameId, plan.domain, plan.request);
          plan = plans.get(replacement.id); plans.delete(replacement.id);
        }
      }
      if (plan.domain === 'sr') await markSrManaged(t);
      if (plan.noOp) return { applied: false, skipped: true, noOp: true, runtimeVerified: false };
      const transactionId = crypto.randomUUID(); let result;
      try { result = await journal.transaction(t.dir, async () => {
        const metaFile = receiptFile(t); await journal.capture(t.dir, metaFile);
        const currentReceipt = receipt(t, { allowEmptyRebind: !plan.old && Object.keys(plan.receipt.applied).length === 0 });
        if (plan.change) {
          await journal.capture(t.dir, plan.change.file);
          if (await digestFile(plan.change.file) !== plan.change.beforeHash) fail('FILE_CHANGED', '配置在预览后改变。');
          await noLinks(plan.change.file); await fsp.writeFile(plan.change.file, plan.change.content, 'utf8');
          if (await digestFile(plan.change.file) !== plan.change.afterHash) fail('WRITE_VERIFY_FAILED', '配置写入后校验失败。');
        }
        let driverAfter = null;
        if (plan.driver) {
          const current = await driver.read(t.exe, DRIVER_IDS);
          if (!policy.same(current, plan.driver.before)) fail('SETTINGS_EXTERNAL_CHANGE', '驱动配置在预览后改变。');
          const pendingFile = driverPendingFile(t), row = { version: 1, transactionId, domain: plan.domain, restoring: plan.restoring, exe: t.exe, ids: plan.driver.ids, before: current, desired: plan.driver.after, after: null };
          await noLinks(pendingFile); await atomicJson(pendingFile, row);
          try { driverAfter = await driver.write(t.exe, current, plan.driver.after); row.after = driverAfter; await atomicJson(pendingFile, row); }
          catch (error) {
            try { await restoreDriverPending(t); } catch (recoveryError) { throw Object.assign(new Error('驱动设置恢复未完成。'), { code: 'SETTINGS_RECOVERY_FIRST', cause: error, recoveryError }); }
            throw error;
          }
        }
        const next = clone(currentReceipt.applied);
        if (plan.restoring) delete next[plan.domain];
        else if (plan.driver) next[plan.domain] = { exe: t.exe, backend: plan.backend, request: plan.request, profile: driverAfter.profile, ids: plan.driver.ids,
          baseline: plan.driver.baseline, lastValues: subset(driverAfter, plan.driver.ids), runtimeVerified: false };
        else next[plan.domain] = { exe: t.exe, backend: plan.backend, request: plan.request, name: plan.change.name,
          baselineText: plan.change.baselineText, lastValues: plan.change.lastValues, runtimeVerified: false };
        if (plan.driver && !Object.values(currentReceipt.applied).some(value => ['native', 'nvidia'].includes(value.backend)))
          currentReceipt.driverOriginalProfile = clone(plan.driver.before.profile);
        currentReceipt.applied = next; currentReceipt.lastTransaction = { id: transactionId, domain: plan.domain, restoring: plan.restoring }; await writeReceipt(t, currentReceipt);
        return { applied: true, transaction: 'file-journal', runtimeVerified: false };
      }); } catch (error) {
        if (plan.driver && fs.existsSync(driverPendingFile(t))) {
          try { await restoreDriverPending(t, true); }
          catch (recoveryError) { throw Object.assign(new Error('驱动设置恢复未完成。'), { code: 'SETTINGS_RECOVERY_FIRST', cause: error, recoveryError }); }
        }
        throw error;
      }
      if (plan.driver) await fsp.unlink(driverPendingFile(t));
      return result;
    });
  }
  async function restore(id, domain) {
    const t = target(id), current = receipt(t).applied[domain];
    const backend = current?.backend || (domain === 'sr' ? 'native' : 'nvidia');
    const request = domain === 'sr' ? { backend, quality: 'game' } : { backend, mode: 'restore' };
    const plan = await preview(id, domain, request), result = await apply(plan.id, { confirm: true });
    const state = requests();
    if (state.games[t.key]) { delete state.games[t.key][domain]; if (!Object.keys(state.games[t.key]).length) delete state.games[t.key]; await saveRequests(state); }
    return result;
  }
  async function beforeLaunch(id) {
    const t = target(id), rows = requests().games[t.key] || {}, outcomes = [];
    for (const domain of ['sr', 'fg']) {
      const row = rows[domain]; if (!row) continue;
      if (!samePath(row.exe, t.exe)) { outcomes.push({ domain, applied: false, code: 'SETTINGS_EXE_CHANGED', reason: '请求属于另一个 EXE。' }); continue; }
      try { const plan = await preview(id, domain, row.request); const result = await apply(plan.id, { confirm: true, automatic: true }); outcomes.push({ domain, ...result }); }
      catch (error) { outcomes.push({ domain, applied: false, code: error.code || 'SETTINGS_FAILED', reason: error.message }); }
    }
    return outcomes;
  }
  return Object.freeze({ inspect, save, preview, apply, beforeLaunch, recover, restore, pending, assertReady, hasSrRequest, hasOwnedState,
    requestFile, receiptFile: id => receiptFile(target(id)), driverPendingFile: id => driverPendingFile(target(id)) });
}

module.exports = { createLaunchSettingsService };
