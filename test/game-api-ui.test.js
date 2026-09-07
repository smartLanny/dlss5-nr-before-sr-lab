'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/renderer/renderer.js'), 'utf8');
const product = JSON.parse(fs.readFileSync(path.join(__dirname, '../product.json'), 'utf8'));

test('DXGI awaiting confirmation is never labeled temporarily unsupported', () => {
  const context = {}; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function supportBadge('), source.indexOf('function hardwareLabel(')), context);
  context.game = { supported: false, installed: false, supportCode: 'ERR_API_SELECTION_REQUIRED' };
  const html = vm.runInContext('supportBadge(game)', context);
  assert.match(html, /API 待确认/); assert.doesNotMatch(html, /暂不支持/);
});

test('carrier checkbox sits beside the core version and exposes the full route warning', () => {
  const context = { escapeHtml: String, state: { payload: { selectedVersion: '0.4.5-ota' } }, versionOptionsMarkup: () => '<option value="0.4.5-ota">0.4.5</option>' }; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('const CARRIER_TOOLTIP'), source.indexOf('function gameApiLabel(')), context);
  context.game = { chosen: { apiResolution: { api: 'dx11', evidence: [] } }, components: { dx11Carrier: false } };
  let html = vm.runInContext('addonVersionRow(game)', context);
  assert.equal((html.match(/carrier-component-check/g) || []).length, 1);
  assert.match(html, /兼容桥接（自动）/); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/);
  assert.match(html, /当前仅支持 DX11/); assert.match(html, /完整配套/);
  assert.match(html, /关闭后 NR 路线不可用/); assert.match(html, /不修改游戏启动 API/);
  assert.match(html, /游戏或启动器/); assert.match(html, /DX12、API 未确认或 Vulkan 不部署 DX11 carrier/);
  context.game.components.dx11Carrier = true;
  html = vm.runInContext('addonVersionRow(game)', context); assert.match(html, /carrier-component-check[^>]*checked/);
  context.game.chosen.apiResolution.api = 'dx12'; context.game.components.dx11Carrier = true;
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /carrier-component-check[^>]*disabled/); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/);
  context.game.chosen.apiResolution.api = 'unknown';
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /carrier-component-check[^>]*disabled/); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/);
  context.game.chosen.apiResolution.api = 'unsupported'; context.game.chosen.apiLabel = 'Vulkan';
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /Vulkan/); assert.match(html, /carrier-component-check[^>]*disabled/);
});

test('API selection stays separate and uses the anti-cheat-aware API call', () => {
  const context = { escapeHtml: String }; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function apiControls('), source.indexOf('function addonVersionRow(')), context);
  context.game = { chosen: { apiResolution: { api: 'dx12', evidence: [] } }, components: { dx11Carrier: true } };
  const html = vm.runInContext('apiControls(game)', context);
  assert.match(html, /game-api-select/); assert.doesNotMatch(html, /carrier-component-check/);
  assert.match(html, /DX12 不使用 DX11 carrier/);
  context.game.chosen.apiResolution.api = 'unknown';
  assert.match(vm.runInContext('apiControls(game)', context), /Vulkan/);
  assert.match(source, /window\.manager\.setGameApi\(id, api, \{ allowAntiCheat \}\)/);
});

test('product metadata exposes the second QQ group number', () => {
  assert.equal(product.qqGroup, '392308850');
});
