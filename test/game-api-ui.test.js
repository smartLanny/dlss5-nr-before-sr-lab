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

test('carrier is a separate row, defaults on only for DX11 and preserves an explicit off choice', () => {
  const context = { escapeHtml: String, state: { payload: { selectedVersion: '0.4.5-ota' } }, versionOptionsMarkup: () => '<option value="0.4.5-ota">0.4.5</option>' }; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('const CARRIER_TOOLTIP'), source.indexOf('function gameApiLabel(')), context);
  context.game = { chosen: { apiResolution: { api: 'dx11', evidence: [] } } };
  let html = vm.runInContext('addonVersionRow(game)', context);
  assert.equal((html.match(/carrier-component-check/g) || []).length, 1);
  assert.match(html, /addon-version-row[\s\S]*<\/div><div class="control-row carrier-control-row"/);
  assert.match(html, /carrier-component-check[^>]*checked/); assert.match(html, /已开启/);
  assert.match(html, /目前只用于 DirectX 11/); assert.match(html, /完整匹配/);
  assert.match(html, /关闭后这条 NR 路线不可用/); assert.match(html, /不会替你切换游戏的启动 API/);
  context.game.components = { dx11Carrier: false };
  html = vm.runInContext('addonVersionRow(game)', context); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/); assert.match(html, /已关闭/);
  context.game.chosen.apiResolution.api = 'dx12'; context.game.components.dx11Carrier = true;
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /carrier-component-check[^>]*disabled/); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/); assert.match(html, /DX12 无需开启/);
  context.game.chosen.apiResolution.api = 'unknown';
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /carrier-component-check[^>]*disabled/); assert.doesNotMatch(html, /carrier-component-check[^>]*checked/); assert.match(html, /确认 API 后可用/);
  context.game.chosen.apiResolution.api = 'unsupported'; context.game.chosen.apiLabel = 'Vulkan';
  html = vm.runInContext('addonVersionRow(game)', context);
  assert.match(html, /Vulkan/); assert.match(html, /当前 API 没有配套桥接/); assert.match(html, /carrier-component-check[^>]*disabled/);
});

test('API selection stays separate and uses the anti-cheat-aware API call', () => {
  const context = { escapeHtml: String }; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function apiControls('), source.indexOf('function addonVersionRow(')), context);
  context.game = { chosen: { apiResolution: { api: 'dx12', evidence: [] } }, components: { dx11Carrier: true } };
  const html = vm.runInContext('apiControls(game)', context);
  assert.match(html, /game-api-select/); assert.doesNotMatch(html, /carrier-component-check/);
  assert.match(html, /DX12 不启用兼容桥接/); assert.match(html, /<details class="api-help">/); assert.doesNotMatch(html, /<details[^>]*open/);
  context.game.chosen.apiResolution.api = 'unknown';
  assert.match(vm.runInContext('apiControls(game)', context), /Vulkan/);
  assert.match(source, /window\.manager\.setGameApi\(id, api, \{ allowAntiCheat \}\)/);
});

test('product metadata exposes the second QQ group number', () => {
  assert.equal(product.qqGroup, '392308850');
});
