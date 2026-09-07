'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/renderer/renderer.js'), 'utf8');

test('DXGI awaiting confirmation is never labeled temporarily unsupported', () => {
  const context = {}; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function supportBadge('), source.indexOf('function hardwareLabel(')), context);
  context.game = { supported: false, installed: false, supportCode: 'ERR_API_SELECTION_REQUIRED' };
  const html = vm.runInContext('supportBadge(game)', context);
  assert.match(html, /API 待确认/); assert.doesNotMatch(html, /暂不支持/);
});

test('carrier checkbox reflects independent choice and cannot enable the DX12 carrier', () => {
  const context = { escapeHtml: String }; vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function apiControls('), source.indexOf('function gameApiLabel(')), context);
  context.game = { chosen: { apiResolution: { api: 'dx11', evidence: [] } }, components: { dx11Carrier: false } };
  let html = vm.runInContext('apiControls(game)', context);
  assert.match(html, /carrier-component-check/); assert.doesNotMatch(html, /type="checkbox" checked/);
  context.game.components.dx11Carrier = true;
  html = vm.runInContext('apiControls(game)', context); assert.match(html, /type="checkbox" checked/);
  context.game.chosen.apiResolution.api = 'dx12'; context.game.components.dx11Carrier = false;
  html = vm.runInContext('apiControls(game)', context); assert.match(html, /type="checkbox" disabled/);
});
