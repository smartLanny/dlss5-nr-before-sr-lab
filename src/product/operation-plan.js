'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { noLinks, digestFile, atomicJson } = require('./launch-safety');
const { PUBLIC_NR_KEYS } = require('./constants');
const { normalizeValue } = require('./nr-config');
const { normalizeBinding } = require('./hotkeys');
const policy = require('./launch-settings-policy');
const fail = (code, message, details) => { throw Object.assign(new Error(message), { code: `OPERATION_${code}`, details }); };
const clone = value => structuredClone(value);
const hash = value => crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
const fingerprint = ({ request, before, changes, blockers }) => hash({ request, before,
  changes: changes.map(row => ['receipt', 'manifest', 'history-receipt'].includes(row.role) && row.afterSha256 !== null ? { ...row, afterSha256: 'generated-manager-record' } : row), blockers });
const same = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();

function validateRequest(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).some(key => !['route', 'api', 'version', 'deployment', 'loadingMode', 'nr', 'sr', 'fg', 'hotkeys', 'launchMode', 'uninstall', 'reapplyExternalChanges'].includes(key))) fail('INPUT', '应用请求含未知选项。');
  const request = clone(value);
  if (request.api !== undefined && !['auto', 'dx11', 'dx12', 'vulkan'].includes(request.api)) fail('INPUT', 'API 选择无效。');
  if (request.version !== undefined && (typeof request.version !== 'string' || !/^[a-zA-Z0-9._-]{1,100}$/.test(request.version))) fail('INPUT', 'Core 版本无效。');
  if (request.deployment !== undefined && !['local', 'external'].includes(request.deployment)) fail('INPUT', '部署模式无效。');
  if (request.loadingMode !== undefined && !['proxy', 'helper'].includes(request.loadingMode)) fail('INPUT', '加载方式无效。');
  if (request.route !== undefined && !['native', 'feeder', 'vulkan'].includes(request.route)) fail('INPUT', '增强路线无效。');
  if (request.launchMode !== undefined && !['auto', 'steam', 'exe'].includes(request.launchMode)) fail('INPUT', '启动方式无效。');
  if (request.reapplyExternalChanges !== undefined && typeof request.reapplyExternalChanges !== 'boolean') fail('INPUT', '重新应用选项无效。');
  if (request.nr !== undefined) {
    if (!request.nr || typeof request.nr !== 'object' || Array.isArray(request.nr) || Object.keys(request.nr).some(key => !PUBLIC_NR_KEYS.includes(key))) fail('INPUT', 'NR 参数无效。');
    for (const [key, value] of Object.entries(request.nr)) {
      if (typeof value !== 'number' && typeof value !== 'boolean' || !Number.isFinite(Number(value))) fail('INPUT', 'NR 参数必须为有效数值。');
      request.nr[key] = normalizeValue(key, value);
    }
  }
  for (const domain of ['sr', 'fg']) if (request[domain] !== undefined) request[domain] = policy.validateRequest(domain, request[domain]);
  if (request.hotkeys !== undefined) {
    if (!request.hotkeys || typeof request.hotkeys !== 'object' || Object.keys(request.hotkeys).some(key => key !== 'reshade') || !request.hotkeys.reshade) fail('INPUT', '只支持修改受管 ReShade 面板快捷键。');
    request.hotkeys = { reshade: normalizeBinding(request.hotkeys.reshade) };
  }
  if (request.uninstall !== undefined && (!['clean', 'restore'].includes(request.uninstall) || Object.keys(request).length !== 1)) fail('INPUT', '移除必须单独选择干净移除或恢复安装前。');
  return request;
}

function createOperationPlans({ userData, service, settings, components, fgWorkflow, environment, preparation, guards,
  applyEnhancement, restoreForUninstall, setLaunchMode, inspectLaunchMode, onChange = () => {} }) {
  const plans = new Map(), directory = path.join(userData, 'operation-plans');
  function target(id) {
    const exe = service.gameExecutable(id), game = service.gameDirectory(id);
    if (!path.isAbsolute(exe || '') || !path.isAbsolute(game || '')) fail('TARGET', '请先确认游戏 EXE。');
    return { id, exe, game, key: hash(path.resolve(exe).toLowerCase()) };
  }
  const ledgerFile = t => path.join(directory, `${t.key}.pending.json`);
  async function read(file) {
    await noLinks(file);
    try { const stat = await fs.stat(file); if (!stat.isFile() || stat.size > 1024 * 1024) fail('RECORD', '操作记录大小无效。'); return JSON.parse(await fs.readFile(file, 'utf8')); }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  }
  async function inspect(id) {
    const t = target(id), record = await read(ledgerFile(t));
    if (record && (record.version !== 1 || !same(record.exe, t.exe) || !same(record.game, t.game) || !Array.isArray(record.stages))) fail('RECORD', '未完成操作记录与当前游戏不一致。');
    return { pending: Boolean(record), record, runtimeVerified: false };
  }
  async function assertReady(id) { if ((await inspect(id)).pending) fail('RECOVERY_REQUIRED', '上次统一应用未完成，请先在维护页恢复未完成操作。'); }
  async function snapshot(t) {
    const layout = service.getLayout(t.id), store = service.store.read(), key = path.resolve(t.game).toLowerCase();
    const files = [...new Set([layout.activeConfigPath, path.join(layout.nrConfigDir || layout.runtimeDir, 'nr_before_sr.ini'),
      path.join(t.game, '_DLSS5_Backup', 'xiaofeng-manager.json'), path.join(t.game, '_DLSS5_Backup', 'xiaofeng-external.json')].filter(Boolean))];
    const identities = [];
    for (const file of files) { await noLinks(file); identities.push({ file, sha256: await digestFile(file) }); }
    return { exe: t.exe, exeSha256: await digestFile(t.exe), layout: { mode: layout.mode, source: layout.source, loadingMode: layout.loadingMode, generation: layout.generation, runtimeDir: layout.runtimeDir, activeConfigPath: layout.activeConfigPath },
      identities, override: store.gameOverrides[key] || null };
  }
  async function preview(id, input) {
    const request = validateRequest(input), t = target(id); await assertReady(id);
    await preparation.assertReady(id); await environment.assertReady(id); await settings.assertReady(id);
    const current = (await service.listGames()).find(game => game.id === id);
    if (!current) fail('TARGET', '找不到所选游戏。');
    const before = await snapshot(t), changes = [], blockers = [], steps = [];
    let retainedFg = null;
    const specialOwner = ['vulkan', 'feeder'].includes(request.route) || ['vulkan', 'feeder'].includes(before.layout.source) || request.api === 'vulkan';
    const changingMode = !specialOwner && request.deployment !== undefined && request.deployment !== before.layout.mode;
    if (changingMode) {
      const previous = await settings.inspect(id), componentState = await components.inspect(id);
      retainedFg = previous.requests?.fg?.request || previous.applied?.fg?.request || null;
      if (retainedFg || componentState.managed || componentState.receipt) {
        changes.push({ action: 'restore-fg-before-migration', path: before.layout.runtimeDir, description: '先恢复当前补帧配置和组件，再迁移，并按保留的补帧选择重新准备。' });
        steps.push({ kind: 'restore-fg' });
      }
    }
    let deployment = null;
    if (request.uninstall) {
      const settingsBefore = await settings.inspect(id);
      for (const domain of ['fg', 'sr']) if (settingsBefore.applied?.[domain]) {
        const previous = settingsBefore.applied[domain];
        const restore = domain === 'sr' ? { backend: previous.backend, quality: 'game' } : { backend: previous.backend, mode: 'restore' };
        const settingPlan = await settings.preview(id, domain, restore);
        changes.push(...settingPlan.operations.map(row => ({ ...row, domain, path: settingPlan.destination })));
        blockers.push(...(settingPlan.blockers || []));
      }
      deployment = await service.previewUninstall?.(id, { mode: request.uninstall }, { plannedFgRestore: true });
      changes.push(...(deployment?.changes || [{ action: request.uninstall, path: t.game, description: request.uninstall === 'clean' ? '移除摘要匹配的受管组件，保留备份且不放回旧代理。' : '恢复有原始记录的安装前文件，保留历史备份。' }]));
      steps.push({ kind: 'uninstall' });
    } else {
      const routeChange = request.api !== undefined || request.version !== undefined;
      if (routeChange || request.deployment !== undefined || request.loadingMode !== undefined || request.route !== undefined) {
        const version = request.version || current.installedVersion || current.addonVersion || service.payloadState()?.settings?.addonVersion;
        const api = request.api || current.apiOverride || 'auto';
        const special = ['feeder', 'vulkan'].includes(request.route);
        if (special && request.loadingMode === 'helper') fail('ROUTE', 'Vulkan 与 Feeder 使用各自的固定加载配套。');
        deployment = special ? await service.previewSpecialDeployment(id, { route: request.route, api, ...(request.version ? { version: request.version } : {}) }) :
          await service.previewDeployment(id, { mode: request.deployment || before.layout.mode || 'local', ...(version ? { version } : {}), api,
            ...(request.loadingMode ? { loadingMode: request.loadingMode } : {}) }, { plannedFgRestore: changingMode });
        changes.push(...(deployment.changes || [])); blockers.push(...(deployment.blockers || []).map(row => typeof row === 'string' ? row : row.message));
        steps.push({ kind: 'deployment', route: request.route, routeChange, api, version: special ? request.version : version, mode: request.deployment || before.layout.mode || 'local', loadingMode: request.loadingMode });
      }
      if (request.nr && Object.keys(request.nr).length) {
        changes.push(...Object.entries(request.nr).map(([key, value]) => ({ action: 'set-config-key', path: '当前部署 / nr_before_sr.ini', key, value })));
        steps.push({ kind: 'nr' });
      }
      if (request.hotkeys) { changes.push({ action: 'set-config-key', path: before.layout.activeConfigPath, key: 'INPUT/KeyOverlay',
        value: [request.hotkeys.reshade.key, Number(request.hotkeys.reshade.ctrl), Number(request.hotkeys.reshade.shift), Number(request.hotkeys.reshade.alt)].join(',') }); steps.push({ kind: 'hotkeys' }); }
      for (const domain of ['sr', 'fg']) if (request[domain] || domain === 'fg' && retainedFg) {
        const desired = request[domain] || retainedFg;
        const preview = await settings.preview(id, domain, desired, { allowComponentPreparation: true, reapplyExternalChanges: request.reapplyExternalChanges === true });
        changes.push(...preview.operations.map(row => ({ ...row, domain, path: preview.destination })));
        if (preview.needsPreparation || preview.preparation) changes.push({ action: 'prepare-components', domain, description: '应用时一并准备并验证所需组件。' });
        blockers.push(...(preview.blockers || []).map(row => typeof row === 'string' ? row : row.message));
        steps.push({ kind: domain, request: desired, preview });
      }
      if (request.launchMode !== undefined) {
        const status = await inspectLaunchMode(id);
        if (request.launchMode === 'steam' && !status.steamAvailable) blockers.push('没有可验证的 Steam 安装身份。');
        changes.push({ action: 'set-launch-mode', value: request.launchMode, path: t.exe }); steps.push({ kind: 'launch' });
      }
    }
    const value = { version: 1, planId: crypto.randomUUID(), gameId: id, exe: t.exe, game: t.game, request, before,
      createdAt: Date.now(), expiresAt: Date.now() + 10 * 60000, changes, blockers: [...new Set(blockers.filter(Boolean))],
      steps, deployment, runtimeVerified: false, requiresConfirmation: true };
    value.fingerprint = fingerprint(value); plans.set(value.planId, value);
    await atomicJson(path.join(directory, `${value.planId}.preview.json`), value);
    return clone(value);
  }
  async function loadPlan(planId, expectedFingerprint) {
    if (!/^[a-f0-9-]{36}$/i.test(planId || '')) fail('PLAN', '操作预览编号无效。');
    const plan = plans.get(planId) || await read(path.join(directory, `${planId}.preview.json`));
    if (!plan || plan.version !== 1 || plan.planId !== planId || plan.expiresAt < Date.now()) fail('EXPIRED', '操作预览已过期，请重新预览。');
    validateRequest(plan.request);
    if (fingerprint(plan) !== plan.fingerprint || expectedFingerprint && expectedFingerprint !== plan.fingerprint) fail('CHANGED', '操作预览身份已改变，未执行。');
    // Recompile all executable steps from the authenticated request. Never
    // execute arbitrary step names or paths supplied by a persisted JSON file.
    const fresh = await preview(plan.gameId, plan.request);
    if (fresh.fingerprint !== plan.fingerprint) fail('CHANGED', '游戏、配置或可用操作在预览后改变，请重新核对清单。');
    return fresh;
  }
  async function archive(t, record) {
    await atomicJson(path.join(directory, 'history', `${t.key}-${record.planId}.json`), record);
    await fs.unlink(ledgerFile(t));
  }
  async function apply(planId, consent = {}) {
    if (consent.confirm !== true) fail('CONFIRM', '请先核对本次具体变更清单。');
    if (!/^[a-f0-9]{64}$/.test(consent.fingerprint || '')) fail('CONFIRM', '应用必须绑定已核对的预览身份。');
    const plan = await loadPlan(planId, consent.fingerprint), t = target(plan.gameId);
    if (plan.blockers.length) fail('BLOCKED', plan.blockers.join('；'));
    await guards.assertGameClosed(t.game, t.exe);
    const record = { version: 1, planId: plan.planId, gameId: t.id, exe: t.exe, game: t.game, request: plan.request,
      before: plan.before, changes: plan.changes, startedAt: new Date().toISOString(), stages: [], status: 'applying' };
    const persist = () => atomicJson(ledgerFile(t), record);
    await persist();
    try {
      for (const step of plan.steps) {
        await guards.assertGameClosed(t.game, t.exe);
        const stage = { kind: step.kind, status: 'started' }; record.stages.push(stage); await persist();
        if (step.kind === 'deployment') {
          const special = ['feeder', 'vulkan'].includes(step.route);
          const migration = special ? await service.previewSpecialDeployment(t.id, { route: step.route, api: step.api, ...(step.version ? { version: step.version } : {}) }) :
            await service.previewDeployment(t.id, { mode: step.mode, ...(step.version ? { version: step.version } : {}), api: step.api,
              ...(step.loadingMode ? { loadingMode: step.loadingMode } : {}) });
          stage.result = await (special ? service.applySpecialDeployment : service.applyDeployment)(migration.planId, { allowAntiCheat: consent.allowAntiCheat === true });
        } else if (step.kind === 'nr') stage.result = await service.writeNrSettings(t.id, plan.request.nr);
        else if (step.kind === 'hotkeys') stage.result = await service.writeGameHotkey(t.id, 'reshade', plan.request.hotkeys.reshade);
        else if (step.kind === 'restore-fg') { await settings.restore(t.id, 'fg'); stage.result = await components.restore(t.id); }
        else if (step.kind === 'sr' || step.kind === 'fg') stage.result = await applyEnhancement(t.id, step.kind, step.request, { allowAntiCheat: consent.allowAntiCheat === true, reapplyExternalChanges: plan.request.reapplyExternalChanges === true });
        else if (step.kind === 'launch') stage.result = await setLaunchMode(t.id, plan.request.launchMode);
        else if (step.kind === 'uninstall') {
          await restoreForUninstall(t.id);
          stage.result = await service.uninstall(t.id, { mode: plan.request.uninstall, removeSettings: false });
        }
        stage.status = 'complete'; await persist();
      }
      record.status = 'complete'; record.completedAt = new Date().toISOString(); await persist(); await archive(t, record);
      onChange(t.id); return { applied: true, planId: record.planId, stages: record.stages, runtimeVerified: false, notice: '所选配置已应用。需要重启游戏，实际增强效果仍待本次运行确认。' };
    } catch (error) {
      record.status = 'recovery-required'; record.error = { code: error.code, message: error.message,
        completedPhases: error.details?.completedPhases || [] }; await persist();
      throw Object.assign(error, { details: { ...error.details, operationPlan: record.planId, completedStages: record.stages.filter(row => row.status === 'complete').map(row => row.kind), recoveryRequired: true } });
    }
  }
  async function recover(id) {
    const t = target(id), { record } = await inspect(id); if (!record) return { recovered: false };
    await guards.assertGameClosed(t.game, t.exe);
    // Each owner verifies its own before/after digests. Completed stages remain
    // visible; recovery never claims to roll back a previous full configuration.
    await service.recoverDeployment?.(id);
    const pending = await settings.pending(id);
    if (pending.length) {
      try { await settings.recover(id); }
      catch (error) {
        if (/FG_/.test(error.code || '')) await fgWorkflow.recover(id);
        else if (/REF_/.test(error.code || '')) await service.recoverReframework(id);
        else throw error;
      }
    }
    await environment.recoverPending(id);
    if ((await preparation.inspect(id)).pending) await preparation.recover(id);
    await settings.assertReady(id); await environment.assertReady(id);
    const deployment = await service.inspectDeployment(id);
    if (deployment.needsRecovery || deployment.pending) fail('RECOVERY_REQUIRED', '部署仍有未完成文件事务，请先恢复该部署。');
    record.status = 'recovered'; record.recoveredAt = new Date().toISOString(); await archive(t, record);
    return { recovered: true, stages: record.stages, notice: '未完成文件事务已恢复；此前已完成的设置仍保留，请核对后重新应用。', runtimeVerified: false };
  }
  return { preview, apply, inspect, recover, assertReady, loadPlan };
}
module.exports = { createOperationPlans, validateOperationRequest: validateRequest };
