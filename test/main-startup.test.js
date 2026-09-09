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
    constructor() {
      super(); this.isPackaged = options.packaged !== false; this.quitCalls = 0; this.ready = false;
      this.commandLine = { hasSwitch: name => options.effectiveSwitches
        ? options.effectiveSwitches.includes(name)
        : (options.argv || []).some(arg => arg === `--${name}` || arg.startsWith(`--${name}=`)) };
    }
    disableHardwareAcceleration() { logs.push({ stage: 'disabled-hardware' }); }
    isReady() { return this.ready; }
    whenReady() { if (options.deferReady) return new Promise(() => {}); this.ready = true; return Promise.resolve(); }
    getVersion() { return 'test-version'; }
    getPath(name) { return path.join(__dirname, `.startup-${name}`); }
    requestSingleInstanceLock() { logs.push({ stage: 'instance-lock-requested' }); return options.singleInstance !== false; }
    releaseSingleInstanceLock() { logs.push({ stage: 'instance-lock-released' }); }
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
  const service = { store: { readRecoveryStatus: () => ({ state: 'ok' }) }, withError: options.withError || (fn => fn()), install: options.install || (async () => ({})),
    gameDirectory: () => 'C:\\game', gameExecutable: () => 'C:\\game\\game.exe',
    gameScan: () => ({ primaryDlss: options.noNativeDlss ? null : { name: 'nvngx_dlss.dll' },
      streamlineFiles: options.noNativeFg ? [] : [{ name: 'sl.interposer.dll' }, { name: 'sl.dlss_g.dll' }], chosen: {}, reshade: {} }),
    reShadeSource: () => null, refreshAfterMutation: async value => value, importAddonFile: async () => {}, dispose() {} };
  const launchSettings = { assertReady: async () => {}, restore: async () => {}, inspect: async () => ({ requests: {}, applied: {} }), ...options.launchService };
  const fgComponents = options.fgComponents || { inspect: async () => ({ route: 'native', ready: true }) };
  const business = {
    './src/product/app-service': { createAppService: () => service }, './src/product/sr-model-service': { createSrModelService: () => ({ read() {} }) },
    './src/product/launch-settings-service': { createLaunchSettingsService: () => launchSettings },
    './src/product/launch-coordinator': { createLaunchCoordinator: () => ({ serialize: options.serialize || (fn => fn()),
      assertMutationReady: options.assertMutationReady || (async () => {}), inspect: async () => {}, launch: async () => ({ launchSettings: [] }) }) },
    './src/core/install-guards': { assertGameClosed: options.assertGameClosed || (async () => {}) }, './src/product/fg-components': { createFgComponents: () => fgComponents },
    './src/product/game-preparation': { createGamePreparation: () => ({ assertReady: async () => {}, inspect: async () => ({ pending: false }) }) },
    './src/product/game-support': { classifyApi: () => 'dx12' }
  };
  const fakeProcess = new EventEmitter(); Object.assign(fakeProcess, { argv: options.argv || ['electron', mainFile], env: { SystemRoot: 'C:\\Windows', ...(options.env || {}) }, platform: 'win32',
    execPath: 'C:\\Program Files\\Manager\\manager.exe', resourcesPath: 'C:\\resources', pid: 1234,
    versions: { node: process.versions.node, electron: '33.0.0', chrome: '130.0.6723.191' } });
  let execCount = 0;
  const customRequire = request => {
    if (request === 'electron') return electron;
    if (request === './src/product/startup-diagnostics') return { ...realDiagnostics, createStartupDiagnostics: () => startup };
    if (request === './src/product/startup-elevation') return require('../src/product/startup-elevation');
    if (request === './src/product/startup-handoff') return { createStartupHandoff: () => ({
      begin: () => ({ nonce: '22222222-2222-2222-2222-222222222222' }),
      wait: options.handoffWait || (async () => { logs.push({ stage: 'elevation-child-ready' }); }),
      cancel: () => logs.push({ stage: 'handoff-cancelled' }), finish: () => logs.push({ stage: 'handoff-finished' }),
      listen() {}, readyChild: () => true, failChild() {}
    }) };
    if (request === './src/product/launch-settings-policy') return realLaunchPolicy;
    if (request === './src/product/fg-workflow') return require('../src/product/fg-workflow');
    if (request === './src/product/game-enhancement-capabilities') return { inspectNativeEnhancementCapabilities: scan => ({
      nativeDlssAvailable: Boolean(scan.primaryDlss), nativeFgAvailable: scan.streamlineFiles.some(row => row.name === 'sl.dlss_g.dll') &&
        scan.streamlineFiles.some(row => row.name === 'sl.interposer.dll')
    }) };
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
  assert.equal(renderer.dialogs.at(-1).buttons.includes('使用软件渲染重启'), true);
});

test('startup options report effective switches and runtime versions without recording argv', async () => {
  const h = harness({ argv: ['electron', mainFile, '--no-sandbox', 'D:\\Private Games\\game.exe'],
    effectiveSwitches: ['disable-gpu', 'disable-gpu-sandbox', 'software-rendering'] });
  await settle();
  const options = h.logs.find(row => row.stage === 'startup-options').details;
  assert.equal(options.noSandbox, false, 'effective Chromium switches take precedence over raw argv');
  assert.equal(options.disableGpu, true); assert.equal(options.disableGpuSandbox, true); assert.equal(options.softwareRendering, true);
  assert.equal(options.chromium, '130.0.6723.191'); assert.equal(options.osRelease, require('node:os').release());
  assert.equal(saw(h, 'disabled-hardware'), true);
  assert.doesNotMatch(JSON.stringify(options), /Private Games|game\.exe|argv/);
  h.window.emit('closed');
});

test('default startup retains sandbox protections and does not request a compatibility relaunch', async () => {
  const h = harness(); await settle();
  const preferences = h.window.config.webPreferences;
  assert.equal(preferences.sandbox, true); assert.equal(preferences.contextIsolation, true); assert.equal(preferences.nodeIntegration, false);
  const options = h.logs.find(row => row.stage === 'startup-options').details;
  for (const name of ['noSandbox', 'disableGpu', 'disableGpuSandbox', 'softwareRendering']) assert.equal(options[name], false, name);
  assert.equal(saw(h, 'relaunch'), false); assert.equal(saw(h, 'disabled-hardware'), false);
  assert.equal(h.execCalls.length, 0, 'opening the normal window neither probes PowerShell nor elevates');
  h.window.emit('closed');
});

test('GPU and renderer launch failures preserve their cause and offer manual compatibility guidance without software retry', async () => {
  for (const type of ['GPU', 'Renderer']) {
    for (const reason of ['launch-failed', 'integrity-failure']) {
      const h = harness(); await settle();
      if (type === 'GPU') h.app.emit('child-process-gone', {}, { type, reason, exitCode: 18 });
      else h.window.webContents.emit('render-process-gone', {}, { reason, exitCode: 18 });
      await settle();
      const failure = h.logs.find(row => row.stage === 'startup-failure').details;
      assert.equal(failure.type, type); assert.equal(failure.reason, reason); assert.equal(failure.exitCode, 18);
      const event = h.logs.find(row => row.stage === (type === 'GPU' ? 'child-process-gone' : 'render-process-gone')).details;
      assert.equal(event.type, type); assert.equal(event.reason, reason); assert.equal(event.exitCode, 18);
      assert.match(h.dialogs.at(-1).detail, /兼容启动\.cmd/); assert.match(h.dialogs.at(-1).detail, /默认启动保留沙箱/);
      assert.equal(h.dialogs.at(-1).buttons.includes('使用软件渲染重启'), false);
      assert.equal(saw(h, 'relaunch'), false); assert.equal(h.process.argv.includes('--no-sandbox'), false);
      h.window.emit('closed');
    }
  }
});

test('single-instance without acknowledgement explains the hidden-window condition without killing anything', async () => {
  const h = harness({ singleInstance: false, acknowledged: false }); await settle();
  assert.equal(saw(h, 'existing-instance-detected'), true); assert.equal(h.errors.length, 1);
  assert.match(h.errors[0].detail, /已有管理器进程，但没有确认窗口已显示/); assert.match(h.errors[0].detail, /不会自动结束进程或删除单实例文件/);
});

test('UAC child launch failure is logged and shown without invoking a real elevation prompt', async () => {
  const denied = Object.assign(new Error('fixture UAC denied'), { code: 'EACCES' });
  const h = harness({ packaged: false, argv: ['electron', mainFile, '--as-admin'],
    execResults: [{ error: null, stdout: 'False' }, { error: denied, stdout: '' }] }); await settle(14);
  assert.equal(saw(h, 'elevation-requested'), true); assert.equal(saw(h, 'elevation-failed'), true); assert.equal(h.errors.length, 1);
  assert.match(h.errors[0].detail, /未能启动管理员进程/); assert.equal(h.app.exitCode, 1);
});

test('native dialog failure falls back to showErrorBox instead of a silent rejection', async () => {
  const h = harness({ requireFailure: './src/product/app-service', dialogFailure: true }); await settle();
  assert.equal(saw(h, 'error-dialog-failed'), true); assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /启动诊断\.cmd/);
});

test('failure before app readiness synchronously shows an error box and exits', async () => {
  const h = harness({ deferReady: true }); h.app.emit('child-process-gone', {}, { type: 'GPU', reason: 'launch-failed', exitCode: 5 }); await settle();
  assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /图形子进程无法启动/); assert.equal(h.app.exitCode, 1);
  assert.equal(saw(h, 'startup-failure'), true);
});

test('an elevation-attempted process that is still non-admin refuses a second spawn', async () => {
  const h = harness({ packaged: false, argv: ['electron', mainFile, '--as-admin', '--elevation-attempted'], execResults: [{ error: null, stdout: 'False' }] }); await settle(12);
  assert.equal(h.execCalls.length, 1, 'only the administrator check runs'); assert.equal(saw(h, 'elevation-requested'), false);
  assert.equal(h.errors.length, 1); assert.match(h.errors[0].detail, /已停止重启循环/); assert.equal(h.app.exitCode, 1);
});

test('portable elevation targets the original shell instead of its temporary inner executable', async t => {
  const root = fs.mkdtempSync(path.join(require('node:os').tmpdir(), 'xiaofeng-portable-shell-')); t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const portable = path.join(root, '小枫 portable.exe'); fs.writeFileSync(portable, 'shell');
  const h = harness({ packaged: false, argv: ['electron', mainFile, '--as-admin'], env: { PORTABLE_EXECUTABLE_FILE: portable, PORTABLE_EXECUTABLE_DIR: path.join(root, 'temporary-inner') },
    execResults: [{ error: null, stdout: 'False' }, { error: null, stdout: '4321' }] }); await settle(14);
  assert.equal(h.execCalls.length, 2); const command = h.execCalls[1].args.at(-1);
  assert.match(command, new RegExp(portable.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))); assert.doesNotMatch(command, /temporary-inner/);
  assert.equal(saw(h, 'elevation-child-started'), true); assert.equal(h.app.quitCalls, 1);
});

test('startup context is read-only, identifies compatibility mode and does not trust a packaged admin manifest', async () => {
  const h = harness({ argv: ['manager.exe', '--no-sandbox'], execResults: [{ error: null, stdout: 'False' }] }); await settle();
  const context = h.handles.get('startup-context');
  const foreign = await context({ sender: {} }); assert.equal(foreign.ok, false); assert.equal(h.execCalls.length, 0);
  const result = await context({ sender: h.window.webContents });
  assert.equal(result.ok, true); assert.equal(result.value.privilege, 'standard'); assert.equal(result.value.mode, 'compatibility');
  assert.equal(result.value.sandbox, false); assert.equal(result.value.canRestartElevated, true);
  assert.equal(h.execCalls.length, 1); assert.doesNotMatch(h.execCalls[0].args.at(-1), /Start-Process|RunAs/);
  assert.equal(saw(h, 'elevation-requested'), false); h.window.emit('closed');
});

test('explicit administrator restart serializes the handoff and preserves the normal window on UAC cancellation', async () => {
  const denied = Object.assign(new Error('fixture UAC denied'), { code: 'EACCES' }); let serialized = 0;
  const h = harness({ serialize: fn => { serialized++; return fn(); },
    execResults: [{ error: null, stdout: 'False' }, { error: denied, stdout: '' }] }); await settle();
  const restart = h.handles.get('startup-restart-elevated');
  assert.equal((await restart({ sender: h.window.webContents }, {})).error.code, 'CONFIRM_REQUIRED');
  assert.equal((await restart({ sender: {} }, { confirm: true })).error.code, 'CONFIRM_REQUIRED'); assert.equal(h.execCalls.length, 0);
  const result = await restart({ sender: h.window.webContents }, { confirm: true });
  assert.equal(result.ok, false); assert.equal(result.error.code, 'STARTUP_ADMIN_RESTART_FAILED'); assert.match(result.error.message, /当前窗口已保留/);
  assert.equal(serialized, 1); assert.equal(h.app.quitCalls, 0);
  assert.equal(saw(h, 'instance-lock-released'), true);
  assert.equal(h.logs.filter(row => row.stage === 'instance-lock-requested').length, 2, 'normal instance reacquires its released lock');
  assert.match(h.execCalls[1].args.at(-1), /--as-admin/); assert.doesNotMatch(h.execCalls[1].args.at(-1), /--no-sandbox/);
  h.window.emit('closed');
});

test('a surviving administrator PID cannot close the original window without a new ready session', async () => {
  const h = harness({ execResults: [{ error: null, stdout: 'False' }, { error: null, stdout: '4321' }],
    handoffWait: async () => { throw Object.assign(new Error('fixture process alive without renderer-ready'), { code: 'STARTUP_CHILD_NOT_READY' }); } });
  await settle();
  const result = await h.handles.get('startup-restart-elevated')({ sender: h.window.webContents }, { confirm: true });
  assert.equal(result.ok, false); assert.equal(result.error.code, 'STARTUP_CHILD_NOT_READY');
  assert.equal(saw(h, 'elevation-child-started'), true); assert.equal(saw(h, 'handoff-cancelled'), true);
  assert.equal(h.app.quitCalls, 0); assert.equal(h.logs.filter(row => row.stage === 'instance-lock-requested').length, 2);
  h.window.emit('closed');
});

test('permission recovery retains the game guard, original error and transaction state without an automatic retry', async () => {
  const { normalizeError } = require('../src/product/errors'); let guarded = 0, writes = 0;
  const h = harness({ withError: async work => { try { return { ok: true, value: await work() }; } catch (error) { return { ok: false, error: normalizeError(error) }; } },
    assertMutationReady: async () => { guarded++; }, install: async () => { writes++; throw Object.assign(new Error('fixture denied'),
      { code: 'EACCES', details: { pending: true, phase: 'rollback', recoveryStateKnown: false } }); } });
  await settle();
  const result = await h.handles.get('game-install')({ sender: h.window.webContents }, 'game');
  assert.equal(guarded, 1); assert.equal(writes, 1); assert.equal(result.ok, false); assert.equal(result.error.code, 'EACCES');
  assert.equal(result.error.details.pending, true); assert.equal(result.error.details.recoveryStateKnown, false);
  assert.equal(result.error.details.recoveryAction, 'restart-elevated'); assert.match(result.error.message, /设置 → 以管理员身份重启/);
  assert.equal(h.execCalls.length, 0); assert.equal(h.app.quitCalls, 0);
  h.window.emit('closed');
});

test('game-closed rejection still prevents a write under ordinary startup', async () => {
  let writes = 0;
  const h = harness({ assertMutationReady: async () => { throw Object.assign(new Error('game is running'), { code: 'ERR_GAME_RUNNING' }); },
    install: async () => { writes++; } }); await settle();
  await assert.rejects(h.handles.get('game-install')({}, 'game'), { code: 'ERR_GAME_RUNNING' });
  assert.equal(writes, 0); assert.equal(h.execCalls.length, 0); h.window.emit('closed');
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
  await assert.rejects(update({}, 'game', 'fg', { backend: 'mfgunlock', mode: 'bogus' }), error => error.code === 'SETTINGS_INPUT');
  assert.deepEqual(componentCalls, []);
  assert.deepEqual(settingCalls, []);
});

test('MFG Unlock update uses the real workflow to prepare missing components before preview, apply and save', async () => {
  const order = [];
  const request = { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 };
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
  assert.ok(order.indexOf('ready') < order.indexOf('prepare'));
  assert.deepEqual(order.filter(value => value !== 'ready'), ['inspect', 'prepare', 'preview', 'apply', 'save']);
  assert.equal(result.applied, true); assert.equal(result.saved, true); assert.equal(result.backend, 'mfgunlock');
});

test('MFG Unlock update rejects a route mismatch without preparing or writing settings', async () => {
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
  await assert.rejects(h.handles.get('launch-settings-update')({}, 'game', 'fg', { backend: 'mfgunlock', mode: 'fixed', multiplier: 3 }),
    error => error.code === 'SETTINGS_FG_ROUTE_MISMATCH');
  assert.ok(calls.includes('ready')); assert.deepEqual(calls.filter(value => value !== 'ready'), ['inspect']);
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

test('missing SR and FG providers each reject their own override before component or driver work while allowing restore', async () => {
  const calls=[];
  const h=harness({noNativeDlss:true,noNativeFg:true,fgComponents:{inspect:async()=>calls.push('components')},launchService:{
    assertReady:async()=>{},preview:async()=>{calls.push('preview');return{id:'restore'};},apply:async()=>{calls.push('apply');return{restored:true};},save:async()=>calls.push('save')
  }});
  await settle();
  await assert.rejects(h.handles.get('launch-settings-update')({},'game','sr',{backend:'native',quality:'balanced',preset:'auto'}),error=>error.code==='ERR_NO_DLSS');
  await assert.rejects(h.handles.get('launch-settings-update')({},'game','fg',{backend:'mfgunlock',mode:'fixed',multiplier:3}),error=>error.code==='SETTINGS_NO_NATIVE_FG');
  assert.deepEqual(calls,[]);
  const result=await h.handles.get('launch-settings-update')({},'game','fg',{backend:'nvidia',mode:'restore'});
  assert.equal(result.restored,true);assert.deepEqual(calls,['preview','apply','save']);
});

test('native FG remains independent from a missing SR provider', async () => {
  const calls = [];
  const h = harness({ noNativeDlss: true, fgComponents: { inspect: async () => { calls.push('fg-inspect'); return { route: 'native', ready: true }; } },
    launchService: { preview: async (_id, domain) => { calls.push(`preview-${domain}`); return { id: 'fg-plan' }; },
      apply: async () => { calls.push('apply'); return { applied: true }; }, save: async () => calls.push('save') } }); await settle();
  await assert.rejects(h.handles.get('launch-settings-update')({}, 'game', 'sr', { backend: 'native', quality: 'quality', preset: 'auto' }), { code: 'ERR_NO_DLSS' });
  const result = await h.handles.get('launch-settings-update')({}, 'game', 'fg', { backend: 'nvidia', mode: 'fixed', multiplier: 2 });
  assert.equal(result.applied, true); assert.deepEqual(calls, ['fg-inspect', 'preview-fg', 'apply', 'save']); h.window.emit('closed');
});
