'use strict';

const fs = require('fs');
const path = require('path');
const { createRdr2ApiSettings } = require('./rdr2-api-settings');

const MAX_ENGINE_MODULES = 8;
const MAX_MARKER_BYTES = 32 * 1024 * 1024;
const MAX_ENGINE_BYTES = 32 * 1024 * 1024;
const PROXY_OR_CARRIER = /^(?:d3d11|d3d12|dxgi|dinput8|nvngx(?:_[^.]*)?|nrchain(?:_[^.]*)?|sl\.[^.]*)\.(?:dll|addon64|addon32|addon)$/i;
// AGS exports support for both D3D11 and D3D12. Its capabilities do not tell
// which API the importing game actually uses (observed in 007 First Light).
const GENERIC_GRAPHICS_SDK = /^(?:amd_ags(?:_x64|_x86)?|nvapi(?:64)?)\.dll$/i;
// Larian's documented renderer-specific entry points. This is restricted to
// the Steam application identity and exact relative path, never basename alone.
// https://forums.larian.com/ubbthreads.php?Number=818949&ubb=showthreaded
const STEAM_ENTRY_APIS = { '1086940': { 'bin/bg3.exe': 'vulkan', 'bin/bg3_dx11.exe': 'dx11' }, '3764200': { 're9.exe': 'dx12' } };
// Capcom's Steam requirements specify DirectX 12 for Requiem. Its actual
// re9.exe also imports D3D11, so that static capability is not a renderer choice.
// https://help.steampowered.com/en/wizard/HelpWithGameTechnicalIssue/?appid=3764200
const STEAM_STATIC_EXCEPTIONS = { '3764200': { 're9.exe': ['dx11'] } };
const STEAM_CONFIG_ENTRIES = { '1174180': ['rdr2.exe'] };
// Rockstar documents this saved per-user choice for Vulkan / DirectX 12.
// https://support.rockstargames.com/articles/2MQafAtBWiOUJrPDLx5I2U/err-gfx-init-error-when-launching-red-dead-redemption-2-on-pc

// A manual selection may use the binary folder as its installation root.
// Recover documented entry identity only from an exact path in a discovered
// Steam installation, without changing the selected EXE or recovery root.
function steamEntryContext(exe, games) {
  if (!exe) return {};
  const matches = [];
  for (const game of games || []) {
    if (game.launcher !== 'Steam' || !game.dir) continue;
    const entries = [...Object.keys(STEAM_ENTRY_APIS[String(game.id)] || {}), ...(STEAM_CONFIG_ENTRIES[String(game.id)] || [])];
    if (entries.some(rel => samePath(path.resolve(game.dir, rel), exe))) {
      matches.push({ steamAppId: String(game.id), entryRoot: game.dir });
    }
  }
  const identities = new Set(matches.map(row => `${row.steamAppId}:${path.resolve(row.entryRoot).toLowerCase()}`));
  return identities.size === 1 ? matches[0] : {};
}

function readRdr2GraphicsApi(exe, context) {
  const result = createRdr2ApiSettings({ documentsDir: context.documentsDir }).read({ ...context, exe });
  return result.api ? { api: result.api, file: result.file, evidence: `RDR2 当前保存的 system.xml 图形设置选择 ${result.api === 'dx12' ? 'DirectX 12' : 'Vulkan'}` } : null;
}

function text(value) { return String(value == null ? '' : value); }

function normalizeApi(value) {
  const v = text(value).toLowerCase().replace(/[^a-z0-9]/g, '');
  if (v === 'dx9' || v === 'directx9' || v === 'd3d9') return 'dx9';
  if (v === 'dx10' || v === 'directx10' || v === 'd3d10' || v === 'd3d101') return 'dx10';
  if (v === 'dx11' || v === 'directx11' || v === 'd3d11') return 'dx11';
  if (v === 'dx12' || v === 'directx12' || v === 'd3d12') return 'dx12';
  if (v === 'vulkan' || v === 'vulkan1') return 'vulkan';
  if (v === 'opengl' || v === 'ogl' || v === 'opengl32') return 'opengl';
  return null;
}

function addSignal(signals, api, source, evidence) {
  const normalized = normalizeApi(api);
  if (normalized) signals.push({ api: normalized, source, evidence });
}

function inspectFile(file, pe, source, signals, options = {}) {
  if (!file || !pe || typeof file !== 'string') return;
  let stat;
  if (!pe.virtual) {
    try { stat = fs.statSync(file); } catch { return; }
    const limit = options.maxBytes || MAX_ENGINE_BYTES;
    if (!stat.isFile() || stat.size > limit && options.headerOnly !== true) return;
  }
  let imports = [];
  try { imports = typeof pe.getImports === 'function' ? pe.getImports(file) : []; } catch {}
  const names = new Set((Array.isArray(imports) ? imports : []).map(value => path.basename(text(value)).toLowerCase()));
  if (names.has('d3d9.dll')) addSignal(signals, 'dx9', source, `${path.basename(file)} imports d3d9.dll`);
  if (names.has('d3d10.dll') || names.has('d3d10_1.dll')) addSignal(signals, 'dx10', source, `${path.basename(file)} imports DirectX 10`);
  if (names.has('d3d12.dll')) addSignal(signals, 'dx12', source, `${path.basename(file)} imports d3d12.dll`);
  if (names.has('d3d11.dll')) addSignal(signals, 'dx11', source, `${path.basename(file)} imports d3d11.dll`);
  if (names.has('vulkan-1.dll')) addSignal(signals, 'vulkan', source, `${path.basename(file)} imports vulkan-1.dll`);
  if (names.has('opengl32.dll')) addSignal(signals, 'opengl', source, `${path.basename(file)} imports opengl32.dll`);
  let markers = new Set();
  if (!options.skipMarkers && (!stat || stat.size <= MAX_MARKER_BYTES)) try {
    const found = typeof pe.findMarkers === 'function'
      ? pe.findMarkers(file, ['D3D12CreateDevice', 'D3D11CreateDevice', 'Direct3DCreate9', 'vkCreateInstance', 'wglCreateContext', 'CreateDXGIFactory']) : [];
    markers = found instanceof Set ? found : new Set(found || []);
  } catch {}
  if (markers.has('D3D12CreateDevice')) addSignal(signals, 'dx12', `${source}-markers`, `${path.basename(file)} contains D3D12CreateDevice`);
  if (markers.has('D3D11CreateDevice')) addSignal(signals, 'dx11', `${source}-markers`, `${path.basename(file)} contains D3D11CreateDevice (route unproven)`);
  if (markers.has('Direct3DCreate9')) addSignal(signals, 'dx9', `${source}-markers`, `${path.basename(file)} contains Direct3DCreate9`);
  if (markers.has('vkCreateInstance')) addSignal(signals, 'vulkan', `${source}-markers`, `${path.basename(file)} contains vkCreateInstance`);
  if (markers.has('wglCreateContext')) addSignal(signals, 'opengl', `${source}-markers`, `${path.basename(file)} contains wglCreateContext`);
  if (markers.has('CreateDXGIFactory') && !markers.has('D3D12CreateDevice') && !markers.has('D3D11CreateDevice'))
    signals.push({ api: null, source: `${source}-markers`, evidence: `${path.basename(file)} contains CreateDXGIFactory` });
}

function classify(signals, chosen) {
  const apis = new Set(signals.map(row => row.api).filter(Boolean));
  const chosenRaw = text(chosen && chosen.api).toLowerCase(), chosenApi = normalizeApi(chosenRaw);
  if (apis.size > 1) return 'mixed';
  if (apis.size === 1 && !apis.has('dx11')) return [...apis][0];
  // A lone DX11 binary is deliberately unresolved: carrier presence and
  // D3D11 markers do not prove that the supported bridge route is active.
  if (apis.has('dx11') || chosenApi === 'dx11') return 'unknown';
  if (chosenApi) return chosenApi;
  if (chosenRaw && !['dxgi', 'unknown'].includes(chosenRaw)) return 'unsupported';
  return 'unknown';
}

function annotateApi(scan, root, context = {}) {
  if (!scan || !scan.chosen) return scan;
  const chosen = scan.chosen;
  const signals = [];
  const override = normalizeApi(context.apiOverride);

  const pe = context.pe || require('../core/pe');
  const exe = text(chosen.path);
  const entryRel = path.relative(context.entryRoot || root, exe).replace(/\\/g, '/').toLowerCase();
  const entryApi = STEAM_ENTRY_APIS[text(context.steamAppId)]?.[entryRel];
  const staticExceptions = STEAM_STATIC_EXCEPTIONS[text(context.steamAppId)]?.[entryRel] || [];
  const rdr2State = createRdr2ApiSettings({ documentsDir: context.documentsDir }).read({ ...context, exe });
  const rdr2 = rdr2State.matched, rdr2Settings = rdr2State.api ? { api: rdr2State.api, evidence: `RDR2 当前保存的 system.xml 图形设置选择 ${rdr2State.api === 'dx12' ? 'DirectX 12' : 'Vulkan'}` } : null;
  if (rdr2Settings) addSignal(signals, rdr2Settings.api, 'game-settings', rdr2Settings.evidence);
  else if (rdr2) signals.push({ api: null, source: 'game-settings', evidence: '尚未从 RDR2 当前文档目录安全读取唯一图形 API 设置，未自动选择路线' });
  if (entryApi) addSignal(signals, entryApi, 'game-entry', text(context.steamAppId) === '3764200'
    ? '已匹配 Steam 3764200 / re9.exe；卡普空公布的游戏配套为 DirectX 12'
    : 'Larian 官方启动入口：bg3.exe 使用 Vulkan；bg3_dx11.exe 使用 DX11。API 选择不会切换 EXE');
  inspectFile(exe, pe, 'exe-imports', signals, { headerOnly: true });
  const chosenLabel = text(chosen.apiLabel);
  if (chosen.dx12 === true || /(?:DirectX|DX|D3D)\s*12\b|(?:DirectX|DX|D3D)\s*11\s*\/\s*12\b/i.test(chosenLabel)) addSignal(signals, 'dx12', 'chosen-static', '扫描结果含 DirectX 12 静态线索');
  if (chosen.dx11 === true || /(?:DirectX|DX|D3D)\s*11\b/i.test(chosenLabel)) addSignal(signals, 'dx11', 'chosen-static', '扫描结果已标记 DirectX 11（实际桥接仍未证明）');
  if (chosenLabel && /DirectX\s*11.*DirectX\s*12|DirectX\s*12.*DirectX\s*11/i.test(chosenLabel)) {
    addSignal(signals, 'dx11', 'chosen-static', '扫描结果标记 DirectX 11/12');
  }
  const chosenApi = normalizeApi(chosen.api);
  if (chosenApi) addSignal(signals, chosenApi, 'chosen-static', `扫描结果标记 ${chosenLabel || chosenApi}`);
  if (/(?:DirectX|DX|D3D)\s*9\b/i.test(chosenLabel)) addSignal(signals, 'dx9', 'chosen-static', '扫描结果标记 DirectX 9');
  if (/(?:DirectX|DX|D3D)\s*10\b/i.test(chosenLabel)) addSignal(signals, 'dx10', 'chosen-static', '扫描结果标记 DirectX 10');
  if (/Vulkan/i.test(chosenLabel)) addSignal(signals, 'vulkan', 'chosen-static', '扫描结果标记 Vulkan');
  if (/OpenGL/i.test(chosenLabel)) addSignal(signals, 'opengl', 'chosen-static', '扫描结果标记 OpenGL');

  for (const signal of signals) {
    if (staticExceptions.includes(signal.api) && /^(?:exe-imports(?:-markers)?|chosen-static)$/.test(signal.source)) {
      signal.api = null;
      signal.evidence += '；保留为静态线索，游戏 API 按已匹配的官方配套判定';
    }
  }

  // The PE reader's import pass is cheap enough to permit large executables;
  // marker scans are deliberately skipped above 32 MiB. Large files retain
  // the scanner's existing chosen flags/label as static evidence.

  const modules = Array.isArray(context.engineModules) ? context.engineModules
    : (Array.isArray(scan.engineModules) ? scan.engineModules : discoverEngineModules(exe, root, pe));
  let inspected = 0;
  for (const item of modules) {
    if (inspected >= MAX_ENGINE_MODULES) break;
    const file = typeof item === 'string' ? item : item && (item.path || item.file);
    if (!file || !/\.dll$/i.test(file) || PROXY_OR_CARRIER.test(path.basename(file))) continue;
    if (GENERIC_GRAPHICS_SDK.test(path.basename(file))) {
      signals.push({api:null,source:'graphics-sdk',evidence:`${path.basename(file)} 为通用显卡 SDK，其多 API 能力不代表游戏的运行 API`});
      continue;
    }
    inspectFile(file, pe, 'engine-module', signals, { maxBytes: MAX_ENGINE_BYTES });
    inspected++;
  }

  if (rdr2) for (const signal of signals) {
    if (signal.api && /^(?:exe-imports(?:-markers)?|chosen-static|engine-module(?:-markers)?)$/.test(signal.source)) {
      signal.api = null;
      signal.evidence += '；这是 RDR2 可选图形能力，当前路线以保存的图形设置为准';
    }
  }

  // Existing ReShade logs are accepted only when a caller has already bound
  // the record to this EXE, the latest session, and the swapchain API.
  const log = context.logEvidence || readReshadeClues(exe);
  if (log && log.bound === true && samePath(log.exe, exe, root) && normalizeApi(log.api)) {
    addSignal(signals, log.api, 'runtime-log', '已绑定当前 EXE/最新会话/交换链 API 的历史日志');
  } else if (log) {
    signals.push({ api: null, source: 'runtime-log', evidence: log.evidence || '日志未能严格绑定当前 EXE、会话和交换链，仅作线索' });
  }

  const args = Array.isArray(context.launchArguments) ? context.launchArguments.join(' ') : text(context.launchArguments);
  const argApis = [];
  if (/\b(?:d3d|dx)11\b/i.test(args)) argApis.push('dx11');
  if (/\b(?:d3d|dx)12\b/i.test(args)) argApis.push('dx12');
  const uniqueArgApis = [...new Set(argApis)];
  if (uniqueArgApis.length) signals.push({ api: null, source: 'launch-arguments', evidence: `启动器配置提示 ${uniqueArgApis.join('/')}；管理器直接启动 EXE 不附加这些参数` });

  const staticApis = new Set(signals.map(row => row.api).filter(Boolean));
  const argumentConflict = uniqueArgApis.some(value => staticApis.size && !staticApis.has(value));
  const logConflict = log && log.bound === true && samePath(log.exe, exe, root) &&
    Array.isArray(log.apis) && log.apis.some(value => staticApis.size && !staticApis.has(value));
  const api = argumentConflict || logConflict || (uniqueArgApis.length > 1) ? 'mixed'
    : rdr2 && !rdr2Settings ? 'unknown' : entryApi && staticApis.size === 1 ? entryApi : classify(signals, chosen);
  const decisive = signals.filter(row => row.api === api);
  const evidence = [...new Set(signals.map(row => row.evidence))].slice(-8);
  const source = decisive.length ? decisive[0].source : (signals[0] ? signals[0].source : 'none');
  const detectedApiResolution = { api, source, evidence };
  const labels = { dx9: 'DirectX 9', dx10: 'DirectX 10', dx11: 'DirectX 11', dx12: 'DirectX 12', vulkan: 'Vulkan', opengl: 'OpenGL' };
  const apiResolution = override
    ? { api: override, source: 'override', evidence: [`用户显式选择 ${labels[override]}`] }
    : detectedApiResolution;
  return { ...scan, chosen: { ...chosen, detectedApi: api, detectedApiResolution, apiResolution,
    ...(rdr2 ? { supportedApis: rdr2State.supportedApis, apiSettings: { kind: 'rdr2-system-xml', api: rdr2State.api, file: rdr2State.file, sha256: rdr2State.sha256, canSync: rdr2State.canSync,
      steamAppId: '1174180', entryRoot: context.entryRoot, exe } } : {}) } };
}

function discoverEngineModules(exe, root, pe) {
  const dirs = new Set([path.dirname(exe)]);
  const exeDir = path.dirname(exe);
  for (const rel of ['Binaries/Win64', 'Engine/Binaries/Win64', 'Client/Binaries/Win64', 'Game/Binaries/Win64']) {
    const candidate = path.resolve(root || exeDir, rel);
    if (candidate.toLowerCase() !== exeDir.toLowerCase()) dirs.add(candidate);
  }
  const result = [];
  let imports = [];
  try { if (fs.statSync(exe).isFile()) imports = pe.getImports(exe); } catch {}
  const importedNames = new Set(imports.map(name => path.basename(name).toLowerCase()));
  for (const dir of dirs) {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { continue; }
    for (const entry of entries) {
      if (!entry.isFile() || !/\.dll$/i.test(entry.name) || PROXY_OR_CARRIER.test(entry.name)) continue;
      if (!importedNames.has(entry.name.toLowerCase()) && !/(?:unityplayer|gameassembly|engine|render|rhi|neox)/i.test(entry.name)) continue;
      result.push(path.join(dir, entry.name));
      if (result.length >= MAX_ENGINE_MODULES * 2) return result;
    }
  }
  return result;
}

function readReshadeClues(exe) {
  let log;
  try { log = path.join(path.dirname(exe), 'ReShade.log'); const ls = fs.statSync(log); const es = fs.statSync(exe); if (!ls.isFile() || ls.mtimeMs < es.mtimeMs) return null; } catch { return null; }
  let fd;
  try {
    fd = fs.openSync(log, 'r');
    const size = fs.fstatSync(fd).size;
    const head = Buffer.alloc(Math.min(16 * 1024, size));
    fs.readSync(fd, head, 0, head.length, 0);
    const tailSize = Math.min(64 * 1024, size);
    const tail = Buffer.alloc(tailSize);
    fs.readSync(fd, tail, 0, tailSize, Math.max(0, size - tailSize));
    const textLog = Buffer.concat([head, tail]).toString('utf8');
    const sessions = [...textLog.matchAll(/Initializing[^\r\n]+ReShade[^\r\n]+into ['"]([^'"]+)['"]/g)];
    const session = sessions[sessions.length - 1];
    if (!session || !samePath(session[1], exe)) return { bound: false, evidence: '已有 ReShade.log 无法绑定当前 EXE，未据此判定 API' };
    const apis = [...new Set([...textLog.slice(session.index).matchAll(/\bD3D(11|12)(?:CreateDevice|CreateDeviceAndSwapChain)\b/g)].map(m => `dx${m[1]}`))];
    // Device creation can come from a launcher, overlay or carrier. Even a
    // matching EXE log only supplies history; it never enables a DX11 carrier.
    return { bound: false, apis, evidence: `当前 EXE 的历史 ReShade.log${apis.length ? `含 ${apis.join('/')} 设备创建` : '未提供可确认的呈现 API'}；不是当前运行路线证明` };
  } catch { return null; }
  finally { if (fd !== undefined) fs.closeSync(fd); }
}

function samePath(left, right, root) {
  if (!left || !right) return false;
  const a = path.resolve(left).toLowerCase();
  const b = path.resolve(right).toLowerCase();
  return a === b;
}

module.exports = { annotateApi, normalizeApi, steamEntryContext, readRdr2GraphicsApi };
