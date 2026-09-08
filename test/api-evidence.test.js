'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { annotateApi } = require('../src/product/api-evidence');

function fakePe(files) {
  return {
    virtual: true,
    getImports(file) { return files[file] && files[file].imports || []; },
    findMarkers(file) { return new Set(files[file] && files[file].markers || []); }
  };
}
function scan(api = 'dxgi') {
  return { chosen: { path: 'C:/games/demo/game.exe', api, apiLabel: 'old label', dx12: false } };
}

test('static DX12 evidence resolves DX12 and preserves existing fields', () => {
  const out = annotateApi(scan(), 'C:/games/demo', { pe: fakePe({ 'C:/games/demo/game.exe': { imports: ['d3d12.dll'] } }) });
  assert.equal(out.chosen.apiResolution.api, 'dx12');
  assert.equal(out.chosen.apiLabel, 'old label');
  assert.match(out.chosen.apiResolution.evidence[0], /d3d12/);
});

test('single DX11 evidence remains unknown', () => {
  const out = annotateApi(scan(), 'C:/games/demo', { pe: fakePe({ 'C:/games/demo/game.exe': { imports: ['d3d11.dll'] } }) });
  assert.equal(out.chosen.apiResolution.api, 'unknown');
});

test('DX11 EXE plus DX12 engine module is mixed', () => {
  const out = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ 'C:/games/demo/game.exe': { imports: ['d3d11.dll'] }, 'C:/games/demo/engine.dll': { markers: ['D3D12CreateDevice'] } }),
    engineModules: [{ path: 'C:/games/demo/engine.dll' }]
  });
  assert.equal(out.chosen.apiResolution.api, 'mixed');
});

test('documented BG3 entry points require Steam identity and exact path, while manual API stays independent', () => {
  const root='C:/games/bg3', vk=root+'/bin/bg3.exe', dx=root+'/bin/bg3_dx11.exe';
  const pe=fakePe({[vk]:{},[dx]:{imports:['d3d11.dll']}});
  const scanFor=exe=>({chosen:{path:exe,api:'dxgi',apiLabel:''}});
  for(const [exe,api] of [[vk,'vulkan'],[dx,'dx11']]) {
    const result=annotateApi(scanFor(exe),root,{pe,steamAppId:'1086940'});
    assert.equal(result.chosen.detectedApi,api);assert.equal(result.chosen.detectedApiResolution.source,'game-entry');
  }
  assert.equal(annotateApi(scanFor(dx),root,{pe,steamAppId:'999'}).chosen.detectedApi,'unknown');
  assert.equal(annotateApi(scanFor(vk),root,{pe,steamAppId:'1086940',logEvidence:{bound:false,apis:['dx12'],evidence:'旧 carrier 创建过 DX12 设备'}}).chosen.detectedApi,'vulkan');
  assert.equal(annotateApi(scanFor(root+'/other/bg3.exe'),root,{pe,steamAppId:'1086940'}).chosen.detectedApi,'unknown');
  const override=annotateApi(scanFor(vk),root,{pe,steamAppId:'1086940',apiOverride:'dx11'});
  assert.equal(override.chosen.detectedApi,'vulkan');assert.equal(override.chosen.apiResolution.api,'dx11');
});

test('a DX12 game importing dual-API AMD AGS stays DX12; real engine conflicts remain mixed', () => {
  const exe='C:/games/demo/game.exe', sdk='C:/games/demo/amd_ags_x64.dll', engine='C:/games/demo/render_engine.dll';
  const pe=fakePe({[exe]:{imports:['d3d12.dll','amd_ags_x64.dll']},[sdk]:{markers:['D3D11CreateDevice','D3D12CreateDevice']},[engine]:{imports:['d3d11.dll']}});
  const result=annotateApi(scan(),'C:/games/demo',{pe,engineModules:[sdk]});
  assert.equal(result.chosen.apiResolution.api,'dx12');
  assert.match(result.chosen.apiResolution.evidence.join(' '),/通用显卡 SDK/);
  assert.equal(annotateApi(scan(),'C:/games/demo',{pe,engineModules:[sdk,engine]}).chosen.apiResolution.api,'mixed');
});

test('explicit override wins without changing original API fields', () => {
  const out = annotateApi(scan(), 'C:/games/demo', { apiOverride: 'dx11', pe: fakePe({}) });
  assert.equal(out.chosen.apiResolution.api, 'dx11');
  assert.equal(out.chosen.api, 'dxgi');
  assert.equal(out.chosen.apiLabel, 'old label');
});

test('carrier/proxy module names are excluded from engine evidence', () => {
  const out = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ 'C:/games/demo/nrchain_nvngx.dll': { imports: ['d3d12.dll'] }, 'C:/games/demo/real-engine.dll': {} }),
    engineModules: [{ path: 'C:/games/demo/nrchain_nvngx.dll' }]
  });
  assert.equal(out.chosen.apiResolution.api, 'unknown');
});

test('unbound runtime log and launch argument remain clues', () => {
  const out = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ 'C:/games/demo/game.exe': {} }),
    launchArguments: '--renderer dx11',
    logEvidence: { exe: 'C:/other/game.exe', api: 'dx12', bound: false }
  });
  assert.equal(out.chosen.apiResolution.api, 'unknown');
  assert.ok(out.chosen.apiResolution.evidence.some(value => /线索|绑定/.test(value)));
});

test('only rigorously bound runtime log can resolve DX12', () => {
  const out = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ 'C:/games/demo/game.exe': {} }),
    logEvidence: { exe: 'C:/games/demo/game.exe', api: 'dx12', bound: true }
  });
  assert.equal(out.chosen.apiResolution.api, 'dx12');
  assert.equal(out.chosen.apiResolution.source, 'runtime-log');
});

test('known incompatible API remains explicit and separate from install support', () => {
  const out = annotateApi(scan('vulkan'), 'C:/games/demo', { pe: fakePe({}) });
  assert.equal(out.chosen.apiResolution.api, 'vulkan');
  assert.equal(out.chosen.detectedApi, 'vulkan');
});

test('raw API names and flags cannot hide mixed evidence or prove a DX11 import route', () => {
  for (const fields of [{ api: 'dx11', dx12: true }, { api: 'dx12', dx11: true }, { apiLabel: 'DX11 / DX12' }]) {
    const input = scan(); Object.assign(input.chosen, fields);
    const out = annotateApi(input, 'C:/games/demo', { pe: fakePe({}) });
    assert.equal(out.chosen.apiResolution.api, 'mixed');
  }
  const out = annotateApi(scan('dx11'), 'C:/games/demo', { pe: fakePe({}) });
  assert.equal(out.chosen.apiResolution.api, 'unknown');
});

test('manual route keeps the independently detected API evidence', () => {
  const out = annotateApi(scan(), 'C:/games/demo', { apiOverride: 'opengl', pe: fakePe({ 'C:/games/demo/game.exe': { imports: ['d3d12.dll'] } }) });
  assert.equal(out.chosen.apiResolution.api, 'opengl'); assert.equal(out.chosen.detectedApiResolution.api, 'dx12');
  assert.equal(out.chosen.detectedApi, 'dx12'); assert.equal(out.chosen.apiResolution.source, 'override');
});

test('default PE reader handles a real bounded temporary fixture', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'api-evidence-'));
  const exe = path.join(dir, 'game.exe');
  fs.writeFileSync(exe, Buffer.alloc(128, 0));
  const out = annotateApi({ chosen: { path: exe, api: 'dxgi', apiLabel: '' } }, dir);
  assert.equal(out.chosen.apiResolution.api, 'unknown');
  fs.rmSync(dir, { recursive: true, force: true });
});

test('launch argument conflict is conservatively mixed', () => {
  const out = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ 'C:/games/demo/game.exe': { imports: ['d3d12.dll'] } }),
    launchArguments: '--renderer d3d11'
  });
  assert.equal(out.chosen.apiResolution.api, 'mixed');
});

test('combined labels alone never become pure DX11 and unknown stays selectable', () => {
  const mixed = scan(); mixed.chosen.apiLabel = 'DirectX 11/12';
  assert.equal(annotateApi(mixed, 'C:/games/demo', { pe: fakePe({}) }).chosen.apiResolution.api, 'mixed');
  assert.equal(annotateApi(scan('unknown'), 'C:/games/demo', { pe: fakePe({}) }).chosen.apiResolution.api, 'unknown');
});

test('default engine discovery checks a renderer even when EXE already hints DX11', t => {
  const fs = require('fs'), path = require('path'), os = require('os');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'api-engine-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const exe = path.join(root, 'game.exe'), engine = path.join(root, 'render_engine.dll');
  fs.writeFileSync(exe, 'fixture'); fs.writeFileSync(engine, 'fixture');
  const input = { chosen: { path: exe, api: 'dxgi', apiLabel: 'DirectX 11', via: 'imports' } };
  const output = annotateApi(input, root, { pe: fakePe({ [exe]: { imports: ['d3d11.dll'] }, [engine]: { imports: ['d3d12.dll'] } }) });
  assert.equal(output.chosen.apiResolution.api, 'mixed');
  assert.ok(output.chosen.apiResolution.evidence.some(line => line.includes('render_engine.dll')));
});

test('actual ReShade header is EXE-bound but device creation is only historical evidence', t => {
  const fs = require('fs'), path = require('path'), os = require('os');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'api-log-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const exe = path.join(root, 'game.exe'); fs.writeFileSync(exe, 'fixture');
  const log = path.join(root, 'ReShade.log');
  fs.writeFileSync(log, `Initializing crosire's ReShade version '6.8.0.0' loaded from 'dxgi.dll' into '${exe}' ...\nRedirecting D3D12CreateDevice(...)\n`);
  const input = { chosen: { path: exe, api: 'dxgi', apiLabel: 'DirectX 11', via: 'imports' } };
  const output = annotateApi(input, root, { pe: fakePe({}) });
  assert.equal(output.chosen.apiResolution.api, 'unknown');
  assert.ok(output.chosen.apiResolution.evidence.some(line => /历史.*dx12/.test(line)));
  fs.writeFileSync(log, `Initializing crosire's ReShade version '6.8.0.0' into '${path.join(root, 'other.exe')}' ...\nRedirecting D3D12CreateDevice(...)\n`);
  assert.equal(annotateApi(input, root, { pe: fakePe({}) }).chosen.apiResolution.api, 'unknown');
});

test('unbound historical multi-device logs cannot downgrade a DX12 game to mixed', () => {
  const exe = 'C:/games/demo/game.exe';
  const result = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ [exe]: { imports: ['d3d12.dll'] } }),
    logEvidence: { exe, bound: false, apis: ['dx11', 'dx12'], evidence: '历史日志创建过 DX11/DX12 设备' }
  });
  assert.equal(result.chosen.detectedApi, 'dx12');
  assert.ok(result.chosen.detectedApiResolution.evidence.includes('历史日志创建过 DX11/DX12 设备'));
  const conflicting = annotateApi(scan(), 'C:/games/demo', {
    pe: fakePe({ [exe]: { imports: ['d3d12.dll'] } }), logEvidence: { exe, bound: true, api: 'dx11', apis: ['dx11'] }
  });
  assert.equal(conflicting.chosen.detectedApi, 'mixed', 'a rigorously bound real conflict still blocks automatic selection');
});
