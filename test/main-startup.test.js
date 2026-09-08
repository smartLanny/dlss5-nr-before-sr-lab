'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');
const realDiagnostics = require('../src/product/startup-diagnostics');
const realLaunchPolicy = require('../src/product/launch-settings-policy');

const mainFile = path.resolve(__dirname, '../main.js');
const source = fs.readFileSync(mainFile, 'utf8');
const settle = async (turns = 8) => { for (let i = 0; i < turns; i++) await new Promise(resolve => setImmediate(resolve)); };

function harness(options = {}) {
  const logs = [], dialogs = [], errors = [], handles = new Map(), execCalls = []; let latestWindow = null;
  const startup = { sessionId: '11111111-1111-1111-1111-111111111111', directory: null,
    log(stage, details = {}) { logs.push({ stage, details }); }, report: () => 'report', exportTo: file => file,
    acknowledge: id => logs.push({ stage: 'ack', details: { id } }), waitForAcknowledgement: async () => options.acknowledged === true };
  class App extends EventEmitter {
    constructor() { super(); this.isPackaged = options.packaged !== false; this.quitCalls = 0; this.ready = false; }
    disableHardwareAcceleration() { logs.push({ stage: 'disabled-hardware' }); }
    isReady() { return this.ready; }
    whenReady() { this.ready = true; return Promise.resolve(); }
    getVersion() { return 'test-version'; }
    getPath(name) { return path.join(__dirname, `.startup-${name}`); }
    requestSingleInstanceLock() { return options.singleInstance !== false; }
    setAppUserModelId() {}
    quit() { this.quitCalls++; }
    exit(code) { this.exitCode = code; }
    relaunch() { logs.push({ stage: 'relaunch' }); }
    async getFileIcon() { return { isEmpty: () => true }; }
  }
  const app = new App();
  class BrowserWindow extends EventEmitter {
    constructor(config) { super(); this.config = config; this.webContents = new EventEmitter(); this.webContents.send = () => {}; latestWindow = this; }
    isDestroyed() { return false; } isMinimized() { return false; } show() { this.shown = true; } focus() {} restore() {} minimize() {} close() { this.emit('closed'); }
    loadFile() { return options.loadFileError ? Promise.reject(new Error('fixture loadFile failure')) : Promise.resolve(); }
  }
  const ipcMain = new EventEmitter(); ipcMain.handle = (name, fn) => handles.set(name, fn);
  const dialog = { async showMessageBox(arg) { dialogs.push(arg); if (options.dialogFailure) throw new Error('dialog unavailable'); return { response: options.dialogResponse ?? 1 }; },
    async showSaveDialog() { return { canceled: true }; }, showErrorBox(title, detail) { errors.push({ title, detail }); }, async showOpenDialog() { return { canceled: true, filePaths: [] }; } };
  const electron = { app, BrowserWindow, ipcMain, dialog, shell: { showItemInFolder() {}, async openExternal() {}, async openPath() { return ''; } }, clipboard: { writeText() {} } };
  const service = { store: { readRecoveryStatus: () => ({ state: 'ok' }) }, withError: fn => fn(), gameDirectory: () => 'C:\\game', gameExecutable: () => 'C:\\game\\game.exe',
    gameScan: () => ({ primaryDlss:options.noNativeDlss ? null : {name:'nvngx_dlss.dll'}, streamlineFiles: [], chosen: {}, reshade: {} }), reShadeSource: () => null, importAddonFile: async () => {}, dispose() {} };
  const launchSettings = options.launchService || { assertReady: async () => {}, restore: async () => {} };
  const fgComponents = options.fgComponents || { inspect: async () => ({ route: 'native', ready: true }) };
  const business = {
    './src/product/app-service': { createAppService: () => service }, './src/product/sr-model-service': { createSrModelService: () => ({ read() {} }) },
    './src/product/launch-settings-service': { createLaunchSettingsService: () => launchSettings },
    './src/product/launch-coordinator': { createLaunchCoordinator: () => ({ serialize: fn => fn(), assertMutationReady: async () => {}, inspect: async () => {}, launch: async () => ({ launchSettings: [] }) }) },
    './src/core/install-guards': {}, './src/product/fg-components': { createFgComponents: () => fgComponents },
    './src/product/game-support': { classifyApi: () => 'dx12' }
  };
  const fakeProcess = new EventEmitter(); Object.assign(fakeProcess, { argv: options.argv || ['electron', mainFile], env: { SystemRoot: 'C:\\Windows', ...(options.env || {}) }, platform: 'win32',
    execPath: 'C:\\Program Files\\Manager\\manager.exe', resourcesPath: 'C:\\resources', pid: 1234, versions: { node: process.versions.node, electron: '33.0.0' } });
  let execCount = 0;
  const customRequire = request => {
    if (request === 'electron') return electron;
    if (request === './src/product/startup-diagnostics') return { createStartupDiagnostics: () => startup, watchWindow: realDiagnostics.watchWindow };
    if (request === './src/product/launch-settings-policy') return realLaunchPolicy;
    if (request === 'child_process') return { execFile(file, args, childOptions, callback) {
      execCalls.push({ file, args, options: childOptions }); execCount++; const row = options.execResults?.[execCount - 1] || { error: null, stdout: 'True' }; setImmediate(() => callback(row.error, row.stdout || '')); return { unref() {} };
    } };
    if (options.requireFailure === request) throw Object.assign(new Error('injected business module require failure'), { code: 'MODULE_NOT_FOUND' });
    if (business[request]) return business[request];
    return require(request);
  };
  const context = vm.createContext({ require: customRequire, module: { exports: {} }, exports: {}, __filename: mainFile, __dirname: path.dirname(mainFile), process: fakeProcess,
    console, Buffer, URL, setTimeout, clearTimeout, setImmediate });
  new vm.Script(source, { filename: mainFile }).runInContext(context);
  return { app, logs, dialogs, errors, handles, execCalls, process: fakeProcess, launchSettings, fgComponents, get window() { return latestWindow; } };
}

function saw(h, stage) { return h.logs.some(row => row.stage === stage); }
function dialogTitle(h, text) { return h.dialogs.some(row => String(row.message || row.title).includes(text)); }

test('business-module require failure produces an early log and visible native dialog', async () => {
  const h = harness({ requireFailure: './src/product/app-service' }); await settle();
  assert.equal(saw(h, 'electron-entry'), true); assert.equal(saw(h, 'startup-failure'), true); assert.equal(dialogTitle(h, '管理器初始化失败'), true);
});

test('loadFile rejection is visible and does not add a no-sandbox retry', async () => {
  const h = harness({ loadFileError: true }); await settle(); assert.equal(saw(h, 'window-created'), true); assert.equal(dialogTitle(h, '界面文件加载失败'), true);
  assert.equal(h.process.argv.includes('--no-sandbox'), false); assert.equal(h.logs.some(row => JSON.stringify(row).includes('--no-sandbox')), false);
});

test('GPU launch failure, preload failure and renderer crash each reach the native error path', async () => {
  const gpu = harness(); await settle(); gpu.app.emit('child-process-gone', {}, { type: 'GPU', reason: 'launch-failed', exitCode: 7 }); await settle();
  assert.equal(saw(gpu, 'child-process-gone'), true); assert.equal(dialogTitle(gpu, '图形子进程无法启动'), true);
  const preload = harness(); await settle(); preload.window.webContents.emit('preload-error', {}, 'preload.js', new Error('preload fixture')); await settle();
  assert.equal(dialogTitle(preload, '界面连接模块加载失败'), true);
  const renderer = harness(); await settle(); renderer.window.webContents.emit('render-process-gone', {}, { reason: 'crashed', exitCode: 9 }); await settle();
  assert.equal(dialogTitle(renderer, '界面进程意外退出'), true);
});

test('single-instance without acknowledgement explains the hidden-window condition without killing anything', async () => {
  const h = harness({ singleInstance: false, acknowledged: false }); await settle();
  assert.equal(saw(h, 'existing-instance-detected'), true); assert.equal(h.errors.length, 1);
  assert.match(h.errors[0].detail, /已有管理器进程，但没有确认窗口已显示/); assert.match(h.errors[0].detail, /不会自动结束进程或删除单实例文件/);
});

test('UAC child launch failure is logged and shown without invoking a real elevation prompt', async () => {
  const denied = Object.assign(new Error('fixture UAC denied'), { code: 'EACCES' });
  const h = harness({ packaged: false, execResults: [{ error: null, stdout: 'False' }, { error: denied, stdout: '' }] }); await settle(14);
  assert.equal(saw(h, 'elevation-requested'), true); assert.equal(saw(h, 'elevation-failed'), true); assert.equal(h.errors.length, 1);
  assert.match(h.errors[0].detail, /未能启动管理员进程/); assert.equal(h.app.exitCode, 1);
});

test('native dialog failure falls back to showErrorBox instead of a silent rejection', async () => {
  const h = harness({ requireFailure: './src/product/app-service', dialogFailure: true }); await settle();
  assert.equal(saw(h, 'error-dialog-failed'), true); assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /启动诊断\.cmd/);
});

test('failure before app readiness synchronously shows an error box and exits', async () => {
  const h = harness(); h.app.emit('child-process-gone', {}, { type: 'GPU', reason: 'launch-failed', exitCode: 5 }); await settle();
  assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /图形子进程无法启动/); assert.equal(h.app.exitCode, 1);
  assert.equal(saw(h, 'startup-failure'), true);
});

test('an elevation-attempted process that is still non-admin refuses a second spawn', async () => {
  const h = harness({ packaged: false, argv: ['electron', mainFile, '--elevation-attempted'], execResults: [{ error: null, stdout: 'False' }] }); await settle(12);
  assert.equal(h.execCalls.length, 1, 'only the administrator check runs'); assert.equal(saw(h, 'elevation-requested'), false);
  assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /已停止重启循环/); assert.equal(h.app.exitCode, 1);
});

test('portable elevation targets the original shell instead of its temporary inner executable', async t => {
  const root = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'xiaofeng-portable-shell-')); t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const portable = path.join(root, '小枫 portable.exe'); fs.writeFileSync(portable, 'shell');
  const h = harness({ packaged: false, env: { PORTABLE_EXECUTABLE_FILE: portable, PORTABLE_EXECUTABLE_DIR: path.join(root, 'temporary-inner') },
    execResults: [{ error: null, stdout: 'False' }, { error: null, stdout: '4321' }] }); await settle(14);
  assert.equal(h.execCalls.length, 2); const command = h.execCalls[1].args.at(-1);
  assert.match(command, new RegExp(portable.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))); assert.doesNotMatch(command, /temporary-inner/);
  assert.equal(saw(h, 'elevation-child-started'), true); assert.equal(h.app.quitCalls, 1);
});

test('launch settings update validates with the real policy before inspecting or preparing FG components', async () => {
  const componentCalls = [], settingCalls = [];
  const h = harness({
    fgComponents: {
      inspect: async () => { componentCalls.push('inspect'); return { route: 'compatibility', ready: false }; },
      prepare: async () => { componentCalls.push('prepare'); return { prepared: true }; }
    },
    launchService: {
      assertReady: async () => settingCalls.push('ready'), preview: async () => settingCalls.push('preview'),
      apply: async () => settingCalls.push('apply'), save: async () => settingCalls.push('save'), restore: async () => {}
    }
  });
  await settle();
  const update = h.handles.get('launch-settings-update');
  assert.equal(typeof update, 'function');
  await assert.rejects(update({}, 'game', 'fg', { backend: 'rtx40', mode: 'bogus' }), error => error.code === 'UNKNOWN_MODE');
  assert.deepEqual(componentCalls, []);
  assert.deepEqual(settingCalls, []);
});

test('RTX40 automatic update prepares missing components before preview, apply and save', async () => {
  const order = [];
  const request = { backend: 'rtx40', mode: 'fixed', multiplier: 3 };
  const h = harness({
    fgComponents: {
      inspect: async () => { order.push('inspect'); return { route: 'compatibility', ready: false }; },
      prepare: async (id, options) => { order.push('prepare'); assert.equal(id, 'game'); assert.equal(options.allowAntiCheat, true); return { prepared: true }; }
    },
    launchService: {
      assertReady: async id => { assert.equal(id, 'game'); order.push('ready'); },
      preview: async (id, domain, actual) => { order.push('preview'); assert.deepEqual([id, domain, actual], ['game', 'fg', request]); return { id: 'plan-1' }; },
      apply: async (id, consent) => { order.push('apply'); assert.equal(id, 'plan-1'); assert.equal(consent.confirm, true); assert.equal(consent.automatic, true); return { applied: true, readbackVerified: true }; },
      save: async (id, domain, actual) => { order.push('save'); assert.deepEqual([id, domain, actual], ['game', 'fg', request]); return { saved: true }; },
      restore: async () => {}
    }
  });
  await settle();
  const result = await h.handles.get('launch-settings-update')({}, 'game', 'fg', request, { allowAntiCheat: true });
  assert.deepEqual(order, ['ready', 'inspect', 'prepare', 'preview', 'apply', 'save']);
  assert.equal(result.applied, true); assert.equal(result.saved, true);
});

test('RTX40 automatic update rejects a route mismatch without preparing or writing settings', async () => {
  const calls = [];
  const h = harness({
    fgComponents: {
      inspect: async () => { calls.push('inspect'); return { route: 'native', ready: false }; },
      prepare: async () => calls.push('prepare')
    },
    launchService: {
      assertReady: async () => calls.push('ready'), preview: async () => calls.push('preview'),
      apply: async () => calls.push('apply'), save: async () => calls.push('save'), restore: async () => {}
    }
  });
  await settle();
  await assert.rejects(h.handles.get('launch-settings-update')({}, 'game', 'fg', { backend: 'rtx40', mode: 'fixed', multiplier: 3 }),
    error => error.code === 'SETTINGS_FG_ROUTE_MISMATCH');
  assert.deepEqual(calls, ['ready', 'inspect']);
});

test('global launch settings reset attempts both domains and rejects partial restoration', async () => {
  const restored = [];
  const h = harness({ launchService: {
    assertReady: async () => {},
    restore: async (_id, domain) => {
      restored.push(domain);
      if (domain === 'sr') throw Object.assign(new Error('fixture SR restore failure'), { code: 'SR_RESTORE_FAILED' });
      return { restored: true };
    }
  } });
  await settle();
  await assert.rejects(h.handles.get('launch-settings-reset-all')({}, 'game'), error => {
    assert.equal(error.code, 'SETTINGS_RESTORE_INCOMPLETE');
    assert.equal(JSON.stringify(error.details.outcomes.map(row => [row.domain, row.ok])), JSON.stringify([['fg', true], ['sr', false]]));
    return true;
  });
  assert.deepEqual(restored, ['fg', 'sr']);
});

test('missing native DLSS rejects automatic overrides before component or driver work while allowing restore', async () => {
  const calls=[];
  const h=harness({noNativeDlss:true,fgComponents:{inspect:async()=>calls.push('components')},launchService:{
    assertReady:async()=>{},preview:async()=>{calls.push('preview');return{id:'restore'};},apply:async()=>{calls.push('apply');return{restored:true};},save:async()=>calls.push('save')
  }});
  await settle();
  await assert.rejects(h.handles.get('launch-settings-update')({},'game','sr',{backend:'native',quality:'balanced',preset:'auto'}),error=>error.code==='ERR_NO_DLSS');
  await assert.rejects(h.handles.get('launch-settings-update')({},'game','fg',{backend:'rtx40',mode:'fixed',multiplier:3}),error=>error.code==='ERR_NO_DLSS');
  assert.deepEqual(calls,[]);
  const result=await h.handles.get('launch-settings-update')({},'game','fg',{backend:'nvidia',mode:'restore'});
  assert.equal(result.restored,true);assert.deepEqual(calls,['preview','apply','save']);
});
