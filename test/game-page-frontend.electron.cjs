'use strict';
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'game-page-ui-'));
app.setPath('userData', path.join(temporary, 'profile')); app.disableHardwareAcceleration();
function installMock() {
  const ok = value => ({ ok: true, value: structuredClone(value) });
  const game = { id: 'fixture', name: '博德之门 3 · 界面测试', dir: 'C:\\UI-fixture\\Baldurs Gate 3', installed: true, supported: true, launcher: 'Steam',
    addonVersion: '0.4.7beta', apiOverride: 'auto', nativeDlssAvailable: true, nativeFgAvailable: true,
    chosen: { path: 'C:\\UI-fixture\\Baldurs Gate 3\\bin\\bg3_dx11.exe', bitness: 64, apiResolution: { api: 'dx11', source: 'entry' } } };
  const feature = { eligible: true, state: 'configurable', canConfirm: false, blockers: [], availableMultipliers: [2, 3, 4], availableModes: ['follow', 'fixed'], evidence: {} };
  const mock = window.__gpMock = { calls: [], listener: null, plan: null, failApply: false, removed: false,
    assessment: { schema: 1, gameId: game.id, game,
      api: { capabilities: ['dx11'], configuredApi: null, effectiveApi: 'dx11', observedApi: null, presentationApi: null, confidence: 'high',
        bridgeStatus: { required: true, kind: 'DX11 桥接', verified: false }, evidence: [{ api: 'dx11', source: 'selected-executable', path: game.chosen.path, message: '已验证 BG3 DX11 独立入口与 PE 导入。' }], conflicts: [], coverage: { complete: true, exeImports: true } },
      layout: { mode: 'local', loadingMode: 'proxy', activeConfigPath: 'C:\\UI-fixture\\bin\\ReShade.ini', runtimeDir: 'C:\\UI-fixture\\bin' },
      deployment: { mode: 'local', loadingMode: 'proxy', version: '0.4.7beta', verified: true, needsRecovery: false },
      enhancements: { hardware: { family: 'RTX40', series: ['RTX40'], names: ['NVIDIA RTX 4070 Ti SUPER'], source: 'fixture' }, featureStates: { sr: structuredClone(feature), fg: structuredClone(feature) }, requests: {}, applied: {}, pending: [] },
      nr: { Enabled: 1, Intensity: 1, WorkMode: 0, CustomWorkScale: 1, Style: 0, AutoMask: 0, ColorStrength: .75, SkinStructureStrength: -1, TransferStrength: 1, PostTransferStrength: 1,
        capabilities: { WorkMode: true, CustomWorkScale: true, ColorStrength: true, SkinStructureStrength: true, TransferStrength: true, PostTransferStrength: true } },
      hotkeys: { nr: { label: 'F6 开关' }, reshade: { key: 36, ctrl: false, shift: false, alt: false } },
      operation: { pending: false }, launch: { selected: 'auto', effective: 'steam', steamAvailable: true, session: null },
      verification: { helper: { status: 'not-applicable', detail: '当前代理路线无需加载助手。' }, core: { status: 'unverified', detail: '等待本次游戏进程确认指定身份的 Core。' },
        nr: { status: 'unverified', detail: '等待成功与提交计数持续增长。' }, visual: { status: 'unverified', detail: '需同场景开关对照与 F8 记录。' } },
      maintenance: { remainingFiles: [{ name: 'other-addon.addon64', kind: '其他模组，保留', sha256: 'c'.repeat(64) }], files: [], isolated: false, scope: '只检查所选 EXE 同目录图形代理与 Add-on。' },
      helperModules: { modules: [] }, antiCheat: { detected: false }, failures: [],
      coreVersions: [{ id: '0.4.7beta', label: 'beta0.4.7', ready: true, notes: '原生 DX12 与 DX11 桥接配套；运行结果独立验证。' },
        { id: '0.4.7beta-bg3-bridge1411', label: 'beta0.4.7 · BG3 桥接 1.4.11 对照', ready: true, notes: '同 Core 固定桥接对照，实际 NR 尚未验证。' }] } };
  mock.baseline = structuredClone(mock.assessment);
  const payload = { ready: true, selectedVersion: '0.4.7beta', versions: { '0.4.7beta': { label: 'beta0.4.7', variants: { RTX40: { ready: true, files: [] } } } }, source: { mode: 'bundled', path: 'C:\\UI-fixture\\payload', ready: true } };
  const empty = async () => ok(null);
  window.manager = {
    boot: async () => ok({ product: { name: 'DLSS 5 AI 超分管理器', edition: '装机宅版', version: '0.4.8 · UI fixture' }, settings: {}, hardware: mock.assessment.enhancements.hardware, payload, addons: [], games: [game] }),
    listGames: async () => ok(mock.removed ? [] : [mock.assessment.game]), refresh: async () => ok(mock.removed ? [] : [mock.assessment.game]),
    assessGame: async id => { mock.calls.push(['assess', id]); return ok(mock.assessment); },
    readPayloadSource: async () => ok({ settings: {}, payload, addons: [] }), getStartupContext: async () => ok({ mode: 'normal', sandbox: true, privilege: 'standard', operation: {} }),
    getGameIcon: empty, fetchGameArt: empty, listAddons: async () => ok([]), onAddonImported() {}, onSrModelApplied() {}, onLaunchSettingsApplied() {},
    startupReady() {}, startupFailed: message => { mock.calls.push(['startup-failed', message]); }, minimize() {}, close() {}, diagnose: async () => ok({ components: [], checks: [], issues: [] }),
    onLaunchSession: callback => { mock.listener = callback; return () => {}; },
    previewOperation: async (_id, request) => { mock.calls.push(['preview', structuredClone(request)]); mock.plan = { request: structuredClone(request), planId: 'fixture-plan', fingerprint: 'd'.repeat(64), blockers: [],
      changes: request.uninstall ? [{ action: request.uninstall, name: 'nr-before-sr.zh-CN.addon64', path: game.dir, beforeSha256: 'a'.repeat(64), afterSha256: null }] : [{ action: 'set-config-key', key: 'Intensity', value: request.nr?.Intensity || 1, path: 'nr_before_sr.ini' }] }; return ok(mock.plan); },
    applyOperation: async (_id, _planId, consent) => { mock.calls.push(['apply', consent]); if (mock.failApply) { mock.assessment.operation = { pending: true, record: { error: { message: '测试文件被占用' }, stages: [{ kind: 'deployment', status: 'complete' }] } }; return { ok: false, error: { message: '测试文件被占用；请恢复未完成操作。' } }; }
      if (mock.plan.request.nr) Object.assign(mock.assessment.nr, mock.plan.request.nr);
      if (mock.plan.request.hotkeys) Object.assign(mock.assessment.hotkeys, structuredClone(mock.plan.request.hotkeys));
      if (mock.plan.request.uninstall) mock.assessment.game.installed = false; return ok({ applied: true, notice: '测试配置已应用。' }); },
    applyOperationElevated: async () => { mock.calls.push(['elevated']); return ok({ applied: true }); },
    previewEnvironmentCleanup: async id => { mock.calls.push(['cleanup-preview', id]); return ok({ planId: 'fixture-cleanup-plan', scope: '只隔离所选 EXE 同目录的已勾选文件。',
      candidates: [{ name: 'other-addon.addon64', kind: '其他 Add-on', note: '用户明确选择后备份隔离。', sha256: 'c'.repeat(64), selectable: true, selectedByDefault: true }] }); },
    applyEnvironmentCleanup: async (id, planId, names) => { mock.calls.push(['cleanup-apply', id, planId, structuredClone(names)]);
      mock.assessment.maintenance.isolated = true; mock.assessment.maintenance.canRestore = true; return ok({ isolated: true }); },
    confirmGameFeature: async (_id, domain) => { mock.calls.push(['confirm', domain]); mock.assessment.enhancements.featureStates[domain] = structuredClone(feature); return ok({ confirmed: true }); },
    recoverOperation: async () => { mock.calls.push(['recover']); mock.assessment.operation = { pending: false }; return ok({ recovered: true }); },
    recordVisualComparison: async (id, input) => {
      mock.calls.push(['visual-record', id, structuredClone(input)]);
      mock.assessment.verification.visual = { status: input.result === 'changed' ? 'passed' : input.result === 'unchanged' ? 'not-observed' : 'unverified',
        source: 'user-comparison', automaticVerification: false, detail: input.result === 'changed' ? '用户观察：同场景发现画面变化。' : input.result === 'unchanged' ? '用户观察：未发现画面变化。' : '用户观察：暂时无法确认。' };
      return ok({ recorded: true, notice: '已保存本次用户观察。' });
    },
    removeGame: async id => { mock.calls.push(['remove-game', id]); mock.removed = true; return ok({ removed: true }); },
    writeGameHotkey: async (...args) => { mock.calls.push(['direct-hotkey-write', ...args]); throw new Error('hotkeys must use the reviewed unified operation'); },
    uninstall: async (...args) => { mock.calls.push(['direct-uninstall', ...args]); throw new Error('library removal must not uninstall'); },
    exportFeedback: async () => ok('fixture.txt'), openFolder: empty, openExternal: async key => { mock.calls.push(['external', key]); return ok(true); },
    launch: async () => { mock.calls.push(['launch']); return ok({ launched: { status: 'waiting-enhancement' } }); }, cancelLaunch: async () => ok({ cancelled: true })
  };
}
async function smoke() {
  const assert = (ok, message) => { if (!ok) throw new Error(message); };
  const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  const until = async predicate => { for (let attempt = 0; attempt < 160; attempt++) { if (predicate()) return; await delay(25); } throw new Error('UI did not reach expected state.'); };
  const click = action => { const button = document.querySelector(`[data-gp-action="${action}"]`); assert(button && !button.disabled, 'action unavailable: ' + action); button.click(); };
  const set = (group, key, value) => { const input = document.querySelector(`[data-gp-group="${group}"][data-gp-field="${key}"]`); assert(input && !input.disabled, 'field unavailable: ' + key);
    input.value = value; input.dispatchEvent(new Event(input.type === 'range' || input.type === 'number' ? 'input' : 'change', { bubbles: true })); };
  const mock = window.__gpMock, count = kind => mock.calls.filter(row => row[0] === kind).length;
  await until(() => document.querySelector('.open-game-page-btn'));
  document.querySelector('.open-game-page-btn').click(); await until(() => document.querySelector('.gp-tabs'));
  assert(document.getElementById('view-game-detail').classList.contains('active'), 'library enters independent detail page');
  assert(!document.querySelector('.game-card .game-detail'), 'details must not expand inside library card');
  assert(document.querySelectorAll('.gp-verification article').length === 4, 'four independent acceptance cards');
  document.querySelector('[data-gp-tab="enhance"]').click();
  set('nr', 'Intensity', '.65'); set('nr', 'WorkMode', '5'); set('sr', 'quality', 'performance'); set('fg', 'mode', 'fixed'); set('fg', 'multiplier', '3');
  await delay(800); assert(count('apply') === 0 && count('preview') === 0, 'changing controls must never auto-write or auto-apply');
  assert(document.querySelector('[data-gp-group="fg"][data-gp-field="mode"] option[value="dynamic"]').disabled, 'RTX40 dynamic remains visible but disabled');
  assert(document.querySelector('[data-gp-group="fg"][data-gp-field="multiplier"] option[value="6"]').disabled, 'unproven multiplier stays disabled');
  assert(document.querySelector('[data-gp-action="launch"]').disabled, 'unapplied drafts block launch');
  click('back'); assert(document.querySelector('.gp-modal').textContent.includes('未应用'), 'leaving drafts is explicit'); click('modal-cancel');
  click('preview'); await until(() => document.querySelector('.gp-modal [data-gp-action="modal-apply"]'));
  assert(mock.plan.request.nr.Intensity === .65 && mock.plan.request.sr.quality === 'performance' && mock.plan.request.fg.multiplier === 3, 'one preview includes NR, SR, FG');
  assert(mock.plan.request.nr.WorkMode === 5, 'NR WorkMode select submits a numeric value');
  click('modal-apply'); await until(() => count('apply') === 1 && !document.querySelector('.gp-modal'));
  assert(!document.querySelector('[data-gp-action="launch"]').disabled, 'finished Apply clears draft'); assert(count('elevated') === 0, 'normal Apply does not auto-elevate');
  mock.assessment.enhancements.featureStates.sr = { eligible: false, canConfirm: true, state: 'waiting-confirmation', blockers: [{ message: '无法读取游戏内 DLSS 开关。' }] };
  click('back'); document.querySelector('.open-game-page-btn').click(); await until(() => document.querySelector('.gp-tabs')); document.querySelector('[data-gp-tab="enhance"]').click();
  assert(document.querySelector('[data-gp-group="sr"][data-gp-field="quality"]').disabled, 'unknown switch blocks editing'); click('confirm-sr');
  await until(() => count('confirm') === 1 && !document.querySelector('[data-gp-action="confirm-sr"]')); assert(count('apply') === 1, 'feature confirmation does not apply driver settings');
  document.querySelector('[data-gp-tab="maintenance"]').click(); click('uninstall-clean'); await until(() => document.querySelector('.gp-modal'));
  assert(mock.plan.request.uninstall === 'clean', 'clean uninstall choice is explicit'); click('modal-cancel'); click('uninstall-restore'); await until(() => document.querySelector('.gp-modal'));
  assert(mock.plan.request.uninstall === 'restore', 'restore choice is freshly requested'); click('modal-cancel');
  document.querySelector('[data-gp-tab="enhance"]').click(); set('nr', 'Intensity', '.7'); click('preview'); await until(() => document.querySelector('.gp-modal'));
  mock.failApply = true; click('modal-apply'); await until(() => count('apply') === 2 && !document.querySelector('.gp-modal'));
  document.querySelector('[data-gp-tab="maintenance"]').click(); assert(document.querySelector('[data-gp-action="recover-operation"]'), 'partial failure exposes recovery');
  click('recover-operation'); await until(() => count('recover') === 1); await delay(100); click('discard');
  document.querySelector('[data-gp-tab="overview"]').click();
  mock.failApply = false;
  const scenario = async (label, change) => {
    mock.assessment = structuredClone(mock.baseline); mock.assessment.game.name = label; change?.(mock.assessment);
    document.querySelector('[data-gp-tab="overview"]').click(); click('refresh');
    await until(() => document.querySelector('.gp-header h2')?.textContent === label);
  };
  const session = { sessionId: 'b4aa3424-4828-40cd-9b5b-a49e566c5130', gameId: 'fixture', targetExe: mock.assessment.game.chosen.path,
    requestedAt: new Date().toISOString(), status: 'waiting-enhancement', process: { pid: 24001, exe: mock.assessment.game.chosen.path, startedAt: new Date().toISOString() } };
  await scenario('本次画面对照 · UI fixture', assessment => { assessment.launch.session = { ...session, historical: true }; });
  assert(document.querySelector('[data-gp-action="record-visual"]').disabled, 'a historical session cannot open current visual recording');
  mock.assessment.launch.session = structuredClone(session); click('refresh'); await until(() => !document.querySelector('[data-gp-action="record-visual"]').disabled);
  const originalLayers = JSON.stringify(Object.fromEntries(['helper', 'core', 'nr'].map(key => [key, mock.assessment.verification[key]])));
  const originalCards = [...document.querySelectorAll('.gp-verification article')].slice(0, 3).map(row => row.textContent);
  const beforeVisualApply = count('apply'), beforeVisualPreview = count('preview');
  for (const result of ['changed', 'unchanged', 'uncertain']) {
    const prior = count('visual-record'); click('record-visual');
    assert(document.querySelector('.gp-modal').getAttribute('role') === 'dialog' && document.querySelector('.gp-modal h3').textContent.includes('画面对照'), 'visual recording opens its dedicated dialog');
    for (const field of document.querySelectorAll('.gp-modal input,.gp-modal select,.gp-modal textarea')) assert(field.labels.length > 0, 'every visual field has an associated label');
    click('save-visual'); await delay(30);
    assert(count('visual-record') === prior && document.querySelector('.gp-modal-message').textContent.includes('同场景'), 'same-scene confirmation is mandatory before submission');
    const checkbox = document.querySelector('[data-gp-same-scene]'); checkbox.checked = true; checkbox.dispatchEvent(new Event('change', { bubbles: true }));
    document.querySelector('[data-gp-visual-result]').value = result;
    const note = '<script>plain user note</script> 同一存档开关对照 ' + result;
    const evidence = 'C:\\UI-fixture\\evidence label only.png';
    document.querySelector('[data-gp-visual-note]').value = note; document.querySelector('[data-gp-visual-evidence]').value = evidence;
    const controls = [...document.querySelectorAll('.gp-modal input:not([disabled]),.gp-modal select:not([disabled]),.gp-modal textarea:not([disabled]),.gp-modal button:not([disabled])')];
    const first = controls[0], last = controls.at(-1); last.focus(); last.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true, cancelable: true }));
    assert(document.activeElement === first, 'Tab stays inside the visual dialog');
    first.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', shiftKey: true, bubbles: true, cancelable: true }));
    assert(document.activeElement === last, 'Shift+Tab wraps inside the dialog');
    click('save-visual'); await until(() => count('visual-record') === prior + 1 && !document.querySelector('.gp-modal'));
    const call = mock.calls.filter(row => row[0] === 'visual-record').at(-1);
    assert(call[1] === 'fixture' && JSON.stringify(call[2]) === JSON.stringify({ sessionId: session.sessionId, sameScene: true, result, note, evidenceLabel: evidence }), 'visual submission includes exact session, result, note and evidence label');
    assert(JSON.stringify(Object.fromEntries(['helper', 'core', 'nr'].map(key => [key, mock.assessment.verification[key]]))) === originalLayers, 'recording never changes helper/Core/NR evidence');
    assert([...document.querySelectorAll('.gp-verification article')].slice(0, 3).every((row, index) => row.textContent === originalCards[index]), 'only the visual acceptance card changes');
    const visualCard = document.querySelectorAll('.gp-verification article')[3];
    assert(visualCard.textContent.includes(result === 'changed' ? '已确认' : result === 'unchanged' ? '未观察到变化' : '待确认'), 'the observation result has an independent visible status');
  }
  assert(count('apply') === beforeVisualApply && count('preview') === beforeVisualPreview, 'visual observations do not submit deployment or driver changes');
  await scenario('快捷键暂存 · UI fixture'); document.querySelector('[data-gp-tab="enhance"]').click();
  const beforeHotkeyApply = count('apply'), beforeHotkeyPreview = count('preview');
  assert(document.querySelector('.gp-facts').textContent.includes('Home'), 'the current panel key starts as Home');
  click('panel-insert'); await delay(100);
  assert(mock.assessment.hotkeys.reshade.key === 36 && count('direct-hotkey-write') === 0 && count('apply') === beforeHotkeyApply && count('preview') === beforeHotkeyPreview, 'Insert changes only the draft until unified preview');
  assert(document.querySelector('[data-gp-action="launch"]').disabled, 'the pending panel shortcut blocks launch until resolved');
  click('preview'); await until(() => document.querySelector('.gp-modal'));
  assert(JSON.stringify(mock.plan.request) === JSON.stringify({ hotkeys: { reshade: { key: 45, ctrl: false, shift: false, alt: false } } }), 'the unified preview carries the exact Insert binding');
  click('modal-cancel'); assert(mock.assessment.hotkeys.reshade.key === 36, 'cancelling preview leaves Home applied');
  click('preview'); await until(() => document.querySelector('.gp-modal')); click('modal-apply');
  await until(() => count('apply') === beforeHotkeyApply + 1 && !document.querySelector('.gp-modal'));
  assert(mock.assessment.hotkeys.reshade.key === 45 && count('direct-hotkey-write') === 0, 'reviewed unified Apply owns the shortcut change');
  await scenario('反作弊游戏 · 卸载后清理', assessment => { assessment.game.installed = false; assessment.antiCheat.detected = true; });
  document.querySelector('[data-gp-tab="maintenance"]').click();
  const beforeCleanupApply = count('cleanup-apply'), beforeCleanupOperation = count('apply');
  click('clean-environment'); await until(() => document.querySelector('.gp-modal [data-gp-clean]'));
  assert(!document.querySelector('.gp-modal [data-gp-consent]'), 'cleanup has no deployment risk consent control');
  click('modal-apply'); await until(() => count('cleanup-apply') === beforeCleanupApply + 1 && !document.querySelector('.gp-modal'));
  const cleanupCall = mock.calls.filter(row => row[0] === 'cleanup-apply').at(-1);
  assert(JSON.stringify(cleanupCall.slice(1)) === JSON.stringify(['fixture', 'fixture-cleanup-plan', ['other-addon.addon64']]), 'anti-cheat cleanup submits the reviewed plan and selected filenames');
  assert(count('apply') === beforeCleanupOperation, 'cleanup uses its own operation without deployment Apply');
  await scenario('反作弊游戏 · 普通应用确认', assessment => { assessment.antiCheat.detected = true; });
  document.querySelector('[data-gp-tab="enhance"]').click(); set('nr', 'Intensity', '.6'); click('preview');
  await until(() => document.querySelector('.gp-modal [data-gp-consent]'));
  const consent = document.querySelector('.gp-modal [data-gp-consent]'), beforeConsentApply = count('apply');
  assert(consent.getClientRects().length > 0 && !consent.checked, 'ordinary anti-cheat Apply shows an unchecked consent control');
  click('modal-apply'); await delay(30);
  assert(count('apply') === beforeConsentApply && document.querySelector('.gp-modal-message').textContent.includes('反作弊'), 'ordinary Apply remains blocked until the visible risk consent is checked');
  click('modal-cancel'); click('discard');
  await scenario('SR 自定义比例 · 无效输入'); document.querySelector('[data-gp-tab="enhance"]').click();
  set('sr', 'quality', 'custom'); set('sr', 'renderPercent', '75');
  const beforeInvalidSrPreview = count('preview'); set('sr', 'renderPercent', '76.5');
  const srError = document.querySelector('.gp-message.error[role="alert"]');
  assert(srError && !srError.hidden && srError.getClientRects().length > 0 && srError.textContent.includes('整数'), 'invalid SR input displays an immediate visible validation error');
  assert(document.querySelector('[data-gp-action="preview"]').disabled, 'invalid SR input disables unified preview');
  document.querySelector('[data-gp-action="preview"]').click(); await delay(30);
  assert(count('preview') === beforeInvalidSrPreview, 'invalid SR input cannot preview the previous valid ratio');
  set('sr', 'renderPercent', '76'); click('preview'); await until(() => document.querySelector('.gp-modal'));
  assert(mock.plan.request.sr.quality === 'custom' && mock.plan.request.sr.renderPercent === 76, 'corrected SR input previews its current value');
  click('modal-cancel'); click('discard');
  await scenario('FG 动态目标 · 无效输入', assessment => {
    assessment.enhancements.hardware = { family: 'RTX50', series: ['RTX50'], names: ['NVIDIA RTX 5090'], source: 'fixture' };
    assessment.enhancements.featureStates.fg.availableModes = ['off', 'fixed', 'dynamic'];
  });
  click('back'); document.querySelector('.open-game-page-btn').click(); await until(() => document.querySelector('.gp-tabs'));
  document.querySelector('[data-gp-tab="enhance"]').click(); set('fg', 'mode', 'dynamic'); set('fg', 'targetFps', '120');
  const beforeInvalidFgPreview = count('preview'); set('fg', 'targetFps', '120.5');
  const fgError = document.querySelector('.gp-message.error[role="alert"]');
  assert(fgError && !fgError.hidden && fgError.getClientRects().length > 0 && fgError.textContent.includes('整数'), 'invalid FG input displays an immediate visible validation error');
  assert(document.querySelector('[data-gp-action="preview"]').disabled, 'invalid FG input disables unified preview');
  document.querySelector('[data-gp-action="preview"]').click(); await delay(30);
  assert(count('preview') === beforeInvalidFgPreview, 'invalid FG input cannot preview the previous valid frame target');
  set('fg', 'targetFps', '121'); click('preview'); await until(() => document.querySelector('.gp-modal'));
  assert(mock.plan.request.fg.backend === 'nvidia' && mock.plan.request.fg.mode === 'dynamic' && mock.plan.request.fg.targetFps === 121, 'corrected FG input previews its current value');
  click('modal-cancel'); click('discard');
  for (const route of ['vulkan', 'feeder']) {
    const packageId = 'fixture-' + route + '-fixed', api = route === 'vulkan' ? 'vulkan' : 'dx12';
    await scenario(route + ' 固定配套 · UI fixture', assessment => {
      assessment.game.installed = false; assessment.game.apiOverride = 'auto'; assessment.game.chosen.apiResolution.api = api;
      assessment.api.effectiveApi = api; assessment.game.nativeDlssAvailable = route !== 'feeder';
      assessment.game[route] = { installed: false, available: true, selectionAvailable: true, packageId, coreVersion: route + '-core' };
      assessment.layout.source = route; assessment.layout.mode = route === 'vulkan' ? 'external' : 'local';
      assessment.deployment.mode = assessment.layout.mode;
    });
    document.querySelector('[data-gp-tab="enhance"]').click();
    const core = document.querySelector('[data-gp-group="route"][data-gp-field="version"]');
    assert(core.disabled && core.value === packageId && core.options.length === 1, 'fixed route exposes only its verified package ID');
    assert(document.querySelector('[data-gp-group="route"][data-gp-field="deployment"]').disabled, 'a fixed route cannot select an unrelated deployment mode');
    const writes = count('apply'); click('prepare'); assert(count('apply') === writes, 'prepare stages the fixed route without applying');
    click('preview'); await until(() => document.querySelector('.gp-modal'));
    assert(mock.plan.request.route === route && mock.plan.request.api === api && mock.plan.request.version === packageId && mock.plan.request.loadingMode === undefined, 'fixed route preview cannot inherit an arbitrary native Core or Helper mode');
    click('modal-cancel'); click('discard');
  }
  await scenario('库条目移出 · UI fixture', assessment => { assessment.game.installed = false;
    assessment.enhancements.applied = { sr: { backend: 'native', request: { backend: 'native', quality: 'quality', preset: 'K' }, readbackVerified: true } }; });
  document.querySelector('[data-gp-tab="maintenance"]').click();
  assert(document.querySelector('[data-gp-action="remove-game"]').disabled, 'owned applied settings must be restored before removing a library entry');
  await scenario('库条目移出 · 无受管状态', assessment => { assessment.game.installed = false; assessment.enhancements.applied = {}; });
  document.querySelector('[data-gp-tab="maintenance"]').click();
  const beforeRemoveApply = count('apply'), beforeRemovePreview = count('preview');
  click('remove-game'); assert(document.querySelector('.gp-modal [data-gp-action="remove-confirm"]'), 'library removal has its own confirmation dialog');
  assert(!document.querySelector('.gp-modal [data-gp-action="modal-apply"]') && count('remove-game') === 0, 'opening removal cannot uninstall or immediately remove');
  click('modal-cancel'); assert(count('remove-game') === 0, 'cancel keeps the library entry');
  click('remove-game'); click('remove-confirm');
  await until(() => count('remove-game') === 1 && document.getElementById('view-games').classList.contains('active') && !document.querySelector('.open-game-page-btn'));
  assert(mock.calls.find(row => row[0] === 'remove-game')[1] === 'fixture', 'only the selected library ID is removed');
  assert(count('apply') === beforeRemoveApply && count('preview') === beforeRemovePreview && count('direct-uninstall') === 0, 'library removal does not apply or uninstall game files');
  // Restore the isolated mock card so the fixture's optional screenshot still
  // opens the requested detail tab after all removal assertions have completed.
  mock.removed = false; mock.assessment = structuredClone(mock.baseline); document.getElementById('refreshBtn').click();
  await until(() => document.querySelector('.open-game-page-btn')); document.querySelector('.open-game-page-btn').click(); await until(() => document.querySelector('.gp-tabs') && document.getElementById('view-game-detail').classList.contains('active'));
  assert(!mock.calls.some(row => row[0] === 'startup-failed'), 'renderer startup completed');
  return { writes: count('apply'), previews: count('preview'), confirmations: count('confirm'), elevated: count('elevated'), visualRecords: count('visual-record'), removals: count('remove-game'),
    assertions: 'drafts, numeric NR selects, Apply, feature confirmations, capability gray states, separate uninstall choices, recovery, same-scene visual provenance, modal focus, staged hotkeys, anti-cheat cleanup and deployment consent, invalid SR/FG draft rejection, fixed Vulkan/Feeder packages, confirmed metadata-only removal' };
}
const preload = path.join(temporary, 'preload.cjs'); fs.writeFileSync(preload, `(${installMock.toString()})();`);
app.whenReady().then(async () => {
  const win = new BrowserWindow({ width: Number(process.env.GAME_UI_WIDTH) || 1300, height: Number(process.env.GAME_UI_HEIGHT) || 1000, show: false, useContentSize: true,
    webPreferences: { preload, sandbox: true, contextIsolation: false, nodeIntegration: false, backgroundThrottling: false, offscreen: true } });
  try {
    await win.loadFile(path.resolve(__dirname, '../src/renderer/index.html'));
    const result = await win.webContents.executeJavaScript(`(${smoke.toString()})()`);
    if (process.env.GAME_UI_TAB) await win.webContents.executeJavaScript(`document.querySelector('[data-gp-tab="${process.env.GAME_UI_TAB}"]').click()`);
    await new Promise(resolve => setTimeout(resolve, 150));
    if (process.argv[2]) fs.writeFileSync(path.resolve(process.argv[2]), (await win.webContents.capturePage()).toPNG());
    console.log(JSON.stringify({ ok: true, sandbox: win.webContents.getLastWebPreferences().sandbox, ...result }, null, 2)); win.destroy(); app.exit(0);
  } catch (error) { console.error(error.stack || error); win.destroy(); app.exit(1); }
});
