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

function uiContext() {
  const context = { escapeHtml: String, state: { addons: [], payload: {
    selectedVersion: '0.4.6-hotfix.1',
    versions: { '0.2.0-beta.2': {}, '0.3.3.5': {}, '0.4.6-hotfix.1': {} },
    bundle: { supersededVersions: { '0.4.6': '0.4.6-hotfix.1' } }
  } } };
  vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function coreVersionLabel('), source.indexOf('function poster(')), context);
  vm.runInContext(source.slice(source.indexOf('const API_LABELS'), source.indexOf('function gameDetail(')), context);
  return context;
}

test('automatic API label retains detection while a manual override controls the route', () => {
  const context = uiContext();
  context.game = { apiOverride: 'dx11', chosen: { path: 'C:/Games/Current.exe', detectedApi: 'dx12',
    detectedApiResolution: { api: 'dx12', source: 'imports', evidence: ['d3d12.dll'] },
    apiResolution: { api: 'dx11', source: 'override' } } };
  let html = vm.runInContext('apiControls(game)', context);
  assert.match(html, /value="auto">DirectX 12（自动）/);
  assert.match(html, /value="dx11" selected/);
  assert.match(html, /兼容桥接随 DirectX 11 自动部署/);
  assert.match(html, /检测线索：d3d12\.dll/);
  assert.match(html, /不会更换游戏程序或修改启动器参数/);
  assert.match(html, /<details class="api-help">/); assert.doesNotMatch(html, /<details[^>]*open/);
  assert.doesNotMatch(html, /carrier-component-check/);
  context.game.chosen.detectedApiResolution = { api: 'unknown', source: 'none', evidence: [] };
  html = vm.runInContext('apiControls(game)', context);
  assert.match(html, /value="auto">API 待确认（自动）/);
  assert.match(html, /value="dx11" selected/);
  delete context.game.chosen.detectedApiResolution; delete context.game.chosen.detectedApi;
  assert.match(vm.runInContext('apiControls(game)', context), /value="auto">API 待确认（自动）/);
});

test('manual API choices retain their names and unsupported NR routes do not offer a bridge switch', () => {
  const context = uiContext();
  for (const [api, label] of [['dx9', 'DirectX 9'], ['dx10', 'DirectX 10'], ['vulkan', 'Vulkan'], ['opengl', 'OpenGL']]) {
    context.game = { apiOverride: api, chosen: { detectedApi: 'dx12', apiResolution: { api, source: 'override' } } };
    const html = vm.runInContext('apiControls(game)', context);
    assert.ok(html.includes(`value="${api}" selected`));
    assert.match(html, /当前 API 暂不支持 NR，不部署兼容桥接/);
    assert.equal(vm.runInContext('gameApiLabel(game.chosen)', context), `${label}（已指定）`);
    for (const choice of ['auto', 'dx9', 'dx10', 'dx11', 'dx12', 'vulkan', 'opengl']) assert.ok(html.includes(`value="${choice}"`));
  }
  assert.doesNotMatch(source, /carrier-component-check|carrier-save-btn|setGameCarrier/);
  assert.doesNotMatch(fs.readFileSync(path.join(__dirname, '../main.js'), 'utf8'), /game-carrier-set/);
  assert.doesNotMatch(fs.readFileSync(path.join(__dirname, '../preload.js'), 'utf8'), /game-carrier-set|setGameCarrier/);
});

test('saving each API calls the current game route without replacing its selected EXE', () => {
  const calls = [], button = {}, head = {}, select = { value: 'auto' };
  const game = { id: 'current-game', chosen: { path: 'C:/Games/Chosen.exe' } };
  const card = { dataset: { id: game.id }, querySelector: selector => ({ '.game-card-head': head, '.api-save-btn': button, '.game-api-select': select })[selector] || null };
  const context = { state: { games: [game] }, document: { querySelectorAll: selector => selector === '.game-card' ? [card] : [] },
    window: { manager: { setGameApi: (...args) => calls.push(args) } }, runConfirmedAction: work => work(true) };
  vm.createContext(context);
  vm.runInContext(source.slice(source.indexOf('function bindGameCards('), source.indexOf('async function loadExpanded(')), context);
  vm.runInContext('bindGameCards()', context);
  for (const api of ['auto', 'dx9', 'dx10', 'dx11', 'dx12', 'vulkan', 'opengl']) {
    select.value = api; button.onclick({ stopPropagation() {} });
    assert.deepEqual(JSON.parse(JSON.stringify(calls.at(-1))), ['current-game', api, { allowAntiCheat: true }]);
    assert.equal(game.chosen.path, 'C:/Games/Chosen.exe');
  }
});

test('retired catalog selection offers its replacement while preserving the actual installed version', () => {
  const context = uiContext();
  assert.equal(vm.runInContext("coreVersionLabel('0.2.0-beta.2')", context), '0.2.0');
  assert.equal(vm.runInContext("coreVersionLabel('0.3.3.5')", context), '0.3.3.5 · 稳定基线');
  assert.equal(vm.runInContext("coreVersionLabel('0.4.6-hotfix.1')", context), '0.4.6-hotfix.1 · Beta');
  context.game = { installed: true, addonVersion: '0.4.6' };
  const html = vm.runInContext('addonVersionRow(game, true)', context);
  assert.match(html, /已安装：0\.4\.6 · Beta/);
  assert.match(html, /可更新为 0\.4\.6-hotfix\.1 · Beta/);
  assert.match(html, /点击“应用版本”或“修复”后才会更换核心/);
  assert.match(html, /value="0\.4\.6-hotfix\.1" selected/);
  assert.doesNotMatch(html, /value="0\.4\.6"/);
  assert.equal(context.game.addonVersion, '0.4.6');
  assert.match(vm.runInContext("actionSuccessMessage('修复完成', {payloadReplacement:{from:'0.4.6',to:'0.4.6-hotfix.1'}})", context), /已应用替代核心 0\.4\.6-hotfix\.1/);
  assert.equal(vm.runInContext("actionSuccessMessage('修复完成', {})", context), '修复完成');
  const installer = fs.readFileSync(path.join(__dirname, '../src/product/installer.js'), 'utf8');
  vm.runInContext(installer.slice(installer.indexOf('const coreVersionText'), installer.indexOf('const { scanConflicts')), context);
  assert.equal(vm.runInContext("coreVersionText('0.2.0-beta.2')", context), '0.2.0');
  assert.equal(vm.runInContext("coreVersionText('0.3.3.5')", context), '0.3.3.5 · 稳定基线');
});

test('product metadata exposes the second QQ group number', () => {
  assert.equal(product.qqGroup, '392308850');
});
