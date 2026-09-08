'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ui = require('../src/renderer/launch-settings-ui');
const policy = require('../src/product/launch-settings-policy');

test('renderer requests compile with the real policy and omit dormant fields', () => {
  for (const backend of ['native', 'optiscaler']) {
    for (const quality of ['game', 'dlaa', 'quality', 'balanced', 'performance', 'ultraPerformance', 'custom']) {
      const request = ui.createRequest('sr', { backend, quality, renderPercent: '75', preset: 'M' });
      assert.deepEqual(policy.validateRequest('sr', request), request);
      if (quality !== 'custom') assert.equal(request.renderPercent, undefined);
      if (backend === 'optiscaler' || quality === 'game') assert.equal(request.preset, undefined);
    }
  }
  assert.throws(() => ui.createRequest('sr', { backend: 'native', quality: 'custom', renderPercent: '75.5' }), /整数/);
  assert.throws(() => ui.createRequest('sr', { backend: 'optiscaler', quality: 'custom', renderPercent: '33.33' }), /33.334/);
  assert.deepEqual(ui.createRequest('sr', { backend: 'native', quality: 'preserve', preset: 'K' }), { backend: 'native', quality: 'preserve', preset: 'K' });
  for (const backend of ['nvidia', 'rtx40']) {
    for (const mode of ['restore', 'fixed', 'dynamic', backend === 'nvidia' ? 'off' : 'follow']) {
      const request = ui.createRequest('fg', { backend, mode, multiplier: '6', targetFps: '0', experimental56: true });
      assert.deepEqual(policy.validateRequest('fg', request), request);
      if (mode !== 'fixed') assert.equal(request.multiplier, undefined);
      if (mode !== 'dynamic') assert.equal(request.targetFps, undefined);
      if (backend === 'nvidia' || mode !== 'dynamic') assert.equal(request.experimental56, undefined);
    }
  }
  assert.throws(() => ui.createRequest('fg', { backend: 'rtx40', mode: 'off' }), /有效/);
  assert.throws(() => ui.createRequest('fg', { backend: 'nvidia', mode: 'fixed', multiplier: '7' }), /2–6/);
  assert.throws(() => ui.createRequest('fg', { backend: 'nvidia', mode: 'dynamic', targetFps: '' }), /整数/);
});

test('an installation family alone never grants FG capability', () => {
  assert.equal(ui.hardwareFacts({ family: 'RTX40' }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ family: 'RTX40', series: ['RTX30'] }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ family: 'RTX40', names: ['NVIDIA GeForce RTX 3080'] }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ family: 'RTX40', series: ['RTX40'], names: ['RTX 3090'] }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ family: 'mixed', series: ['RTX40', 'RTX50'] }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ series: ['RTX40'], source: 'unavailable' }).fgBackend, null);
  assert.equal(ui.hardwareFacts({ family: 'RTX40', series: ['RTX40'], names: ['NVIDIA GeForce RTX 4090'] }).fgBackend, 'rtx40');
  assert.equal(ui.hardwareFacts({ family: 'RTX50', series: ['RTX50'] }).fgBackend, 'nvidia');
});

test('saving, previewing and confirming are separate IPC operations; errors remain errors', async () => {
  const calls = [];
  const data = { hardware: { series: ['RTX40'] }, requests: {}, applied: {}, pending: [] };
  const host = { isConnected: true, innerHTML: '', querySelector: () => null };
  let failSave = false;
  const manager = {
    inspectLaunchSettings: async () => ({ ok: true, value: structuredClone(data) }),
    saveLaunchSettings: async (id, domain, request) => {
      calls.push(['save', id, domain, request]);
      if (failSave) return { ok: false, error: { code: 'SETTINGS_DISK', message: '写入失败' } };
      data.requests[domain] = { exe: 'C:\\Games\\A.exe', request };
      return { ok: true, value: { saved: true, applied: false } };
    },
    previewLaunchSettings: async (id, domain, request) => { calls.push(['preview', id, domain, request]); return { ok: true, value: { id: 'plan-1', domain, request, blockers: [] } }; },
    applyLaunchSettings: async (id, confirm) => { calls.push(['apply', id, confirm]); return { ok: true, value: { applied: true } }; },
    restoreLaunchSettings: async (id, domain) => { calls.push(['restore', id, domain]); delete data.requests[domain]; delete data.applied[domain]; return { ok: true, value: { restored: true } }; }
  };
  const controller = ui.mount(host, 'game-1', { manager });
  await controller.ready;
  Object.assign(controller.getState().drafts.sr, { quality: 'custom', renderPercent: 75, preset: 'M' });
  await controller.perform('save', 'sr');
  assert.deepEqual(calls.map(row => row[0]), ['save']);
  assert.match(host.innerHTML, /请求已保存/);
  assert.doesNotMatch(host.innerHTML, /配置已读回/);
  await controller.perform('preview', 'sr');
  assert.deepEqual(calls.map(row => row[0]), ['save', 'preview']);
  assert.match(host.innerHTML, /确认应用/);
  await controller.perform('confirm', 'sr');
  assert.deepEqual(calls.at(-1), ['apply', 'plan-1', { confirm: true }]);
  await controller.perform('preview', 'sr');
  controller.getState().drafts.sr.renderPercent = 76;
  await controller.perform('confirm', 'sr');
  assert.equal(calls.filter(row => row[0] === 'apply').length, 1);
  assert.match(host.innerHTML, /设置已改变/);
  failSave = true;
  await controller.perform('save', 'sr');
  assert.match(host.innerHTML, /写入失败 \[SETTINGS_DISK\]/);
  assert.doesNotMatch(host.innerHTML, /请求已保存，下次从管理器启动/);
  await controller.perform('restore', 'sr');
  assert.equal(data.requests.sr, undefined);
  assert.equal(controller.getState().drafts.sr.quality, 'game');
  assert.match(host.innerHTML, /取消下次启动覆盖/);
});

test('readback and runtime are distinct and backend text is escaped', () => {
  const applied = { sr: { request: { backend: 'native', quality: 'quality' }, runtimeVerified: true } };
  assert.doesNotMatch(ui.statusMarkup('sr', { applied }, false), /配置已读回/);
  applied.sr.readbackVerified = true;
  const status = ui.statusMarkup('sr', { applied }, false);
  assert.match(status, /配置已读回/);
  assert.match(status, /游戏未验证/);
  const preview = ui.previewMarkup({ domain: 'sr', plan: { id: '1', request: { backend: 'native', quality: 'game' }, warnings: ['<img src=x onerror=1>'], blockers: ['不能应用'] } });
  assert.match(preview, /&lt;img/);
  assert.match(preview, /data-ls-action="confirm" disabled/);
});

test('legacy SR choices stay visible until this editor takes ownership', () => {
  const data = { requests: {}, applied: {}, legacy: { selection: 'auto', effective: 'm', managed: false, baselineCaptured: true } };
  assert.equal(ui.initialFields('sr', data).quality, 'preserve');
  assert.equal(ui.initialFields('sr', data).preset, 'M');
  const status = ui.statusMarkup('sr', data, false);
  assert.match(status, /沿用原模型设置：自动 → 模型 M/);
  assert.match(status, /保存本页后接管/);
  assert.doesNotMatch(status, /未设启动覆盖|配置已读回/);
  data.legacy.managed = true;
  assert.equal(ui.initialFields('sr', data).quality, 'game');
  assert.doesNotMatch(ui.statusMarkup('sr', data, false), /沿用原模型设置/);
  data.legacy.managed = false;
  data.requests.sr = { request: { backend: 'native', quality: 'custom', renderPercent: 75, preset: 'K' } };
  assert.equal(ui.initialFields('sr', data).preset, 'K');
  assert.doesNotMatch(ui.statusMarkup('sr', data, false), /沿用原模型设置/);
  delete data.requests.sr;
  data.legacy.effective = 'default';
  assert.equal(ui.initialFields('sr', data).quality, 'game');
  data.legacy.error = { message: '读取失败' };
  assert.match(ui.statusMarkup('sr', data, false), /启动覆盖状态未确认/);
});

test('the new module replaces the legacy writable SR widget and preserves core controls', () => {
  const html = fs.readFileSync(path.join(__dirname, '../src/renderer/index.html'), 'utf8');
  const renderer = fs.readFileSync(path.join(__dirname, '../src/renderer/renderer.js'), 'utf8');
  assert.match(html, /src="launch-settings-ui.js"/);
  assert.doesNotMatch(html, /src="sr-model-ui.js"/);
  assert.match(renderer, /launchSettingsUi.mount/);
  assert.match(renderer, /carrier-component-check/);
  assert.match(renderer, /核心版本/);
});

test('RTX40 preparation is an explicit usable action and RTX50 selects the native route', async () => {
  const data = { hardware: { family: 'RTX40', series: ['RTX40'] }, requests: {}, applied: {}, pending: [],
    fgComponents: { route: 'compatibility', ready: false, canPrepare: true, missing: ['兼容插件', '加载器'], blockers: [] } };
  const host = { innerHTML: '', isConnected: true, querySelector: () => null };
  const calls = [];
  const manager = { inspectLaunchSettings: async () => ({ ok: true, value: structuredClone(data) }),
    prepareFgComponents: async (id, options) => { calls.push([id, options]); data.fgComponents.ready = true; return { ok: true, value: { prepared: true } }; } };
  const controller = ui.mount(host, 'game', { manager }); await controller.ready;
  assert.match(host.innerHTML, /准备兼容组件/);
  assert.doesNotMatch(host.innerHTML, /data-ls-field="mode"/);
  await controller.perform('prepare', 'fg');
  assert.deepEqual(calls, [['game', { allowAntiCheat: false }]]);
  assert.match(host.innerHTML, /data-ls-field="mode"/);
  const native = ui.fgComponentMarkup({ hardware: { family: 'RTX50', series: ['RTX50'] }, fgComponents: { route: 'native', ready: true } }, false);
  assert.match(native, /RTX 50 · 原生帧生成/);
  assert.doesNotMatch(native, /准备兼容组件/);
});
