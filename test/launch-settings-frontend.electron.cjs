'use strict';
// Run with electron test/launch-settings-frontend.electron.cjs [screenshot.png].
// The real renderer is loaded in a hidden window. IPC is mocked; no game or
// driver settings are touched. The temporary Electron profile is isolated.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'launch-ui-smoke-'));
app.setPath('userData', path.join(temporary, 'profile'));
app.disableHardwareAcceleration();

function installMock() {
  const ok = value => ({ ok: true, value: structuredClone(value) });
  const mock = window.__launchMock = {
    data: { hardware: { family: 'RTX40', series: ['RTX40'], names: ['NVIDIA GeForce RTX 4090'], source: 'fixture' }, requests: {}, applied: {}, pending: [], driverScope: { name: 'Fixture Shared Profile', applications: ['Game.exe', 'Launcher.exe'], predefined: true, shared: true }, fgComponents: { ready: true, canPrepare: false, missing: [], blockers: [] } },
    calls: [], failUpdate: false, listener: null
  };
  const game = { id: 'ui-fixture', name: 'SR / FG 界面验证', dir: 'C:\\UI-fixture\\游戏目录', installed: true, supported: true, launcher: '手动添加',
    chosen: { apiResolution: { api: 'dx12', evidence: [] }, detectedApiResolution: { api: 'dx12', evidence: [] }, bitness: 64 }, components: { dx11Carrier: false }, addonVersion: '0.4.6-hotfix.1' };
  mock.payload = { ready: true, selectedVersion: '0.4.6-hotfix.1', source: { mode: 'bundled', path: 'C:\\组件\\随程序提供', ready: true }, versions: { '0.4.6-hotfix.1': { label: '0.4.6-hotfix.1', variants: { RTX40: { ready: true, files: [] }, RTX50: { ready: true, files: [] } } } } };
  const originalVersions = structuredClone(mock.payload.versions);
  const sourceState = () => ok({ settings: {}, payload: mock.payload, addons: [] });
  const manager = window.manager = {
    boot: async () => ok({ product: { name: 'DLSS 5 AI 超分管理器', edition: '装机宅版', author: '野生的装机宅', version: 'UI smoke' }, settings: {}, hardware: mock.data.hardware,
      payload: mock.payload, games: [game, { ...game, id: 'ui-fixture-2', name: '待检查的游戏' }, { ...game, id: 'ui-fixture-3', name: '待安装的游戏', installed: false, supported: false, supportCode: 'ERR_API_SELECTION_REQUIRED', chosen: { ...game.chosen, apiResolution: { api: 'unknown' }, detectedApiResolution: { api: 'unknown' } } }], addons: [] }),
    readPayloadSource: async () => sourceState(),
    openExternal: async key => { mock.calls.push(['open-external', key]); return ok(true); },
    pickPayloadSource: async () => { mock.calls.push(['pick-source']); if (mock.failSource) return { ok: false, error: { code: 'ERR_PAYLOAD_SOURCE_HASH', message: '测试组件校验失败：核心文件不匹配' } }; mock.payload.source = { mode: 'external', path: 'D:\\安装组件库\\已校验', ready: true }; mock.payload.versions['fixture-core-v2'] = structuredClone(originalVersions['0.4.6-hotfix.1']); return sourceState(); },
    recheckPayloadSource: async () => { mock.calls.push(['recheck-source']); mock.payload.source.ready = false; mock.payload.source.error = { code: 'ERR_PAYLOAD_SOURCE_UNAVAILABLE', message: '所选组件目录已断开，请重新连接。' }; mock.payload.ready = false; mock.payload.versions = {}; return { ok: false, error: mock.payload.source.error }; },
    resetPayloadSource: async () => { mock.calls.push(['reset-source']); mock.payload.source = { mode: 'bundled', path: 'C:\\组件\\随程序提供', ready: true }; mock.payload.versions = structuredClone(originalVersions); mock.payload.ready = true; return sourceState(); },
    readNr: async () => ok({ AutoMask: 1, Style: 0, Intensity: 1, TransferStrength: 1, PostTransferStrength: 1, capabilities: { TransferStrength: true, PostTransferStrength: true }, ColorStrength: 0.75, SkinStructureStrength: 1 }),
    writeNr: async (id, patch) => { mock.calls.push(['write-nr', id, patch]); return ok(patch); },
    diagnose: async () => ok({ components: [{ ok: true, label: '核心组件', detail: '测试桩' }] }),
    readHotkeys: async () => ok({ reshade: { key: 36 } }),
    fetchGameIcon: async () => ok(null), fetchGameArt: async () => ok(null),
    onAddonImported: () => {}, onLaunchSettingsApplied: callback => { mock.listener = callback; },
    inspectLaunchSettings: async id => { mock.calls.push(['inspect', id]); return ok(mock.data); },
    prepareFgComponents: async (id, options) => { mock.calls.push(['prepare-components', id, options]); mock.data.fgComponents = { route: 'compatibility', ready: true, managed: true, missing: [], blockers: [] }; return ok({ prepared: true }); },
    restoreFgComponents: async id => { mock.calls.push(['restore-components', id]); delete mock.data.requests.fg; delete mock.data.applied.fg; mock.data.fgComponents = { route: 'native', ready: true, missing: [], blockers: [] }; return ok({ restored: true }); },
    updateLaunchSettings: async (id, domain, request, options) => {
      mock.calls.push(['update', id, domain, request, options]);
      if (mock.failUpdate) return { ok: false, error: { code: 'SETTINGS_TEST_UPDATE', message: '模拟自动应用失败' } };
      if (domain === 'fg' && request.backend === 'rtx40' && mock.data.fgComponents?.ready !== true) {
        mock.calls.push(['auto-prepare-components', id]);
        mock.data.fgComponents = { route: 'compatibility', ready: true, managed: true, missing: [], blockers: [] };
      }
      mock.data.requests[domain] = { exe: game.dir + '\\game.exe', request, savedAt: new Date().toISOString() };
      mock.data.applied[domain] = { request, backend: request.backend, readbackVerified: true, runtimeVerified: false };
      return ok({ applied: true, runtimeVerified: false });
    },
    resetAllLaunchSettings: async id => {
      mock.calls.push(['reset-all', id]); mock.data.requests = {}; mock.data.applied = {};
      if (mock.data.legacy) Object.assign(mock.data.legacy, { managed: true, baselineCaptured: false });
      return ok({ restored: true });
    },
    recoverLaunchSettings: async id => { mock.calls.push(['recover', id]); mock.data.pending = []; return ok({ recovered: true }); },
    minimize: () => {}, close: () => {}
  };
  window.addEventListener('error', event => { mock.error = event.message; });
}

async function smoke() {
  const checks = [];
  const mock = window.__launchMock;
  function assert(condition, description) { if (!condition) throw new Error(description); checks.push(description); }
  const waitUntil = async predicate => { const deadline = Date.now() + 5000; while (!predicate()) { if (Date.now() > deadline) throw new Error('Timed out waiting for renderer'); await new Promise(resolve => setTimeout(resolve, 10)); } };
  const visible = node => Boolean(node && node.getClientRects().length && getComputedStyle(node).visibility !== 'hidden');
  await waitUntil(() => document.querySelector('.game-card-head'));
  const communityNote = document.querySelector('.community-note');
  assert(communityNote && document.getElementById('bilibiliBtn').nextElementSibling === communityNote && communityNote.textContent.includes('用爱发电') && communityNote.textContent.includes('不要付费') && visible(communityNote), 'The quiet two-line free-use note appears below the homepage link');
  const apiFixture = document.createElement('div');
  for (const [api, label] of [['dx11', 'DX11 桥接（自动）'], ['dx12', 'DX12（自动）'], ['unknown', 'API 待确认（自动）']]) {
    apiFixture.innerHTML = apiControls({ apiOverride: 'auto', chosen: { detectedApi: api, detectedApiResolution: { api, source: 'fixture', evidence: [] }, apiResolution: { api, source: 'fixture' } } });
    const select = apiFixture.querySelector('.game-api-select');
    assert(select.value === 'auto' && select.querySelector('option[value="auto"]').textContent === label, `${api} automatic option displays the detected route precisely`);
    if (api === 'dx11') {
      select.value = 'dx12';
      assert(select.value === 'dx12' && select.selectedOptions[0].value !== 'auto', 'Manual DX12 can be selected independently of the detected DX11 automatic option');
    }
  }
  apiFixture.innerHTML = apiControls({ apiOverride: 'dx12', chosen: { detectedApi: 'dx11', detectedApiResolution: { api: 'dx11', source: 'fixture', evidence: [] }, apiResolution: { api: 'dx12', source: 'override' } } });
  assert(apiFixture.querySelector('.game-api-select').value === 'dx12' && apiFixture.querySelector('option[value="auto"]').textContent === 'DX11 桥接（自动）', 'Rerender retains manual DX12 while the automatic option continues to report detected DX11');
  assert(!document.querySelector('.launch-settings-host'), 'Collapsed cards do not build hidden SR/FG forms');
  document.querySelector('[data-id="ui-fixture-3"] .game-card-head').click();
  const pendingDetail = document.querySelector('[data-id="ui-fixture-3"] .game-detail');
  assert(pendingDetail.detailTabController.active() === 'enhance' && !pendingDetail.querySelector('[data-detail-panel="enhance"]').hidden && visible(pendingDetail.querySelector('.game-api-select')) && !pendingDetail.querySelector('.nr-mask'), 'An uninstalled unknown-API game starts on enhancement with its API choice available above the tabs');
  document.querySelector('[data-id="ui-fixture-3"] .game-card-head').click();
  document.querySelector('.game-card-head').click();
  const detail = document.querySelector('.game-detail'), tabs = detail.detailTabController;
  const host = document.querySelector('.launch-settings-host');
  assert(tabs.active() === 'enhance' && !host.launchSettingsController, 'An installed game starts on enhancement without probing hidden SR/FG settings');
  assert(detail.querySelectorAll('[data-detail-tab]').length === 2 && !detail.querySelector('[data-detail-tab="diagnostics"]'), 'Game details offer only enhancement and graphics tabs');
  const apiSelect = detail.querySelector('.game-api-select'), coreSelect = detail.querySelector('.game-version-select');
  assert(!apiSelect.closest('[data-detail-panel]') && !coreSelect.closest('[data-detail-panel]') && visible(apiSelect) && visible(coreSelect), 'API and core controls live outside tab panels and are visible on enhancement');
  assert(detail.querySelector('.skin-settings').tagName === 'DIV' && !detail.querySelector('.nr-mask').closest('details') && visible(detail.querySelector('.nr-mask')) && visible(detail.querySelector('.nr-skin-strength')), 'Skin protection and strength are visible without expanding an advanced section');
  const coveredTab = detail.querySelector('[data-detail-tab="graphics"]');
  detail.querySelector('.uninstall-inline-btn').click();
  const removalModal = document.getElementById('modal'), tabRect = coveredTab.getBoundingClientRect();
  const hit = document.elementFromPoint(tabRect.left + tabRect.width / 2, tabRect.top + tabRect.height / 2);
  assert(!removalModal.classList.contains('hidden') && Number(getComputedStyle(removalModal).zIndex) > Number(getComputedStyle(detail.querySelector('.detail-tabs')).zIndex), 'The removal modal is stacked above sticky detail tabs');
  assert(hit === removalModal || hit?.closest('#modal') === removalModal, 'The modal mask receives pointer hits over the underlying sticky tab');
  document.getElementById('modalConfirm').focus();
  assert(document.activeElement === document.getElementById('modalConfirm') && removalModal.matches(':focus-within'), 'Modal actions retain keyboard focus above the covered content');
  hit.click();
  assert(tabs.active() === 'enhance' && !removalModal.classList.contains('hidden'), 'Clicking the mask over a tab cannot activate the covered tab or dismiss the confirmation');
  document.getElementById('modalCancel').click();
  detail.querySelector('[data-detail-tab="graphics"]').click();
  assert(visible(apiSelect) && visible(coreSelect), 'The same public API and core controls remain visible on graphics');
  await host.launchSettingsController.ready;
  const controller = host.launchSettingsController;
  assert(host.querySelector('.launch-driver-scope')?.textContent.includes('Fixture Shared Profile') && host.querySelector('.launch-driver-scope')?.textContent.includes('关联 2 个启动入口'), 'A shared NVIDIA profile explains that settings affect both launch entries');
  const capabilityHost = document.createElement('div'); capabilityHost.className = 'launch-settings-host'; host.parentElement.appendChild(capabilityHost);
  const writesBeforeCapability = mock.calls.filter(row => row[0] === 'update').length;
  const capabilityController = window.launchSettingsUi.mount(capabilityHost, 'ui-fixture', { manager: window.manager, nativeDlssAvailable: false });
  await capabilityController.ready;
  assert(capabilityHost.textContent.includes('未检测到普通 DLSS') && [...capabilityHost.querySelectorAll('fieldset')].every(item => item.disabled), 'A game without detected ordinary DLSS clearly disables both SR and FG editors');
  assert(!capabilityHost.querySelector('[data-ls-action="restore-all"]').disabled && !capabilityHost.querySelector('[data-ls-action="prepare"]:not([disabled])'), 'Missing DLSS keeps restoration available while component preparation cannot run');
  await capabilityController.perform('auto', 'sr');
  assert(mock.calls.filter(row => row[0] === 'update').length === writesBeforeCapability, 'A disabled capability cannot be bypassed through the renderer controller');
  capabilityHost.remove();
  const panel = domain => host.querySelector(`[data-ls-domain="${domain}"]`);
  const field = (domain, key) => panel(domain).querySelector(`[data-ls-field="${key}"]`);
  const set = (domain, key, value) => {
    const input = field(domain, key);
    if (!input) throw new Error(`Missing field ${domain}.${key}`);
    if (input.type === 'checkbox') input.checked = Boolean(value); else input.value = String(value);
    input.dispatchEvent(new Event(input.type === 'number' ? 'input' : 'change', { bubbles: true }));
  };
  const click = async (domain, action) => {
    const button = (domain ? panel(domain) : host).querySelector(`[data-ls-action="${action}"]`);
    if (!button || button.disabled || button.closest('fieldset')?.disabled) throw new Error(`Unavailable action ${domain}.${action}`);
    button.click();
    await waitUntil(() => !controller.getState().busy);
  };
  const count = action => mock.calls.filter(row => row[0] === action).length;
  const saved = domain => mock.data.requests[domain]?.request;
  const waitForAuto = async before => {
    await waitUntil(() => count('update') > before);
    await waitUntil(() => !controller.getState().busy);
  };
  await new Promise(resolve => setTimeout(resolve, 560));
  assert(count('update') === 0 && count('reset-all') === 0, 'Initialization inspects settings without writing them');
  const readsBeforeFilter = count('inspect');
  document.querySelector('#searchInput').value = 'SR';
  document.querySelector('#searchInput').dispatchEvent(new Event('input', { bubbles: true }));
  await new Promise(resolve => setTimeout(resolve, 160));
  assert(document.querySelector('.launch-settings-host') === host && count('inspect') === readsBeforeFilter, 'Search keeps the expanded editor and does not repeat component inspection');
  document.querySelector('#searchInput').value = '';
  document.querySelector('#searchInput').dispatchEvent(new Event('input', { bubbles: true }));
  await new Promise(resolve => setTimeout(resolve, 160));
  const hardware = async (series, family = series === 'RTX50' ? 'RTX50' : 'RTX40') => {
    mock.data.hardware = { family, series: series ? [series] : [], names: series ? [`NVIDIA GeForce ${series.replace('RTX', 'RTX ')}90`] : [], source: 'fixture' };
    mock.data.requests = {}; mock.data.applied = {};
    await controller.refresh(true);
  };

  assert(document.querySelectorAll('.sr-model-select').length === 0, 'Legacy SR selector is absent');
  assert(document.querySelectorAll('[aria-label="DLSS SR 模型"]').length === 1, 'There is exactly one SR model selector');
  assert(!field('sr', 'backend'), 'SR has no backend selector');
  assert(field('sr', 'preset').value === 'auto' && field('sr', 'preset').selectedOptions[0].textContent === '自动推荐 · M（推荐）', 'RTX40 starts with the visible M automatic recommendation without an initialization write');
  assert([...field('sr', 'preset').options].some(item => item.value === 'L' && item.textContent.includes('高画质低性能')), 'Manual L explains its quality and performance tradeoff');
  assert([...field('sr', 'preset').options].some(item => item.value === 'M' && item.textContent.includes('推荐（适用 RTX40 / RTX50）')), 'Manual M identifies its RTX40 and RTX50 recommendation');
  assert(document.querySelector('.game-version-select').textContent.includes('Beta'), 'Core Beta choice remains visible');
  assert(!document.querySelector('.carrier-component-check'), 'API route owns compatibility bridging without a separate checkbox');
  assert(field('sr', 'renderPercent').value === '67', 'Input percentage is visible before any interaction');
  const beforeMerged = count('update');
  set('sr', 'quality', 'dlaa'); set('sr', 'quality', 'balanced'); set('sr', 'quality', 'performance');
  assert(field('sr', 'renderPercent').value === '50', 'Rapid preset changes update the displayed percentage immediately');
  await waitForAuto(beforeMerged);
  assert(count('update') === beforeMerged + 1 && saved('sr').quality === 'performance' && saved('sr').renderPercent === undefined, 'Rapid changes merge into one automatic update with the final driver preset');
  assert(!host.querySelector('[data-ls-action="save"]') && !host.querySelector('[data-ls-action="preview"]') && !host.querySelector('[data-ls-action="confirm"]'), 'The automatic editor exposes no save, preview or confirmation controls');
  const ratioInput = field('sr', 'renderPercent'); ratioInput.focus(); set('sr', 'renderPercent', 75);
  assert(field('sr', 'quality').value === 'custom' && document.activeElement === ratioInput, 'Editing ratio selects custom without losing focus');
  const beforeRatio = count('update'); await waitForAuto(beforeRatio);
  assert(saved('sr').quality === 'custom' && saved('sr').renderPercent === 75, 'A numeric ratio automatically applies as a custom request');
  tabs.select('enhance');
  const modelStrength = document.querySelector('.nr-model-strength');
  const effectStrength = document.querySelector('.nr-effect-strength');
  await waitUntil(() => modelStrength.oninput && effectStrength.oninput);
  modelStrength.value = '1.2'; modelStrength.dispatchEvent(new Event('input', { bubbles: true }));
  effectStrength.value = '1.4'; effectStrength.dispatchEvent(new Event('input', { bubbles: true }));
  await waitUntil(() => mock.calls.some(row => row[0] === 'write-nr'));
  const nrPatch = mock.calls.find(row => row[0] === 'write-nr')[2];
  assert(nrPatch.Intensity === 1.2 && nrPatch.TransferStrength === 1.4 && nrPatch.PostTransferStrength === 1.4 && !('ColorStrength' in nrPatch), 'Strength controls write the correct paired core values without changing hue follow');
  const originalReadNr = window.manager.readNr, originalDetailDiagnose = window.manager.diagnose, originalReadHotkeys = window.manager.readHotkeys;
  const detailReads = { nr: 0, diagnostic: 0, hotkeys: 0 };
  const mismatch = { ok: false, error: { code: 'ERR_INSTALL_EXE_CHANGED', message: '安装记录属于原 EXE，请先还原或重新选择原程序。' } };
  window.manager.readNr = async () => { detailReads.nr++; return mismatch; };
  window.manager.diagnose = async () => { detailReads.diagnostic++; return { ok: true, value: { executableMismatch: true, components: [{ ok: false, label: '安装 EXE 与当前所选程序不一致', detail: '原 EXE 的安装记录仍保留，请先处理绑定。' }] } }; };
  window.manager.readHotkeys = async () => { detailReads.hotkeys++; return mismatch; };
  const writesBeforeFailedRead = count('write-nr');
  await loadExpanded('ui-fixture');
  assert(detailReads.nr === 1 && detailReads.diagnostic === 1 && detailReads.hotkeys === 1, 'A detail reload reads NR, diagnostics and hotkeys once without repeating diagnostic IO');
  assert(detail.querySelector('.component-body').textContent.includes('安装 EXE 与当前所选程序不一致') && detail.querySelector('.component-summary').textContent.includes('1 项需要处理'), 'An EXE mismatch diagnostic remains visible when NR and hotkey reads reject');
  assert([...detail.querySelectorAll('.nr-mask, .style-segment button, .nr-model-strength, .nr-effect-strength, .nr-skin-strength, .default-btn')].every(control => control.disabled), 'An NR read failure disables NR editors and restoring defaults');
  assert(detail.querySelector('.save-state').textContent === '读取失败' && detail.querySelector('.nr-read-error').textContent.includes('ERR_INSTALL_EXE_CHANGED') && detail.querySelector('.nr-read-error').textContent.includes('原 EXE'), 'NR failure shows its readable reason without claiming settings were read');
  assert(detail.querySelector('.reshade-hotkey-btn').disabled && detail.querySelector('.reshade-hotkey-btn').textContent === '读取失败' && detail.querySelector('.reshade-hotkey-btn').title.includes('ERR_INSTALL_EXE_CHANGED'), 'A rejected hotkey read cannot display the previous Home key as a fresh result');
  detail.querySelector('.style-segment button').click(); detail.querySelector('.default-btn').click();
  assert(count('write-nr') === writesBeforeFailedRead, 'Disabled controls do not submit NR changes after a failed read');
  window.manager.readNr = async () => { const result = await originalReadNr(); return { ok: true, value: { ...result.value, AutoMask: 0, capabilities: { TransferStrength: false, PostTransferStrength: false } } }; };
  window.manager.diagnose = originalDetailDiagnose; window.manager.readHotkeys = originalReadHotkeys;
  await loadExpanded('ui-fixture');
  assert(!modelStrength.disabled && !detail.querySelector('.nr-mask').disabled && !detail.querySelector('.default-btn').disabled && [...detail.querySelectorAll('.style-segment button')].every(control => !control.disabled), 'A valid reread reenables supported NR editors and restoring defaults');
  assert(effectStrength.disabled && detail.querySelector('.nr-skin-strength').disabled, 'Recovery still respects missing effect capabilities and disabled skin protection');
  assert(detail.querySelector('.save-state').textContent === '已读取设置' && !detail.querySelector('.save-state').classList.contains('error') && !detail.querySelector('.nr-read-error') && !detail.querySelector('.reshade-hotkey-btn').disabled && detail.querySelector('.reshade-hotkey-btn').textContent === 'Home', 'Successful rereads clear stale errors and restore the verified hotkey display');
  let finishStaleNr;
  window.manager.readNr = () => new Promise(resolve => { finishStaleNr = () => resolve(mismatch); });
  window.manager.diagnose = async () => ({ ok: true, value: { components: [{ ok: false, label: '过期 EXE 诊断', detail: '不应覆盖新读取' }] } });
  window.manager.readHotkeys = async () => mismatch;
  const staleRead = loadExpanded('ui-fixture');
  await waitUntil(() => finishStaleNr);
  window.manager.readNr = originalReadNr; window.manager.diagnose = originalDetailDiagnose; window.manager.readHotkeys = originalReadHotkeys;
  await loadExpanded('ui-fixture');
  finishStaleNr(); await staleRead;
  assert(!effectStrength.disabled && !detail.querySelector('.nr-skin-strength').disabled && detail.querySelector('.save-state').textContent === '已读取设置', 'The latest legal reread restores effect and skin controls without a stale NR rejection disabling them');
  assert(!detail.querySelector('.component-body').textContent.includes('过期 EXE 诊断') && !detail.querySelector('.nr-read-error') && detail.querySelector('.reshade-hotkey-btn').textContent === 'Home', 'A superseded allSettled load cannot overwrite current diagnostics, errors or hotkeys');
  tabs.select('graphics');
  mock.data.requests = {}; mock.data.applied = {};
  mock.data.legacy = { selection: 'auto', effective: 'm', managed: false, baselineCaptured: true };
  await controller.refresh(true);
  assert(field('sr', 'quality').value === 'quality' && field('sr', 'preset').value === 'auto', 'Legacy automatic selection remains automatic while its current M result stays visible');
  assert(panel('sr').textContent.includes('沿用原模型设置：自动 → 模型 M') && !panel('sr').textContent.includes('未设启动覆盖'), 'Legacy launch behavior stays visible before ownership migration');
  await click(null, 'restore-all');
  assert(field('sr', 'quality').value === 'quality' && field('sr', 'preset').value === 'auto' && !panel('sr').textContent.includes('沿用原模型设置'), 'A restored legacy policy returns to the explicit hardware-bound recommendation without silently restoring old M ownership');
  set('sr', 'quality', 'custom'); set('sr', 'renderPercent', 75); set('sr', 'preset', 'M');
  const beforeSrAuto = count('update'); await waitForAuto(beforeSrAuto);
  assert(saved('sr').renderPercent === 75 && saved('sr').preset === 'M' && mock.data.applied.sr.readbackVerified, 'Native SR automatically saves, applies and reads back custom 75 percent with model M');
  assert(panel('sr').textContent.includes('配置已校验') && panel('sr').textContent.includes('游戏内实际表现'), 'Readback remains distinct from the stated need for in-game verification');
  const applyScrollOwner = document.querySelector('.view.active'); applyScrollOwner.scrollTop = Math.max(120, applyScrollOwner.scrollTop);
  const scrollBeforeAuto = applyScrollOwner.scrollTop;
  const beforeK = count('update'); set('sr', 'preset', 'K'); await waitForAuto(beforeK);
  assert(saved('sr').preset === 'K', 'Changing the SR model automatically applies K without confirmation');
  assert(applyScrollOwner.scrollTop === scrollBeforeAuto, 'Busy rendering and automatic apply refresh preserve the game view scroll position');

  mock.data.requests.sr = { request: { backend: 'optiscaler', quality: 'custom', renderPercent: 75 } };
  await controller.refresh(true);
  assert(!field('sr', 'backend') && panel('sr').querySelector('fieldset').disabled && saved('sr').backend === 'optiscaler', 'Legacy OptiScaler requests are preserved for restoration without an editable backend');
  await click(null, 'restore-all');
  assert(!saved('sr') && field('sr', 'renderPercent'), 'Restoring legacy OptiScaler unlocks native SR editing');
  const preparesBeforeRuntime = count('prepare-components');
  mock.data.fgComponents = { route: 'compatibility', ready: false, canPrepare: false, missing: [], blockers: ['缺少 VC++ x64 运行库：vcruntime140.dll'], runtime: { status: 'missing', ready: false } };
  await controller.refresh();
  assert(panel('fg').querySelector('[data-ls-action="runtime-help"]') && !panel('fg').querySelector('[data-ls-action="prepare"]') && field('fg', 'mode'), 'Missing VC runtime exposes repair guidance while keeping the intended FG choice editable');
  await click('fg', 'runtime-help');
  assert(mock.calls.some(row => row[0] === 'open-external' && row[1] === 'vcRuntimeUrl') && count('prepare-components') === preparesBeforeRuntime, 'Runtime help opens the fixed official link without writing game files');
  await click('fg', 'runtime-check');
  assert(panel('fg').textContent.includes('运行库检查尚未通过') && field('fg', 'mode'), 'An unsuccessful runtime recheck remains explicit without discarding the intended FG choice');
  mock.data.fgComponents.runtime.repair = 'game-files'; await controller.refresh();
  assert(!panel('fg').querySelector('[data-ls-action="runtime-help"]') && panel('fg').textContent.includes('验证游戏文件'), 'A broken game-local DLL is not misrepresented as fixable by installing a system runtime');
  mock.data.fgComponents = { route: 'compatibility', ready: false, canPrepare: true, missing: ['兼容插件'], blockers: [], runtime: { status: 'available', ready: true } };
  await click('fg', 'runtime-check');
  assert(panel('fg').querySelector('[data-ls-action="prepare"]') && panel('fg').textContent.includes('游戏内加载与帧生成仍需验证'), 'Runtime recovery unlocks preparation while keeping game verification separate');
  mock.data.fgComponents = { route: 'compatibility', ready: false, canPrepare: true, missing: ['兼容插件', '加载器'], blockers: [] };
  await controller.refresh();
  assert(panel('fg').querySelector('[data-ls-action="prepare"]') && field('fg', 'mode') && !field('fg', 'mode').disabled, 'Missing RTX40 components leave FG choices selectable for the automatic backend transaction');
  const beforeCompat = count('update'); set('fg', 'mode', 'fixed'); set('fg', 'multiplier', 6); await waitForAuto(beforeCompat);
  assert(mock.data.fgComponents.ready && saved('fg').backend === 'rtx40' && saved('fg').multiplier === 6 && count('auto-prepare-components') === 1, 'RTX40 selection automatically prepares compatibility components and applies the request');
  assert(panel('fg').textContent.includes('1 帧渲染 + 5 帧生成'), 'RTX40 multiplier includes the rendered frame');
  for (const series of ['RTX40', 'RTX50']) {
    await hardware(series);
    const expected = series === 'RTX40' ? 'rtx40' : 'nvidia';
    assert(controller.getState().drafts.fg.backend === expected, `${series} starts with the correct ${expected} backend`);
    const beforeFixed = count('update'); set('fg', 'mode', 'fixed'); set('fg', 'multiplier', 6); await waitForAuto(beforeFixed);
    assert(saved('fg').backend === expected && saved('fg').multiplier === 6, `${series} fixed 6x automatically uses the correct backend`);
    assert(panel('fg').textContent.includes('1 帧渲染 + 5 帧生成'), `${series} multiplier includes the rendered frame`);
    assert(mock.data.applied.fg.request.multiplier === 6, `${series} fixed request is applied without a preview or confirmation`);
    set('fg', 'mode', 'dynamic'); set('fg', 'targetFps', 144);
    if (series === 'RTX40') set('fg', 'experimental56', true);
    const beforeDynamic = count('update'); await waitForAuto(beforeDynamic);
    assert(saved('fg').mode === 'dynamic' && saved('fg').targetFps === 144 && !('multiplier' in saved('fg')), `${series} dynamic mode omits dormant fixed multiplier`);
    assert(series === 'RTX40' ? saved('fg').experimental56 === true : !('experimental56' in saved('fg')), `${series} experimental option follows its backend schema`);
    if (series === 'RTX50') {
      const beforeOff = count('update'); set('fg', 'mode', 'off'); await waitForAuto(beforeOff);
      assert(saved('fg').mode === 'off' && panel('fg').textContent.includes('游戏菜单可能不变'), 'Native FG off has a driver-disable request and honest game-state explanation');
      set('fg', 'mode', 'restore');
      assert(panel('fg').textContent.includes('不会关闭 FG'), 'Restore is explicitly distinguished from FG off');
      const beforeRestoreMode = count('update'); await waitForAuto(beforeRestoreMode);
    }
  }
  await hardware('RTX30');
  assert(panel('fg').querySelector('fieldset').disabled, 'RTX30 is not inferred as RTX40 from payload family');
  await hardware(null, 'unknown');
  assert(panel('fg').querySelector('fieldset').disabled && panel('fg').textContent.includes('未确认'), 'Unknown hardware cannot request FG');
  await hardware('RTX40');
  mock.data.requests.fg = { request: { backend: 'nvidia', mode: 'fixed', multiplier: 3 } };
  await controller.refresh(true);
  assert(panel('fg').querySelector('fieldset').disabled && panel('fg').textContent.includes('不符'), 'A saved FG backend for another GPU cannot be resubmitted');
  await click(null, 'restore-all');
  mock.failUpdate = true;
  const beforeFailure = count('update'); set('sr', 'quality', 'balanced'); await waitForAuto(beforeFailure);
  assert(panel('sr').textContent.includes('模拟自动应用失败') && controller.getState().dirty.sr && panel('sr').querySelector('[data-ls-action="auto"]'), 'A failed automatic update stays dirty and exposes a retry');
  mock.failUpdate = false; await click('sr', 'auto');
  assert(!controller.getState().dirty.sr && saved('sr').quality === 'balanced', 'Retry applies the retained dirty draft');
  mock.data.pending = [{ kind: 'file-journal' }]; await controller.refresh();
  assert(panel('sr').querySelector('fieldset').disabled && host.querySelector('[data-ls-action="recover"]'), 'An unfinished transaction exposes recovery and locks editing');
  await click(null, 'recover');
  assert(!panel('sr').querySelector('fieldset').disabled && mock.data.pending.length === 0, 'Recovery reloads and unlocks settings');
  mock.data.applied.sr = { request: { backend: 'native', quality: 'quality' }, readbackVerified: true };
  await mock.listener({ id: 'ui-fixture', outcomes: [{ domain: 'sr', applied: true }] });
  assert(panel('sr').textContent.includes('配置已校验'), 'Launch IPC completion refreshes the visible settings');
  await mock.listener({ id: 'ui-fixture', outcomes: [{ domain: 'sr', applied: false, skipped: true }], launchFailed: true });
  assert(panel('sr').textContent.includes('配置已校验') && panel('sr').textContent.includes('不代表游戏当前档位'), 'Failed launch refreshes the retained receipt without claiming game verification');

  set('sr', 'quality', 'custom'); set('sr', 'renderPercent', 75); set('sr', 'preset', 'M');
  set('fg', 'mode', 'dynamic'); set('fg', 'targetFps', 144);
  const beforeTabUpdates = count('update');
  const graphicsTab = detail.querySelector('[data-detail-tab="graphics"]'); graphicsTab.focus();
  graphicsTab.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true }));
  assert(tabs.active() === 'enhance' && detail.querySelector('[data-detail-panel="graphics"]').hidden && detail.querySelector('[data-detail-panel="graphics"]').inert, 'Right arrow wraps from graphics to enhancement and removes hidden controls from keyboard interaction');
  assert(visible(detail.querySelector('.game-api-select')) && visible(detail.querySelector('.game-version-select')), 'Public API and core controls remain visible after keyboard tab navigation');
  document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
  assert(tabs.active() === 'graphics' && detail.querySelector('[data-detail-panel="enhance"]').hidden && detail.querySelector('[data-detail-panel="enhance"]').inert, 'Left arrow wraps from enhancement to graphics across exactly two tabs');
  document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
  assert(tabs.active() === 'enhance', 'Home reaches the first detail tab');
  document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: 'End', bubbles: true }));
  assert(tabs.active() === 'graphics', 'End reaches graphics as the last of two detail tabs');
  document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
  assert(tabs.select('invalid') === false && tabs.active() === 'enhance', 'Invalid tab selection is refused');
  detail.querySelector('.launch-jump-btn').click();
  assert(tabs.active() === 'graphics' && host.launchSettingsController === controller && field('sr', 'renderPercent').value === '75' && field('fg', 'targetFps').value === '144', 'Returning to graphics preserves both unsaved drafts and the existing controller');
  const viewBeforeApply = document.querySelector('.view.active'); viewBeforeApply.scrollTop = Math.max(80, viewBeforeApply.scrollTop);
  const preservedScroll = viewBeforeApply.scrollTop;
  const resetsBeforeGlobal = count('reset-all');
  await click(null, 'restore-all');
  await new Promise(resolve => setTimeout(resolve, 560));
  assert(count('reset-all') === resetsBeforeGlobal + 1 && count('update') === beforeTabUpdates, 'Global reset runs once for both domains and cancels pending draft timers without a later rewrite');
  assert(!saved('sr') && !saved('fg') && !mock.data.applied.sr && !mock.data.applied.fg, 'Global reset restores both SR and FG domains');
  assert(viewBeforeApply.scrollTop === preservedScroll, 'Busy rendering and reset refresh preserve the game view scroll position');
  for (const node of document.querySelectorAll('.app-shell, main, .view.active, .game-card, .game-detail, .launch-settings-host, .launch-panel, .launch-field select, .launch-number input')) {
    const rect = node.getBoundingClientRect();
    assert(rect.left >= -1 && rect.right <= innerWidth + 1 && node.scrollWidth <= node.clientWidth + 1, `900px width contains ${node.className || node.tagName}`);
  }
  assert(!mock.error, 'No renderer error occurred');
  document.querySelector('[data-view="addons"]').click();
  assert(document.querySelector('#addonVersionSelect').closest('#view-addons') && !document.querySelector('#view-settings #addonVersionSelect'), 'Default core selection lives with component management');
  assert(!document.querySelector('.addon-guide').open, 'Version history is collapsed so source and usable versions stay first');
  assert(document.querySelector('#addExeBtn').classList.contains('hidden'), 'Unrelated EXE picker is absent from plugin management');
  const sourceAction = async id => { document.getElementById(id).click(); await waitUntil(() => !document.getElementById('choosePayloadSourceBtn').disabled); };
  await sourceAction('choosePayloadSourceBtn');
  assert(document.getElementById('payloadSourcePath').textContent === 'D:\\安装组件库\\已校验' && !document.getElementById('resetPayloadSourceBtn').classList.contains('hidden'), 'Validated external directory and explicit return action are visible');
  assert(document.querySelector('.launch-settings-host') === host, 'Changing source preserves the existing expanded settings editor');
  assert([...document.querySelector('.game-version-select').options].some(option => option.value === 'fixture-core-v2') && document.querySelector('.game-version-select').value === '0.4.6-hotfix.1', 'A retained detail updates its catalog while keeping the previous valid version choice');
  mock.failSource = true; await sourceAction('choosePayloadSourceBtn');
  assert(document.getElementById('payloadSourceFeedback').textContent.includes('核心文件不匹配') && document.getElementById('payloadSourcePath').textContent.includes('已校验'), 'Rejected selection keeps the active source and a persistent readable error');
  await sourceAction('recheckPayloadSourceBtn');
  assert(document.getElementById('payloadSourceStatus').textContent === '需要处理' && document.getElementById('payloadSourceDetail').textContent.includes('断开'), 'Disconnected source remains visibly unavailable instead of claiming readiness');
  await sourceAction('resetPayloadSourceBtn');
  assert(document.getElementById('payloadSourceLabel').textContent === '随程序提供', 'Returning to bundled components is an explicit working action');
  const originalDiagnose = window.manager.diagnose; let finishOld;
  window.manager.diagnose = id => id === 'ui-fixture' ? new Promise(resolve => { finishOld = () => resolve({ ok: true, value: { components: [{ ok: false, label: '过期诊断', detail: '不可覆盖新选择' }] } }); })
    : Promise.resolve({ ok: true, value: { components: [{ ok: false, label: '当前诊断', detail: '当前游戏的问题' }] } });
  document.querySelector('[data-view="repair"]').click();
  await waitUntil(() => finishOld);
  const repairSelect = document.getElementById('repairGameSelect');
  repairSelect.value = 'ui-fixture-2'; repairSelect.dispatchEvent(new Event('change', { bubbles: true }));
  await waitUntil(() => document.getElementById('diagnostics').textContent.includes('当前诊断'));
  finishOld(); await new Promise(resolve => setTimeout(resolve, 10));
  assert(!document.getElementById('diagnostics').textContent.includes('过期诊断'), 'Old async diagnostics cannot overwrite a new game');
  repairSelect.value = ''; repairSelect.dispatchEvent(new Event('change', { bubbles: true }));
  assert(document.getElementById('uninstallBtn').disabled && document.getElementById('repairBtn').disabled && document.getElementById('copyDiagBtn').disabled, 'Without a selected game destructive and copy actions are disabled');
  window.manager.diagnose = originalDiagnose;
  document.querySelector('[data-view="games"]').click();
  document.querySelector('.launch-jump-btn').click();
  assert(document.querySelector('.titlebar').getBoundingClientRect().top >= 0 && document.scrollingElement.scrollTop === 0, 'SR/FG jump scrolls only the game view and preserves the fixed app header');
  const view = document.querySelector('.view.active');
  view.scrollTop += host.getBoundingClientRect().top - view.getBoundingClientRect().top - 12;
  return { checks, viewport: { width: innerWidth, height: innerHeight, launchTop: host.getBoundingClientRect().top, scrollTop: document.querySelector('.view.active').scrollTop } };
}

const preload = path.join(temporary, 'preload.cjs');
fs.writeFileSync(preload, `(${installMock.toString()})();`, 'utf8');
app.whenReady().then(async () => {
  const width = Number(process.env.SRFG_UI_WIDTH) || 900;
  const win = new BrowserWindow({ width, height: Number(process.env.SRFG_UI_HEIGHT) || 1100, useContentSize: true, show: false, webPreferences: { preload, sandbox: false, contextIsolation: false, nodeIntegration: false, backgroundThrottling: false, offscreen: true } });
  try {
    await win.loadFile(path.join(process.env.MANAGER_UI_ROOT || path.resolve(__dirname, '..'), 'src/renderer/index.html'));
    const result = await win.webContents.executeJavaScript(`(${smoke.toString()})()`);
    if (['rtx50', 'rtx40-prepare'].includes(process.env.SRFG_UI_STATE)) {
      const native = process.env.SRFG_UI_STATE === 'rtx50';
      await win.webContents.executeJavaScript(`(async()=>{
        const mock=window.__launchMock, host=document.querySelector('.launch-settings-host');
        mock.data.hardware={family:${JSON.stringify(native ? 'RTX50' : 'RTX40')},series:[${JSON.stringify(native ? 'RTX50' : 'RTX40')}],names:[${JSON.stringify(native ? 'NVIDIA GeForce RTX 5090' : 'NVIDIA GeForce RTX 4090')}]};
        mock.data.requests={}; mock.data.applied={};
        mock.data.fgComponents=${JSON.stringify(native ? { route: 'native', ready: true, missing: [], blockers: [] } : { route: 'compatibility', ready: false, canPrepare: true, missing: ['RTX40MFGCore.dll', 'RTX40MFG.asi', 'RTX40MFG-UI.addon64', 'version.dll'], blockers: [] })};
        await host.launchSettingsController.refresh(true);
        const view=document.querySelector('.view.active');view.scrollTop+=host.getBoundingClientRect().top-view.getBoundingClientRect().top-12;
      })()`);
    }
    if (process.env.SRFG_UI_STATE === 'bridge') await win.webContents.executeJavaScript("(()=>{const row=document.querySelector('.api-config-block'),detail=row.closest('.game-detail'),view=row.closest('.view');detail.detailTabController.select('enhance');view.scrollTop+=row.getBoundingClientRect().top-view.getBoundingClientRect().top-12;})()");
    if (process.env.SRFG_UI_STATE === 'runtime-missing') await win.webContents.executeJavaScript(`(async()=>{const mock=window.__launchMock,host=document.querySelector('.launch-settings-host');mock.data.fgComponents={route:'compatibility',ready:false,canPrepare:false,missing:[],blockers:['缺少 VC++ x64 运行库：vcruntime140.dll、msvcp140.dll。请安装或修复微软运行库后重新检查。'],runtime:{status:'missing',ready:false}};await host.launchSettingsController.refresh(false);const view=document.querySelector('.view.active');view.scrollTop+=host.getBoundingClientRect().top-view.getBoundingClientRect().top-12;})()`);
    win.webContents.invalidate();
    await new Promise(resolve => setTimeout(resolve, 150));
    const screenshot = process.argv[2];
    if (screenshot) { fs.mkdirSync(path.dirname(path.resolve(screenshot)), { recursive: true }); fs.writeFileSync(path.resolve(screenshot), (await win.webContents.capturePage()).toPNG()); }
    console.log(JSON.stringify({ ok: true, ...result, screenshot: screenshot || null }, null, 2));
    win.destroy(); app.exit(0);
  } catch (error) { console.error(error.stack || error); win.destroy(); app.exit(1); }
});
