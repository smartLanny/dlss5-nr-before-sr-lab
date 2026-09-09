'use strict';

function installMock(features, options = {}) {
  const { ipcRenderer } = require('electron');
  const clone = structuredClone, ok = value => ({ ok: true, value: clone(value) });
  const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  const game = { id: 'fixture', name: '博德之门 3 · 界面测试', dir: 'C:\\UI-fixture\\Baldurs Gate 3', installed: true, supported: true, launcher: 'Steam',
    addonVersion: '0.4.7beta', apiOverride: 'auto', nativeDlssAvailable: true, nativeFgAvailable: true,
    chosen: { path: 'C:\\UI-fixture\\Baldurs Gate 3\\bin\\bg3_dx11.exe', bitness: 64, apiResolution: { api: 'dx11', source: 'entry' } } };
  const hardware = { family: 'RTX40', series: ['RTX40'], names: ['NVIDIA RTX 4070 Ti SUPER'], source: 'fixture' };
  const assessment = { schema: 1, gameId: game.id, game, hardware,
    defaults: { api: 'auto', version: '0.4.7beta', deployment: 'external', loadingMode: 'proxy' },
    api: { capabilities: ['dx11'], configuredApi: null, effectiveApi: 'dx11', observedApi: null, presentationApi: null, confidence: 'high',
      bridgeStatus: { required: true, kind: 'DX11 桥接', verified: false }, evidence: [{ api: 'dx11', source: 'selected-executable', path: game.chosen.path, message: '已验证 BG3 DX11 独立入口。' }], conflicts: [], coverage: { complete: true, exeImports: true } },
    layout: { mode: 'local', loadingMode: 'proxy', activeConfigPath: 'C:\\UI-fixture\\bin\\ReShade.ini', runtimeDir: 'C:\\UI-fixture\\bin' },
    deployment: { mode: 'local', loadingMode: 'proxy', version: '0.4.7beta', verified: true, needsRecovery: false },
    enhancements: { hardware, featureStates: clone(features.on40), requests: {}, applied: {}, pending: [] },
    nr: { Enabled: 1, Intensity: 1, WorkMode: 0, CustomWorkScale: 1, Style: 0, AutoMask: 0, ColorStrength: .75, SkinStructureStrength: -1, TransferStrength: 1, PostTransferStrength: 1,
      capabilities: { WorkMode: true, CustomWorkScale: true, ColorStrength: true, SkinStructureStrength: true, TransferStrength: true, PostTransferStrength: true } },
    hotkeys: { nr: { label: 'F6 开关' }, reshade: { key: 36, ctrl: false, shift: false, alt: false } },
    operation: { pending: false }, launch: { selected: 'auto', effective: 'steam', steamAvailable: true, session: null },
    verification: { helper: { status: 'not-applicable', detail: '当前代理路线无需加载助手。' }, core: { status: 'unverified', detail: '等待本次游戏进程确认指定 Core。' },
      nr: { status: 'unverified', detail: '等待成功与提交计数持续增长。' }, visual: { status: 'unverified', detail: '需同场景开关对照与 F8 记录。' } },
    maintenance: { remainingFiles: [{ name: 'other-addon.addon64', kind: '其他模组，保留', sha256: 'c'.repeat(64) }], files: [], isolated: false, scope: '只检查所选 EXE 同目录图形代理与 Add-on。' },
    helperModules: { modules: [] }, components: { files: [], conflicts: [], warnings: [] }, antiCheat: { detected: false }, failures: [],
    coreVersions: [{ id: '0.4.7beta', label: 'beta0.4.7', ready: true },
      { id: '0.4.7beta-bg3-bridge1411', label: 'beta0.4.7 · BG3 桥接 1.4.11 对照', ready: true, comparisonOnly: true }] };
  const second = clone(assessment); second.gameId = second.game.id = 'fixture-two'; second.game.name = '第二款游戏 · DX12';
  second.game.dir = 'C:\\UI-fixture\\Second'; second.game.chosen.path = 'C:\\UI-fixture\\Second\\Game.exe';
  second.game.chosen.apiResolution.api = second.api.effectiveApi = 'dx12'; second.api.capabilities = ['dx12']; second.nr.Intensity = .9;
  const mock = window.__gpMock = { calls: [], plans: new Map(), assessments: { fixture: assessment, 'fixture-two': second },
    baseline: clone(assessment), secondBaseline: clone(second), features, removed: new Set(), delays: options.captureOnly ? {} : { 'fixture:installation': 5000 }, pending: 0,
    failApply: false, listeners: new Set(), mounts: new Map(), plan: null };
  Object.defineProperty(mock, 'assessment', { get: () => mock.assessments.fixture, set: value => { mock.assessments.fixture = value; } });
  const sectionFields = {
    installation: ['game', 'api', 'defaults', 'layout', 'deployment', 'nr', 'hotkeys', 'operation', 'launch', 'antiCheat', 'coreVersions', 'hardware'],
    enhancements: ['enhancements'], diagnostics: ['maintenance', 'verification', 'helperModules', 'components', 'layout', 'deployment']
  };
  let ui, page;
  Object.defineProperty(window, 'launchSettingsUi', { configurable: true, get: () => ui, set: value => {
    ui = value; value.mount = () => { mock.calls.push(['legacy-mount']); throw Error('production inline path must not mount the legacy writer'); };
  } });
  Object.defineProperty(window, 'GamePageUi', { configurable: true, get: () => page, set: value => {
    page = value; const mount = value.mount;
    value.mount = (host, manager, options) => { const controller = mount(host, manager, options); host.__gpController = controller;
      const id = host.dataset.gameDetail; mock.mounts.set(id, (mock.mounts.get(id) || 0) + 1); return controller; };
  } });
  const payload = { ready: true, selectedVersion: '0.4.7beta', versions: { '0.4.7beta': { label: 'beta0.4.7', variants: { RTX40: { ready: true, files: [] } } } }, source: { mode: 'bundled', path: 'C:\\UI-fixture\\payload', ready: true } };
  const games = () => Object.values(mock.assessments).filter(row => !mock.removed.has(row.gameId)).map(row => row.game);
  const empty = async () => ok(null);
  const forbidden = name => async (...args) => { mock.calls.push([name, ...args]); throw Error('unexpected direct mutation: ' + name); };
  window.manager = {
    boot: async () => ok({ product: { name: 'DLSS 5 AI 超分管理器', edition: '装机宅版', version: '0.4.8-beta.2 · UI fixture' }, settings: {}, hardware, payload, addons: assessment.coreVersions, games: games() }),
    listGames: async () => ok(games()), refresh: async () => { mock.calls.push(['refresh']); return ok(games()); },
    assessGame: async (id, options) => {
      const sections = options?.sections;
      if (!Array.isArray(sections) || sections.length !== 1 || !sectionFields[sections[0]]) throw Error('UI must request exactly one known assessment section');
      const section = sections[0], ticket = mock.calls.filter(row => row[0] === 'assess').length + 1;
      const snapshot = { schema: 1, gameId: id, sections: [section] };
      for (const key of sectionFields[section]) snapshot[key] = clone(mock.assessments[id][key]);
      mock.calls.push(['assess', id, section, ticket]); mock.pending++;
      try { await delay(mock.delays[id + ':' + section] || 0); mock.calls.push(['assess-resolved', id, section, ticket]); return ok(snapshot); }
      finally { mock.pending--; }
    },
    readPayloadSource: async () => ok({ settings: {}, payload, addons: assessment.coreVersions }), getStartupContext: async () => ok({ mode: 'normal', sandbox: true, privilege: 'standard', operation: {} }),
    getGameIcon: empty, fetchGameArt: empty, listAddons: async () => ok([]), onAddonImported() {}, onSrModelApplied() {}, onLaunchSettingsApplied() {},
    startupReady() {}, startupFailed: message => { mock.calls.push(['startup-failed', message]); }, minimize() {}, close() {}, diagnose: async () => ok({ components: [], checks: [], issues: [] }),
    onLaunchSession: callback => { mock.listeners.add(callback); return () => mock.listeners.delete(callback); },
    previewOperation: async (id, request) => {
      mock.calls.push(['preview', clone(request), id]);
      const nativeOperations = request.sr ? await ipcRenderer.invoke('game-page-fixture-sr-plan', request.sr, mock.assessments[id].hardware) : [];
      const plan = { request: clone(request), planId: 'fixture-plan-' + mock.calls.length, fingerprint: 'd'.repeat(64), blockers: [], nativeOperations,
        changes: request.uninstall ? [{ action: request.uninstall, name: 'nr-before-sr.zh-CN.addon64', path: mock.assessments[id].game.dir, beforeSha256: 'a'.repeat(64), afterSha256: null }]
          : [...nativeOperations.map(row => ({ ...row, domain: 'sr' })), ...Object.entries(request.nr || {}).map(([key, value]) => ({ action: 'set-config-key', key, value, path: 'nr_before_sr.ini' }))] };
      mock.plan = plan; mock.plans.set(plan.planId, plan); return ok(plan);
    },
    applyOperation: async (id, planId, consent) => {
      mock.calls.push(['apply', clone(consent), id]); const value = mock.assessments[id], plan = mock.plans.get(planId);
      if (mock.failApply) { value.operation = { pending: true, record: { error: { message: '测试文件被占用' }, stages: [{ kind: 'deployment', status: 'complete' }] } }; return { ok: false, error: { message: '测试文件被占用；请恢复未完成操作。' } }; }
      if (Object.hasOwn(plan.request, 'api')) {
        const detected = value.api.detectedApi || value.game.chosen.detectedApiResolution?.api ||
          (value.game.chosen.apiResolution?.source !== 'override' ? value.game.chosen.apiResolution?.api : null) || 'unknown';
        value.game.apiOverride = value.defaults.api = plan.request.api;
        value.api.configuredApi = plan.request.api === 'auto' ? null : plan.request.api;
        value.api.detectedApi = detected;
        value.api.effectiveApi = plan.request.api === 'auto' ? detected : plan.request.api;
        value.game.chosen.apiResolution = plan.request.api === 'auto'
          ? clone(value.game.chosen.detectedApiResolution || { api: detected, source: 'entry' }) : { api: plan.request.api, source: 'override' };
      }
      if (plan.request.nr) Object.assign(value.nr, plan.request.nr);
      if (plan.request.hotkeys) Object.assign(value.hotkeys, clone(plan.request.hotkeys));
      for (const domain of ['sr', 'fg']) if (plan.request[domain]) {
        value.enhancements.requests[domain] = { request: clone(plan.request[domain]) };
        value.enhancements.applied[domain] = { request: clone(plan.request[domain]), readbackVerified: true };
      }
      if (plan.request.uninstall) value.game.installed = false; return ok({ applied: true, notice: '测试配置已应用。' });
    },
    applyOperationElevated: async () => { mock.calls.push(['elevated']); return ok({ applied: true }); },
    previewEnvironmentCleanup: async id => { mock.calls.push(['cleanup-preview', id]); return ok({ planId: 'fixture-cleanup-plan', scope: '只隔离已勾选文件。',
      candidates: [{ name: 'other-addon.addon64', kind: '其他 Add-on', note: '明确选择后备份隔离。', sha256: 'c'.repeat(64), selectable: true, selectedByDefault: true }] }); },
    applyEnvironmentCleanup: async (id, planId, names) => { mock.calls.push(['cleanup-apply', id, planId, clone(names)]);
      mock.assessments[id].maintenance.isolated = true; mock.assessments[id].maintenance.canRestore = true; return ok({ isolated: true }); },
    confirmGameFeature: async (id, domain, input) => {
      mock.calls.push(['confirm', domain, id, clone(input)]);
      const result = clone(domain === 'sr' ? features.confirmedSr : features.on40.fg);
      mock.assessments[id].enhancements.featureStates[domain] = result; return ok(result);
    },
    recoverOperation: async id => { mock.calls.push(['recover', id]); mock.assessments[id].operation = { pending: false }; return ok({ recovered: true }); },
    recordVisualComparison: async (id, input) => { mock.calls.push(['visual-record', id, clone(input)]);
      mock.assessments[id].verification.visual = { status: input.result === 'changed' ? 'passed' : input.result === 'unchanged' ? 'not-observed' : 'unverified', source: 'user-comparison',
        automaticVerification: false, detail: input.result === 'changed' ? '用户观察：同场景发现画面变化。' : input.result === 'unchanged' ? '用户观察：未发现画面变化。' : '用户观察：暂时无法确认。' };
      return ok({ recorded: true, notice: '已保存本次用户观察。' }); },
    removeGame: async id => { mock.calls.push(['remove-game', id]); mock.removed.add(id); return ok({ removed: true }); },
    writeGameHotkey: forbidden('direct-hotkey-write'), uninstall: forbidden('direct-uninstall'), updateLaunchSettings: forbidden('direct-launch-settings-write'),
    saveParameters: forbidden('direct-parameter-write'), writeConfig: forbidden('direct-config-write'),
    exportFeedback: async () => ok('fixture.txt'), openFolder: empty, openExternal: async key => { mock.calls.push(['external', key]); return ok(true); },
    launch: async id => { mock.calls.push(['launch', id]); return ok({ launched: { gameId: id, status: 'waiting-enhancement' } }); }, cancelLaunch: async id => { mock.calls.push(['cancel-launch', id]); return ok({ cancelled: true }); }
  };
}

async function smoke() {
  const mock = window.__gpMock, delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  let assertionCount = 0;
  const assert = (value, message) => { assertionCount++; if (!value) throw Error(message); };
  const until = async (predicate, label, timeout = 6500) => { const start = performance.now(); while (performance.now() - start < timeout) { if (predicate()) return; await delay(20); } throw Error('UI timeout: ' + label); };
  const card = (id = 'fixture') => document.querySelector(`.game-card[data-id="${id}"]`);
  const host = () => document.querySelector('.game-card.expanded .game-detail.gp-inline');
  const state = () => host()?.__gpController.getState();
  const field = (group, key) => host()?.querySelector(`[data-gp-group="${group}"][data-gp-field="${key}"]`);
  const button = action => host()?.querySelector(`[data-gp-action="${action}"]`);
  const count = kind => mock.calls.filter(row => row[0] === kind).length;
  const click = action => { const item = button(action); assert(item && !item.disabled, 'action unavailable: ' + action);
    const closed = []; for (let parent = item.parentElement; parent && parent !== host(); parent = parent.parentElement) if (parent.tagName === 'DETAILS' && !parent.open) closed.push(parent);
    for (const details of closed.reverse()) details.querySelector(':scope > summary').click();
    assert(item.getClientRects().length > 0, 'action is visible after opening its details: ' + action); item.click(); };
  const set = (group, key, value) => { const input = field(group, key); assert(input && !input.disabled, 'field unavailable: ' + group + '.' + key);
    if (input.type === 'checkbox') input.checked = Boolean(value); else input.value = value;
    input.dispatchEvent(new Event(input.type === 'range' || input.type === 'number' ? 'input' : 'change', { bubbles: true })); };
  const tab = async key => { const item = host()?.querySelector(`[data-gp-tab="${key}"]`); assert(item, 'tab exists: ' + key); item.click();
    await until(() => host()?.querySelector(`[data-gp-tab="${key}"]`)?.getAttribute('aria-selected') === 'true', 'tab ' + key); };
  const open = async id => { const item = card(id)?.querySelector('.open-game-page-btn'); assert(item, 'card can open: ' + id); item.click();
    await until(() => host()?.dataset.gameDetail === id, 'inline card ' + id, 1000); };
  const fold = async selector => { await until(() => host()?.querySelector(selector), 'details ' + selector); const details = host().querySelector(selector);
    if (!details.open) details.querySelector('summary').click(); assert(details.open, 'details can open: ' + selector); };
  const maintenance = async () => { await tab('maintenance'); await until(() => state().loaded.includes('diagnostics') && state().loaded.includes('enhancements'), 'advanced sections'); await fold('.gp-maintenance-details'); };
  const diagnostics = async () => { await tab('maintenance'); await until(() => state().loaded.includes('diagnostics') && state().loaded.includes('enhancements'), 'diagnostic sections'); await fold('.gp-diagnostics-details'); };
  const settled = async () => until(() => !state()?.busy && !host()?.querySelector('.gp-modal'), 'operation settled');
  const preview = async (action = 'preview') => { click(action); await until(() => host()?.querySelector('.gp-modal [data-gp-action="modal-apply"]'), 'preview dialog'); };
  const discard = () => { if (button('discard')) click('discard'); };
  const scenario = async (label, change) => {
    discard(); mock.assessment = structuredClone(mock.baseline); mock.assessment.game.name = label; change?.(mock.assessment);
    await maintenance(); click('refresh'); await until(() => state().data.game.name === label && mock.pending === 0, 'fresh scenario ' + label); await settled();
  };
  await until(() => card()?.querySelector('.open-game-page-btn') && card('fixture-two'), 'library cards');
  const openAt = performance.now(); await open('fixture');
  assert(performance.now() - openAt < 500, 'five-second installation check must not delay inline expansion');
  assert(document.getElementById('view-games').classList.contains('active'), 'library remains active');
  assert(!document.getElementById('view-game-detail').classList.contains('active'), 'independent detail view is never activated');
  assert(host().querySelectorAll('[data-gp-tab]').length === 3, 'three tabs appear from the seed');
  assert(host().querySelector('[data-detail-tab="enhance"]') && host().querySelector('[data-detail-tab="graphics"]') && host().querySelector('[data-detail-tab="advanced"]'), 'existing accessible tab identities are reused');
  assert(!state().loaded.includes('installation'), 'seed appears while installation is unresolved');
  await until(() => state().loaded.includes('installation'), 'five-second installation response'); mock.delays['fixture:installation'] = 0;
  assert(!mock.calls.some(row => row[0] === 'assess' && row[2] !== 'installation'), 'first tab does not start expensive enhancement or diagnostic work');
  assert(field('nr', 'Intensity') && field('nr', 'Style') && !field('nr', 'WorkMode'), 'first tab shows only basic NR fields');
  assert(![...field('route', 'version').options].some(row => row.value.includes('bridge1411')), 'bridge comparison is absent from basic Core choices');
  const firstHost = host(), firstController = firstHost.__gpController;
  set('nr', 'Intensity', '.65'); mock.delays['fixture:diagnostics'] = 5000;
  await tab('maintenance'); set('nr', 'WorkMode', '5');
  await until(() => mock.calls.some(row => row[0] === 'assess' && row[2] === 'diagnostics'), 'diagnostic request started');
  const diagnosticTicket = mock.calls.filter(row => row[0] === 'assess' && row[2] === 'diagnostics').at(-1)[3];
  const backAt = performance.now(); await tab('overview');
  assert(performance.now() - backAt < 500 && field('nr', 'Intensity').value === '0.65', 'slow diagnostics never block the basic tab or erase its draft');
  mock.delays['fixture-two:installation'] = 30; await open('fixture-two');
  await until(() => state().loaded.includes('installation'), 'second game installs first');
  set('nr', 'Intensity', '.8');
  await until(() => mock.calls.some(row => row[0] === 'assess-resolved' && row[3] === diagnosticTicket), 'older first-game diagnostic response');
  assert(state().id === 'fixture-two' && state().data.api.effectiveApi === 'dx12' && field('nr', 'Intensity').value === '0.8', 'late first-game response cannot replace the second game or its draft');
  await open('fixture');
  assert(host() === firstHost && host().__gpController === firstController && mock.mounts.get('fixture') === 1, 'same host and controller survive cross-game expansion');
  assert(field('nr', 'Intensity').value === '0.65', 'first-game draft survives returning from another game');
  await tab('maintenance'); assert(field('nr', 'WorkMode').value === '5', 'advanced draft survives delayed diagnostics');
  click('back'); assert(!host() && !document.querySelector('.gp-modal'), 'collapse keeps drafts without a leave confirmation');
  await open('fixture'); assert(state().draft.nr.Intensity === .65 && state().draft.nr.WorkMode === 5, 'collapsed draft is retained');
  mock.delays['fixture:diagnostics'] = 0; discard(); await tab('enhance');
  await until(() => state().loaded.includes('enhancements'), 'SR/FG lazy section');
  assert(field('sr', 'preset').value === 'M' && field('sr', 'quality').value === 'preserve', 'RTX40 starts with explicit M and preserves the game quality');
  assert(!field('sr', 'renderPercent') && !field('fg', 'multiplier') && !field('fg', 'targetFps'), 'dormant ratio, multiplier and dynamic fields are hidden');
  await preview('preview-sr');
  assert(JSON.stringify(mock.plan.request) === JSON.stringify({ sr: { backend: 'native', quality: 'preserve', preset: 'M' } }), 'default SR can preview without changing any selector');
  assert(mock.plan.nativeOperations.length === 2 && mock.plan.nativeOperations.some(row => row.value === 13), 'production SR compiler emits two operations including model M');
  click('modal-cancel'); discard();
  set('sr', 'preset', 'L'); const recommendationWrites = count('apply'); click('recommend-sr');
  assert(field('sr', 'preset').value === 'M' && field('sr', 'quality').value === 'preserve' && count('apply') === recommendationWrites, 'Restore recommendation only updates the explicit draft');
  discard(); await tab('overview'); set('nr', 'Intensity', '.65'); await tab('maintenance'); set('nr', 'WorkMode', '5'); await tab('enhance');
  set('sr', 'quality', 'performance'); set('fg', 'mode', 'fixed'); set('fg', 'multiplier', '3');
  const passiveWrites = count('apply'), passivePreviews = count('preview'); await delay(800);
  assert(count('apply') === passiveWrites && count('preview') === passivePreviews && count('legacy-mount') === 0, 'control changes never mount the old writer or auto-apply');
  assert(field('fg', 'mode').querySelector('option[value="dynamic"]').disabled, 'RTX40 Dynamic stays disabled');
  assert(field('fg', 'multiplier').querySelector('option[value="6"]').disabled, 'unverified multiplier stays disabled');
  assert(button('launch').disabled, 'dirty draft blocks launch'); await preview();
  assert(mock.plan.request.nr.Intensity === .65 && mock.plan.request.nr.WorkMode === 5 && mock.plan.request.sr.preset === 'M' && mock.plan.request.sr.quality === 'performance' && mock.plan.request.fg.multiplier === 3, 'one preview includes numeric NR, explicit SR and FG');
  click('modal-apply'); await settled(); assert(!button('launch').disabled && count('elevated') === 0, 'successful ordinary Apply clears draft without elevation');

  await scenario('跨页签 SR 应用 · UI fixture', value => {
    const request = { backend: 'native', quality: 'preserve', preset: 'M' };
    value.enhancements.requests.sr = { request: structuredClone(request) };
    value.enhancements.applied.sr = { request: structuredClone(request), readbackVerified: true };
  });
  await tab('enhance'); assert(field('sr', 'preset').value === 'M', 'cross-tab SR starts from persisted M');
  set('sr', 'preset', 'K'); await tab('overview'); await preview();
  assert(mock.plan.request.sr.preset === 'K', 'overview unified preview includes the SR tab draft');
  const beforeCrossTabEnhancements = mock.calls.filter(row => row[0] === 'assess' && row[2] === 'enhancements').length;
  click('modal-apply'); await settled();
  assert(mock.assessment.enhancements.requests.sr.request.preset === 'K' && mock.assessment.enhancements.applied.sr.request.preset === 'K', 'cross-tab Apply persists both requested and applied SR K');
  assert(mock.calls.filter(row => row[0] === 'assess' && row[2] === 'enhancements').length === beforeCrossTabEnhancements, 'overview Apply leaves enhancement reading lazy');
  await tab('enhance'); await until(() => state().loaded.includes('enhancements'), 'enhancement reload after overview Apply');
  assert(mock.calls.filter(row => row[0] === 'assess' && row[2] === 'enhancements').length === beforeCrossTabEnhancements + 1 && field('sr', 'preset').value === 'K', 'returning to SR reads and displays persisted K');
  await preview('preview-sr'); assert(mock.plan.request.sr.preset === 'K', 'subsequent SR preview cannot revert the applied preset to M');
  click('modal-apply'); await settled();
  assert(mock.assessment.enhancements.applied.sr.request.preset === 'K' && field('sr', 'preset').value === 'K', 'reapplying displayed SR retains K in storage and UI');

  await scenario('真实确认状态 · UI fixture', value => { value.enhancements.featureStates.sr = structuredClone(mock.features.waitingSr); }); await tab('enhance');
  assert(field('sr', 'quality').disabled && button('confirm-sr'), 'production waiting state blocks editing and exposes confirmation');
  mock.delays['fixture:enhancements'] = 5000; click('refresh');
  await until(() => mock.pending > 0, 'old waiting response in flight'); const confirmationAssesses = count('assess'), confirmationApply = count('apply');
  const confirmationAt = performance.now(); click('confirm-sr');
  await until(() => !button('confirm-sr') && !field('sr', 'quality').disabled, 'confirmation response immediately enables controls', 800);
  assert(performance.now() - confirmationAt < 800 && mock.pending > 0, 'confirmation updates before the slow assessment finishes');
  assert(state().data.enhancements.featureStates.sr.canConfirm === false && host().querySelector('.gp-confirmed'), 'confirmed production state is displayed without a repeated question');
  assert(count('apply') === confirmationApply && count('assess') === confirmationAssesses, 'confirmation neither applies settings nor triggers a blocking full reassessment');
  await until(() => mock.pending === 0, 'stale waiting response drained');
  assert(!button('confirm-sr') && !field('sr', 'quality').disabled, 'late pre-confirmation response cannot revive the prompt'); mock.delays['fixture:enhancements'] = 0;

  await maintenance(); await preview('uninstall-clean'); assert(mock.plan.request.uninstall === 'clean', 'clean uninstall is selected explicitly'); click('modal-cancel');
  await preview('uninstall-restore'); assert(mock.plan.request.uninstall === 'restore', 'restore mode is freshly selected'); click('modal-cancel');
  await tab('overview'); set('nr', 'Intensity', '.7'); await preview(); mock.failApply = true; click('modal-apply'); await settled();
  await maintenance(); assert(button('recover-operation'), 'partial apply failure exposes recovery'); click('recover-operation');
  await until(() => count('recover') === 1 && !state().busy && !state().data.operation.pending, 'recover operation'); discard(); mock.failApply = false;
  const beforeLaunchApply = count('apply'); click('launch');
  await until(() => count('launch') === 1 && !state().launching, 'explicit launch');
  assert(mock.calls.find(row => row[0] === 'launch')[1] === 'fixture' && count('apply') === beforeLaunchApply, 'launch targets the current game without applying settings');

  const session = { sessionId: 'b4aa3424-4828-40cd-9b5b-a49e566c5130', gameId: 'fixture', targetExe: mock.baseline.game.chosen.path,
    requestedAt: new Date().toISOString(), status: 'waiting-enhancement', process: { pid: 24001, exe: mock.baseline.game.chosen.path, startedAt: new Date().toISOString() } };
  await scenario('本次画面对照 · UI fixture', value => { value.launch.session = { ...session, historical: true }; }); await diagnostics();
  assert(host().querySelectorAll('.gp-verification article').length === 4 && button('record-visual').disabled, 'four independent layers reject historical visual recording');
  mock.assessment.launch.session = structuredClone(session); click('refresh'); await until(() => !button('record-visual')?.disabled, 'current visual recording'); await diagnostics();
  const originalLayers = JSON.stringify(Object.fromEntries(['helper', 'core', 'nr'].map(key => [key, mock.assessment.verification[key]])));
  const beforeVisualApply = count('apply'), beforeVisualPreview = count('preview');
  for (const result of ['changed', 'unchanged', 'uncertain']) {
    await diagnostics(); const cards = [...host().querySelectorAll('.gp-verification article')].slice(0, 3).map(row => row.textContent), prior = count('visual-record'); click('record-visual');
    const modal = host().querySelector('.gp-modal'); assert(modal.getAttribute('role') === 'dialog' && modal.querySelector('h3').textContent.includes('画面对照'), 'dedicated visual dialog');
    for (const input of modal.querySelectorAll('input,select,textarea')) assert(input.labels.length > 0, 'visual inputs have labels');
    click('save-visual'); await delay(30); assert(count('visual-record') === prior && modal.querySelector('.gp-modal-message').textContent.includes('同场景'), 'same-scene confirmation is mandatory');
    modal.querySelector('[data-gp-same-scene]').checked = true; modal.querySelector('[data-gp-visual-result]').value = result;
    const note = '<script>plain user note</script> 同场景 ' + result, evidenceLabel = 'C:\\UI-fixture\\evidence label only.png';
    modal.querySelector('[data-gp-visual-note]').value = note; modal.querySelector('[data-gp-visual-evidence]').value = evidenceLabel;
    const controls = [...modal.querySelectorAll('input:not([disabled]),select:not([disabled]),textarea:not([disabled]),button:not([disabled])')], first = controls[0], last = controls.at(-1);
    last.focus(); last.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true, cancelable: true })); assert(document.activeElement === first, 'Tab is trapped inside modal');
    first.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', shiftKey: true, bubbles: true, cancelable: true })); assert(document.activeElement === last, 'Shift+Tab wraps inside modal');
    click('save-visual'); await settled(); await diagnostics();
    const call = mock.calls.filter(row => row[0] === 'visual-record').at(-1);
    assert(call[1] === 'fixture' && JSON.stringify(call[2]) === JSON.stringify({ sessionId: session.sessionId, sameScene: true, result, note, evidenceLabel }), 'visual request preserves session and plain-text evidence');
    assert(JSON.stringify(Object.fromEntries(['helper', 'core', 'nr'].map(key => [key, mock.assessment.verification[key]]))) === originalLayers, 'visual recording does not promote helper/Core/NR');
    assert([...host().querySelectorAll('.gp-verification article')].slice(0, 3).every((row, index) => row.textContent === cards[index]), 'only visual card changes');
    assert(host().querySelectorAll('.gp-verification article')[3].textContent.includes(result === 'changed' ? '已确认' : result === 'unchanged' ? '未观察到变化' : '待确认'), 'visual status follows observation');
  }
  assert(count('apply') === beforeVisualApply && count('preview') === beforeVisualPreview, 'visual observations never submit deployment or driver operations');
  await scenario('快捷键暂存 · UI fixture'); await tab('maintenance');
  const shortcutDetails = [...host().querySelectorAll('details')].find(row => row.querySelector('summary')?.textContent.includes('快捷键')); shortcutDetails.querySelector('summary').click();
  const beforeHotkeyApply = count('apply'); click('panel-insert'); await delay(100);
  assert(mock.assessment.hotkeys.reshade.key === 36 && count('direct-hotkey-write') === 0 && count('apply') === beforeHotkeyApply, 'shortcut only changes the draft');
  await preview(); assert(JSON.stringify(mock.plan.request) === JSON.stringify({ hotkeys: { reshade: { key: 45, ctrl: false, shift: false, alt: false } } }), 'Insert binding travels through unified preview');
  click('modal-cancel'); assert(mock.assessment.hotkeys.reshade.key === 36, 'cancel retains Home'); await preview(); click('modal-apply'); await settled();
  assert(mock.assessment.hotkeys.reshade.key === 45, 'unified Apply owns shortcut mutation');

  await scenario('反作弊游戏 · 卸载后清理', value => { value.game.installed = false; value.antiCheat.detected = true; }); await maintenance();
  const cleanupOperations = count('apply'); click('clean-environment'); await until(() => host().querySelector('[data-gp-clean]'), 'cleanup preview');
  assert(!host().querySelector('.gp-modal [data-gp-consent]'), 'cleanup does not contain an unrelated deployment consent'); click('modal-apply'); await settled();
  assert(JSON.stringify(mock.calls.find(row => row[0] === 'cleanup-apply').slice(1)) === JSON.stringify(['fixture', 'fixture-cleanup-plan', ['other-addon.addon64']]), 'cleanup sends reviewed IDs and selected filenames');
  assert(count('apply') === cleanupOperations, 'cleanup uses its own owner');
  await scenario('反作弊游戏 · 普通应用确认', value => { value.antiCheat.detected = true; }); await tab('overview'); set('nr', 'Intensity', '.6'); await preview();
  const consent = host().querySelector('[data-gp-consent]'), beforeConsentApply = count('apply'); assert(consent && consent.getClientRects().length > 0 && !consent.checked, 'normal Apply shows visible unchecked consent');
  click('modal-apply'); await delay(30); assert(count('apply') === beforeConsentApply && host().querySelector('.gp-modal-message').textContent.includes('反作弊'), 'normal Apply still requires its visible consent'); click('modal-cancel'); discard();

  await scenario('SR 自定义比例 · 无效输入'); await tab('enhance'); set('sr', 'quality', 'custom'); set('sr', 'renderPercent', '75');
  const invalidSrPreviews = count('preview'); set('sr', 'renderPercent', '76.5');
  assert(host().querySelector('.gp-message.error[role="alert"]')?.textContent.includes('整数') && button('preview').disabled, 'invalid SR value immediately blocks preview');
  button('preview').click(); await delay(30); assert(count('preview') === invalidSrPreviews, 'invalid SR cannot reuse previous valid input');
  set('sr', 'renderPercent', '76'); await preview(); assert(mock.plan.request.sr.renderPercent === 76, 'corrected SR previews current value'); click('modal-cancel'); discard();
  await scenario('FG 动态目标 · 无效输入', value => { value.hardware = value.enhancements.hardware = { family: 'RTX50', series: ['RTX50'], names: ['NVIDIA RTX 5090'], source: 'fixture' }; value.enhancements.featureStates = structuredClone(mock.features.on50); });
  await tab('enhance'); set('fg', 'mode', 'dynamic'); assert(field('fg', 'targetFps') && !field('fg', 'multiplier'), 'dynamic shows only its target'); set('fg', 'targetFps', '120');
  const invalidFgPreviews = count('preview'); set('fg', 'targetFps', '120.5');
  assert(host().querySelector('.gp-message.error[role="alert"]')?.textContent.includes('整数') && button('preview').disabled, 'invalid FG value immediately blocks preview');
  button('preview').click(); await delay(30); assert(count('preview') === invalidFgPreviews, 'invalid FG cannot reuse previous target');
  set('fg', 'targetFps', '121'); await preview(); assert(mock.plan.request.fg.backend === 'nvidia' && mock.plan.request.fg.targetFps === 121, 'corrected FG retains backend and current target'); click('modal-cancel'); discard();

  await scenario('API 未确定 · 需要手选', value => { value.game.installed = false; value.api.effectiveApi = 'mixed'; value.api.capabilities = ['dx11', 'dx12']; value.game.chosen.apiResolution.api = 'mixed'; }); await tab('overview');
  assert(field('route', 'api').selectedOptions[0].textContent.includes('需要手动选择') && button('prepare').disabled, 'unresolved automatic API requests manual selection and blocks installation');
  set('route', 'api', 'dx12'); await preview(); assert(mock.plan.request.api === 'dx12', 'manual API reaches unified preview'); click('modal-cancel'); discard();
  await scenario('手动 API 恢复自动 · UI fixture', value => {
    value.game.apiOverride = value.defaults.api = value.api.configuredApi = value.api.effectiveApi = 'dx11';
    value.api.detectedApi = 'dx12'; value.api.capabilities = ['dx11', 'dx12'];
    value.game.chosen.apiResolution = { api: 'dx11', source: 'override' };
    value.game.chosen.detectedApiResolution = { api: 'dx12', source: 'entry' };
  });
  await tab('overview'); assert(field('route', 'api').value === 'dx11' && state().data.api.detectedApi === 'dx12', 'manual API preference is separate from independently detected DX12');
  set('route', 'api', 'auto'); await preview();
  assert(mock.plan.request.api === 'auto', 'returning to automatic API preserves auto in the unified request');
  click('modal-apply'); await settled();
  assert(mock.assessment.game.apiOverride === 'auto' && mock.assessment.defaults.api === 'auto' && mock.assessment.api.configuredApi === null, 'Apply persists automatic preference instead of a resolved manual API');
  assert(field('route', 'api').value === 'auto' && state().data.api.detectedApi === 'dx12' && state().data.api.effectiveApi === 'dx12', 'post-Apply refresh keeps automatic selection while using detected DX12');
  const beforeAutomaticRefresh = count('assess'); click('refresh');
  await until(() => count('assess') > beforeAutomaticRefresh && mock.pending === 0 && state().loaded.includes('installation'), 'explicit automatic API refresh');
  assert(field('route', 'api').value === 'auto' && state().data.api.effectiveApi === 'dx12', 'explicit recheck does not convert automatic API back to manual');
  await scenario('外置默认安装 · UI fixture', value => { value.game.installed = false; }); await tab('overview'); await preview('prepare');
  assert(mock.plan.request.api === 'auto' && mock.plan.request.version === '0.4.7beta' && mock.plan.request.deployment === 'external' && mock.plan.request.loadingMode === 'proxy', 'prepare previews the visible install choice and preserves automatic API preference'); click('modal-cancel'); discard();
  await tab('maintenance'); set('bridge', 'version', '0.4.7beta-bg3-bridge1411'); await tab('overview');
  assert(field('route', 'version').value === '0.4.7beta', 'advanced bridge choice does not appear as a second Core'); await preview();
  assert(mock.plan.request.version === '0.4.7beta-bg3-bridge1411', 'advanced bridge selection preserves its exact paired identity'); click('modal-cancel'); discard();
  for (const route of ['vulkan', 'feeder']) {
    const packageId = 'fixture-' + route + '-fixed', api = route === 'vulkan' ? 'vulkan' : 'dx12';
    await scenario(route + ' 固定配套', value => { value.game.installed = false; value.game.chosen.apiResolution.api = value.api.effectiveApi = api; value.game.nativeDlssAvailable = route !== 'feeder';
      value.game[route] = { installed: false, available: true, selectionAvailable: true, packageId, coreVersion: route + '-core' };
      value.layout.source = route; value.layout.mode = value.deployment.mode = route === 'vulkan' ? 'external' : 'local'; });
    await tab('overview'); assert(field('route', 'version').disabled && field('route', 'version').value === packageId && field('route', 'version').options.length === 1, 'fixed route exposes only its package');
    await preview('prepare'); assert(mock.plan.request.route === route && mock.plan.request.api === 'auto' && mock.plan.request.version === packageId && mock.plan.request.loadingMode === undefined, 'fixed route follows automatic detection without inheriting arbitrary Core or helper'); click('modal-cancel'); discard();
    await tab('maintenance'); assert(field('route', 'deployment').disabled && field('route', 'loadingMode').disabled, 'fixed route blocks unrelated layout and helper choices');
  }
  await scenario('库条目移出 · 已有覆盖', value => { value.game.installed = false; value.enhancements.applied.sr = { request: { backend: 'native', quality: 'quality', preset: 'K' }, readbackVerified: true }; }); await maintenance();
  assert(button('remove-game').disabled, 'owned settings block library removal');
  await scenario('库条目移出 · 无受管状态', value => { value.game.installed = false; }); await maintenance();
  const beforeRemoveApply = count('apply'), beforeRemovePreview = count('preview'); click('remove-game');
  assert(button('remove-confirm') && !button('modal-apply') && count('remove-game') === 0, 'library removal has a separate metadata-only dialog'); click('modal-cancel'); assert(count('remove-game') === 0, 'cancel retains entry');
  click('remove-game'); click('remove-confirm'); await until(() => !card('fixture') && count('remove-game') === 1, 'library removal');
  assert(card('fixture-two') && count('apply') === beforeRemoveApply && count('preview') === beforeRemovePreview && count('direct-uninstall') === 0, 'only selected metadata entry is removed');
  mock.removed.clear(); mock.assessment = structuredClone(mock.baseline); document.getElementById('refreshBtn').click(); await until(() => card('fixture'), 'restore screenshot fixture'); await open('fixture');
  await until(() => state().loaded.includes('installation'), 'final screenshot installation');
  const finalToast = document.getElementById('toast');
  assert(!finalToast.classList.contains('error'), 'successful library refresh must not report an error toast');
  if (finalToast.classList.contains('show')) {
    assert(finalToast.textContent === '扫描完成', 'only the known successful refresh toast is cleared for screenshot');
    finalToast.classList.remove('show');
  }
  assert(!mock.calls.some(row => row[0] === 'startup-failed' || row[0] === 'legacy-mount' || row[0].startsWith('direct-')), 'no legacy controller, direct writers or startup failure ran');
  assert(document.getElementById('view-games').classList.contains('active') && !document.getElementById('view-game-detail').classList.contains('active'), 'all flows remain inside the library');
  return { assertionCount, writes: count('apply'), previews: count('preview'), confirmations: count('confirm'), visualRecords: count('visual-record'), removals: count('remove-game'),
    assertions: 'inline seed, 5s lazy sections, reverse cross-game responses, controller reuse, preserved drafts, explicit SR defaults, cross-tab SR Apply and readback, automatic API persistence, production confirmation state and SR compiler, unified Apply, recovery, modal focus, visual provenance, hotkeys, cleanup consent, invalid inputs, fixed routes and metadata-only removal' };
}

async function captureBaseline({ tab = 'overview' } = {}) {
  const mock = window.__gpMock, delay = ms => new Promise(resolve => setTimeout(resolve, ms));
  let assertionCount = 0;
  const assert = (value, message) => { assertionCount++; if (!value) throw Error(message); };
  const until = async (predicate, label) => { for (let attempt = 0; attempt < 100; attempt++) { if (predicate()) return; await delay(20); } throw Error('Short UI timeout: ' + label); };
  const card = () => document.querySelector('.game-card[data-id="fixture"]');
  const host = () => document.querySelector('.game-card.expanded .game-detail.gp-inline');
  const state = () => host()?.__gpController.getState();
  const field = (group, key) => host()?.querySelector(`[data-gp-group="${group}"][data-gp-field="${key}"]`);
  const button = action => host()?.querySelector(`[data-gp-action="${action}"]`);
  const click = action => { const item = button(action); assert(item && !item.disabled && item.getClientRects().length, 'short action available: ' + action); item.click(); };
  await until(() => card()?.querySelector('.open-game-page-btn'), 'library ready'); card().querySelector('.open-game-page-btn').click();
  await until(() => state()?.loaded.includes('installation'), 'installation baseline');
  assert(document.getElementById('view-games').classList.contains('active') && !document.getElementById('view-game-detail').classList.contains('active'), 'short baseline stays in library');

  // Test the actual global refresh path that previously returned an unwrapped
  // boolean to runAction, producing a false failure after a successful scan.
  const beforeRefresh = mock.calls.filter(row => row[0] === 'refresh').length;
  document.getElementById('refreshBtn').click();
  await until(() => mock.calls.filter(row => row[0] === 'refresh').length === beforeRefresh + 1 && document.getElementById('toast').classList.contains('show'), 'global refresh notification');
  const toast = document.getElementById('toast');
  assert(!toast.classList.contains('error') && toast.textContent === '扫描完成', 'successful refresh reports scan completion without an error');

  // An installed legacy version may be normalized to a newer visible choice.
  // Its update action must preview that full choice without silently migrating.
  mock.assessment.game.addonVersion = mock.assessment.deployment.version = '0.4.5-ota';
  mock.assessment.defaults.version = '0.4.7beta';
  const originalLayout = JSON.stringify(mock.assessment.layout), originalDeployment = JSON.stringify(mock.assessment.deployment);
  click('refresh'); await until(() => state().data.deployment.version === '0.4.5-ota' && mock.pending === 0, 'legacy version baseline');
  assert(field('route', 'version').value === '0.4.7beta' && !state().draft.version, 'new Core choice is visible without a user dropdown change');
  assert(host().textContent.includes('当前已安装 0.4.5-ota') && host().textContent.includes('应用后才会更新'), 'actual installed version is clearly distinguished');
  assert(button('prepare')?.textContent === '预览更新配套', 'legacy installation exposes its update action');
  const beforeApply = mock.calls.filter(row => row[0] === 'apply').length;
  click('prepare'); await until(() => host().querySelector('.gp-modal'), 'legacy update preview');
  assert(mock.plan.request.version === '0.4.7beta' && mock.plan.request.api === 'auto' && mock.plan.request.deployment === 'local' && mock.plan.request.loadingMode === undefined, 'legacy update previews the current Core choice, automatic API preference and existing local layout');
  assert(mock.calls.filter(row => row[0] === 'apply').length === beforeApply && JSON.stringify(mock.assessment.layout) === originalLayout && JSON.stringify(mock.assessment.deployment) === originalDeployment, 'preview neither applies nor migrates existing files');
  click('modal-cancel'); click('discard');
  mock.assessment = structuredClone(mock.baseline); click('refresh'); await until(() => state().data.deployment.version === '0.4.7beta' && mock.pending === 0, 'restored normal screenshot baseline');

  mock.delays['fixture:enhancements'] = 120;
  host().querySelector('[data-gp-tab="enhance"]').click();
  await until(() => state().loaded.includes('enhancements'), 'short lazy enhancement load');
  assert(document.activeElement === host().querySelector('[data-gp-tab="enhance"]'), 'tab keyboard focus survives asynchronous section rendering');
  assert(field('sr', 'preset').value === 'M' && field('sr', 'quality').value === 'preserve' && !field('sr', 'renderPercent'), 'short enhancement baseline shows explicit M without changing game quality');
  host().querySelector(`[data-gp-tab="${tab}"]`).click();
  if (tab === 'maintenance') await until(() => state().loaded.includes('diagnostics'), 'short diagnostic load');
  await delay(40);
  assert(!toast.classList.contains('error'), 'no unrecognized error toast is hidden');
  if (toast.classList.contains('show')) { assert(toast.textContent === '扫描完成', 'only the known refresh success notification is cleared'); toast.classList.remove('show'); }
  const nodes = [document.documentElement, document.body, document.getElementById('view-games'), card(), host(), ...host().querySelectorAll('.gp-controls')];
  const widths = nodes.filter(node => node.getClientRects().length).map(node => ({ node: node.id || node.className || node.tagName, clientWidth: node.clientWidth, scrollWidth: node.scrollWidth }));
  assert(widths.every(row => row.scrollWidth <= row.clientWidth + 1), 'narrow baseline has no horizontal overflow: ' + JSON.stringify(widths));
  assert(!mock.calls.some(row => row[0] === 'startup-failed' || row[0] === 'legacy-mount' || row[0].startsWith('direct-')), 'short baseline invokes no legacy or direct writers');
  return { scope: 'capture-only', assertionCount, tab, viewport: { width: innerWidth, height: innerHeight }, horizontalOverflow: false,
    writes: mock.calls.filter(row => row[0] === 'apply').length, previews: mock.calls.filter(row => row[0] === 'preview').length,
    assertions: 'global refresh success notification, unapplied legacy Core update preview, preserved local layout, lazy tab focus, explicit SR baseline and narrow overflow', widths };
}

module.exports = { installMock, smoke, captureBaseline };
