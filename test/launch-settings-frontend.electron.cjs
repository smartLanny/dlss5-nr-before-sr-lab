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
    data: { hardware: { family: 'RTX40', series: ['RTX40'], names: ['NVIDIA GeForce RTX 4090'], source: 'fixture' }, requests: {}, applied: {}, pending: [] },
    calls: [], plans: {}, failSave: false, failApply: false, blockPreview: false, listener: null
  };
  const game = { id: 'ui-fixture', name: 'SR / FG 界面验证', dir: 'C:\\UI-fixture\\游戏目录', installed: true, supported: true, launcher: '手动添加',
    chosen: { apiResolution: { api: 'dx11', evidence: [] }, bitness: 64 }, components: { dx11Carrier: true }, addonVersion: '0.4.6-hotfix.1' };
  const manager = window.manager = {
    boot: async () => ok({ product: { name: 'DLSS 5 AI 超分管理器', edition: '装机宅版', author: '野生的装机宅', version: 'UI smoke' }, settings: {}, hardware: mock.data.hardware,
      payload: { ready: true, selectedVersion: '0.4.6-hotfix.1', versions: { '0.4.6-hotfix.1': { label: '0.4.6-hotfix.1', variants: { RTX40: { ready: true, files: [] }, RTX50: { ready: true, files: [] } } } } }, games: [game], addons: [] }),
    readNr: async () => ok({ AutoMask: 1, Style: 0, ColorStrength: 0.75, SkinStructureStrength: 1 }),
    diagnose: async () => ok({ components: [{ ok: true, label: '核心组件', detail: '测试桩' }] }),
    readHotkeys: async () => ok({ reshade: { key: 36 } }),
    fetchGameIcon: async () => ok(null), fetchGameArt: async () => ok(null),
    onAddonImported: () => {}, onLaunchSettingsApplied: callback => { mock.listener = callback; },
    inspectLaunchSettings: async id => { mock.calls.push(['inspect', id]); return ok(mock.data); },
    saveLaunchSettings: async (id, domain, request) => {
      mock.calls.push(['save', id, domain, request]);
      if (mock.failSave) return { ok: false, error: { code: 'SETTINGS_TEST_SAVE', message: '模拟保存失败' } };
      mock.data.requests[domain] = { exe: game.dir + '\\game.exe', request, savedAt: new Date().toISOString() };
      return ok({ saved: true, nextLaunch: true, applied: false });
    },
    previewLaunchSettings: async (id, domain, request) => {
      mock.calls.push(['preview', id, domain, request]);
      const plan = { id: 'plan-' + mock.calls.length, domain, request, backend: request.backend, exe: game.dir + '\\game.exe', destination: request.backend === 'native' || request.backend === 'nvidia' ? '每游戏驱动配置' : '现有后端配置文件', operations: [{ action: 'set-dword', id: 123, value: 1 }], warnings: [], blockers: mock.blockPreview ? ['模拟硬件阻止'] : [], runtimeVerified: false };
      mock.plans[plan.id] = plan;
      return ok(plan);
    },
    applyLaunchSettings: async (id, consent) => {
      mock.calls.push(['apply', id, consent]);
      if (mock.failApply) return { ok: false, error: { code: 'WRITE_VERIFY_FAILED', message: '模拟配置读回失败' } };
      const plan = mock.plans[id];
      if (!plan || consent.confirm !== true || plan.blockers.length) throw new Error('unconfirmed or blocked plan');
      mock.data.applied[plan.domain] = { request: plan.request, backend: plan.backend, readbackVerified: true, runtimeVerified: false };
      return ok({ applied: true, runtimeVerified: false });
    },
    restoreLaunchSettings: async (id, domain) => {
      mock.calls.push(['restore', id, domain]); delete mock.data.requests[domain]; delete mock.data.applied[domain];
      if (domain === 'sr' && mock.data.legacy) Object.assign(mock.data.legacy, { managed: true, baselineCaptured: false });
      return ok({ applied: true });
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
  await waitUntil(() => document.querySelector('.game-card-head'));
  document.querySelector('.game-card-head').click();
  const host = document.querySelector('.launch-settings-host');
  await host.launchSettingsController.ready;
  const controller = host.launchSettingsController;
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
  const hardware = async (series, family = series === 'RTX50' ? 'RTX50' : 'RTX40') => {
    mock.data.hardware = { family, series: series ? [series] : [], names: series ? [`NVIDIA GeForce ${series.replace('RTX', 'RTX ')}90`] : [], source: 'fixture' };
    mock.data.requests = {}; mock.data.applied = {};
    await controller.refresh(true);
  };

  assert(document.querySelectorAll('.sr-model-select').length === 0, 'Legacy SR selector is absent');
  assert(document.querySelectorAll('[aria-label="DLSS SR 模型"]').length === 0, 'Game-control default does not expose a duplicate model override');
  assert(document.querySelector('.game-version-select').textContent.includes('Beta'), 'Core Beta choice remains visible');
  assert(document.querySelector('.carrier-component-check').checked, 'DX11 compatibility bridge remains independent and enabled');
  mock.data.legacy = { selection: 'auto', effective: 'm', managed: false, baselineCaptured: true };
  await controller.refresh(true);
  assert(field('sr', 'quality').value === 'preserve' && field('sr', 'preset').value === 'M', 'Legacy automatic M populates the single SR editor without changing quality');
  assert(panel('sr').textContent.includes('沿用原模型设置：自动 → 模型 M') && !panel('sr').textContent.includes('未设启动覆盖'), 'Legacy launch behavior stays visible before ownership migration');
  await click('sr', 'restore');
  assert(field('sr', 'quality').value === 'game' && !panel('sr').textContent.includes('沿用原模型设置'), 'A restored and managed legacy policy cannot silently re-enable M');
  set('sr', 'quality', 'custom'); set('sr', 'renderPercent', 75); set('sr', 'preset', 'M');
  const beforeApply = count('apply');
  await click('sr', 'save');
  assert(saved('sr').renderPercent === 75 && saved('sr').preset === 'M', 'Native SR saves custom 75 percent and model M');
  assert(count('apply') === beforeApply, 'Save does not immediately apply');
  assert(!panel('sr').textContent.includes('配置已读回'), 'Save alone does not claim readback');
  set('sr', 'preset', 'K'); await click('sr', 'save');
  assert(saved('sr').preset === 'K', 'The same SR model control saves K');
  await click('sr', 'preview');
  assert(count('apply') === beforeApply && panel('sr').textContent.includes('目标 EXE'), 'Preview shows target and waits for confirmation');
  await click('sr', 'confirm');
  assert(count('apply') === beforeApply + 1, 'Confirmation applies exactly one preview');
  assert(mock.calls.filter(row => row[0] === 'apply').at(-1)[2].confirm === true, 'Application sends explicit confirmation');
  assert(panel('sr').textContent.includes('配置已读回') && panel('sr').textContent.includes('游戏未验证'), 'Readback and runtime status remain separate');
  await click('sr', 'preview'); set('sr', 'renderPercent', 76);
  assert(!panel('sr').querySelector('[data-ls-action="confirm"]'), 'Editing a number immediately invalidates the preview');
  set('sr', 'renderPercent', 75); await click('sr', 'restore');
  assert(!saved('sr') && field('sr', 'quality').value === 'game', 'Restore clears the saved request and returns SR to game control');

  set('sr', 'backend', 'optiscaler'); set('sr', 'quality', 'custom'); set('sr', 'renderPercent', 75);
  await click('sr', 'save');
  assert(saved('sr').backend === 'optiscaler' && saved('sr').renderPercent === 75 && !('preset' in saved('sr')), 'OptiScaler uses only its owned ratio fields');
  await click('sr', 'restore');
  for (const series of ['RTX40', 'RTX50']) {
    await hardware(series);
    const expected = series === 'RTX40' ? 'rtx40' : 'nvidia';
    set('fg', 'mode', 'fixed'); set('fg', 'multiplier', 6); await click('fg', 'save');
    assert(saved('fg').backend === expected && saved('fg').multiplier === 6, `${series} fixed 6x uses the correct backend`);
    assert(panel('fg').textContent.includes('1 帧渲染 + 5 帧生成'), `${series} multiplier includes the rendered frame`);
    await click('fg', 'preview'); await click('fg', 'confirm');
    assert(mock.data.applied.fg.request.multiplier === 6, `${series} fixed request applies after preview`);
    set('fg', 'mode', 'dynamic'); set('fg', 'targetFps', 144);
    if (series === 'RTX40') set('fg', 'experimental56', true);
    await click('fg', 'save'); await click('fg', 'preview'); await click('fg', 'confirm');
    assert(saved('fg').mode === 'dynamic' && saved('fg').targetFps === 144 && !('multiplier' in saved('fg')), `${series} dynamic mode omits dormant fixed multiplier`);
    assert(series === 'RTX40' ? saved('fg').experimental56 === true : !('experimental56' in saved('fg')), `${series} experimental option follows its backend schema`);
    await click('fg', 'restore');
    assert(!saved('fg') && !mock.data.applied.fg, `${series} restore clears the next launch request`);
  }
  await hardware('RTX30');
  assert(panel('fg').querySelector('[data-ls-action="save"]').disabled, 'RTX30 is not inferred as RTX40 from payload family');
  await hardware(null, 'unknown');
  assert(panel('fg').querySelector('[data-ls-action="preview"]').disabled && panel('fg').textContent.includes('未确认'), 'Unknown hardware cannot request FG');
  await hardware('RTX40');
  mock.data.requests.fg = { request: { backend: 'nvidia', mode: 'fixed', multiplier: 3 } };
  await controller.refresh(true);
  assert(panel('fg').querySelector('[data-ls-action="save"]').disabled && panel('fg').textContent.includes('不一致'), 'A saved FG backend for another GPU cannot be resubmitted');
  await click('fg', 'restore');
  set('sr', 'quality', 'quality');
  mock.failSave = true; await click('sr', 'save');
  assert(panel('sr').textContent.includes('模拟保存失败') && !saved('sr'), 'A failed save remains an error without a fake saved state');
  mock.failSave = false;
  mock.blockPreview = true; await click('sr', 'preview');
  assert(panel('sr').querySelector('[data-ls-action="confirm"]').disabled, 'Backend blockers disable application confirmation');
  mock.blockPreview = false; await click('sr', 'cancel');
  await click('sr', 'preview'); mock.failApply = true; await click('sr', 'confirm');
  assert(panel('sr').textContent.includes('模拟配置读回失败') && !mock.data.applied.sr, 'Readback failure is not reported as successful application');
  mock.failApply = false;
  mock.data.pending = [{ kind: 'file-journal' }]; await controller.refresh();
  assert(panel('sr').querySelector('fieldset').disabled && host.querySelector('[data-ls-action="recover"]'), 'An unfinished transaction exposes recovery and locks editing');
  await click(null, 'recover');
  assert(!panel('sr').querySelector('fieldset').disabled && mock.data.pending.length === 0, 'Recovery reloads and unlocks settings');
  mock.data.applied.sr = { request: { backend: 'native', quality: 'quality' }, readbackVerified: true };
  await mock.listener({ id: 'ui-fixture', outcomes: [{ domain: 'sr', applied: true }] });
  assert(panel('sr').textContent.includes('配置已读回'), 'Launch IPC completion refreshes the visible settings');

  set('sr', 'quality', 'custom'); set('sr', 'renderPercent', 75); set('sr', 'preset', 'M');
  set('fg', 'mode', 'dynamic'); set('fg', 'targetFps', 144);
  for (const node of document.querySelectorAll('.app-shell, main, .view.active, .game-card, .game-detail, .launch-settings-host, .launch-panel, .launch-field select, .launch-number input')) {
    const rect = node.getBoundingClientRect();
    assert(rect.left >= -1 && rect.right <= innerWidth + 1 && node.scrollWidth <= node.clientWidth + 1, `900px width contains ${node.className || node.tagName}`);
  }
  assert(!mock.error, 'No renderer error occurred');
  const view = document.querySelector('.view.active');
  view.scrollTop += host.getBoundingClientRect().top - view.getBoundingClientRect().top - 12;
  return { checks, viewport: { width: innerWidth, height: innerHeight, launchTop: host.getBoundingClientRect().top, scrollTop: document.querySelector('.view.active').scrollTop } };
}

const preload = path.join(temporary, 'preload.cjs');
fs.writeFileSync(preload, `(${installMock.toString()})();`, 'utf8');
app.whenReady().then(async () => {
  const width = Number(process.env.SRFG_UI_WIDTH) || 900;
  const win = new BrowserWindow({ width, height: 1100, useContentSize: true, show: false, webPreferences: { preload, sandbox: false, contextIsolation: false, nodeIntegration: false, backgroundThrottling: false, offscreen: true } });
  try {
    await win.loadFile(path.join(__dirname, '../src/renderer/index.html'));
    const result = await win.webContents.executeJavaScript(`(${smoke.toString()})()`);
    win.webContents.invalidate();
    await new Promise(resolve => setTimeout(resolve, 150));
    const screenshot = process.argv[2];
    if (screenshot) { fs.mkdirSync(path.dirname(path.resolve(screenshot)), { recursive: true }); fs.writeFileSync(path.resolve(screenshot), (await win.webContents.capturePage()).toPNG()); }
    console.log(JSON.stringify({ ok: true, ...result, screenshot: screenshot || null }, null, 2));
    win.destroy(); app.exit(0);
  } catch (error) { console.error(error.stack || error); win.destroy(); app.exit(1); }
});
