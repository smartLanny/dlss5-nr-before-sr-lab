'use strict';

const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const { createStartupDiagnostics, watchWindow } = require('./src/product/startup-diagnostics');
const startup = createStartupDiagnostics();
const { app, BrowserWindow, ipcMain, dialog, shell, clipboard } = require('electron');
let createAppService, createSrModelService, createLaunchSettingsService, createLaunchCoordinator, installGuards, createFgComponents, classifyApi;

let win = null;
let service = null;
let srModel = null;
let launchSettings = null;
let launchCoordinator = null;
let fgComponents = null;
let windowWatch = null;
let failureVisible = false;
let quitting = false;
app.on('before-quit', () => { quitting = true; });
startup.log('startup-options', { noSandbox: process.argv.includes('--no-sandbox'), disableGpu: process.argv.includes('--disable-gpu'),
  nodeOptionsPresent: Boolean(process.env.NODE_OPTIONS), electronRunAsNodePresent: Boolean(process.env.ELECTRON_RUN_AS_NODE),
  compatibilityLayerPresent: Boolean(process.env.__COMPAT_LAYER) });
if (process.argv.includes('--software-rendering')) { app.disableHardwareAcceleration(); startup.log('software-rendering-requested'); }

async function exportStartupReport() {
  const selected = await dialog.showSaveDialog({ title: '保存精简启动诊断', defaultPath: 'DLSS5-启动诊断.txt', filters: [{ name: '文本诊断', extensions: ['txt'] }] });
  if (!selected.canceled && selected.filePath) { startup.exportTo(selected.filePath); shell.showItemInFolder(selected.filePath); }
}

async function startupFailure(title, error, softwareRetry = false) {
  startup.log('startup-failure', { title, code: error?.code || '', message: error?.message || String(error), stack: error?.stack || '' });
  if (failureVisible) return;
  failureVisible = true;
  try {
    if (!app.isReady()) {
      let saved = '';
      try { if (startup.directory) saved = startup.exportTo(path.join(startup.directory, '启动失败诊断.txt')); } catch {}
      dialog.showErrorBox('管理器启动失败', `${title}\n${error?.message || ''}\n${saved ? `诊断已保存：${saved}` : '请运行独立的“启动诊断.cmd”导出诊断。'}`);
      app.exit(1);
      return;
    }
    await app.whenReady();
    const result = await dialog.showMessageBox({ type: 'error', title: '管理器未能正常启动', message: title,
      detail: `${error?.message || '启动过程未完成。'}\n可以直接导出精简诊断。不会自动删除旧配置，也不能仅凭此判断缺少 VC++。`,
      buttons: softwareRetry ? ['导出诊断', '使用软件渲染重启', '关闭'] : ['导出诊断', '关闭'], cancelId: softwareRetry ? 2 : 1 });
    if (result.response === 0) { await exportStartupReport(); app.quit(); }
    else if (softwareRetry && result.response === 1) { app.relaunch({ args: [...process.argv.slice(1).filter(arg => arg !== '--software-rendering'), '--software-rendering'] }); app.quit(); }
    else app.quit();
  } catch (failure) {
    startup.log('error-dialog-failed', { message: failure.message });
    try { dialog.showErrorBox('管理器启动失败', `${title}\n${error?.message || ''}\n请运行随包的“启动诊断.cmd”导出诊断。`); } catch {}
    app.quit();
  } finally { failureVisible = false; }
}
process.on('uncaughtException', error => { void startupFailure('启动过程发生异常', error); });
process.on('unhandledRejection', error => { void startupFailure('启动过程未完成', error); });
process.on('exit', code => startup.log('process-exit', { code }));
app.on('child-process-gone', (_event, details) => {
  startup.log('child-process-gone', { type: details.type, reason: details.reason, exitCode: details.exitCode, serviceName: details.serviceName, name: details.name });
  if (!quitting && details.type === 'GPU' && ['launch-failed', 'integrity-failure'].includes(details.reason)) {
    void startupFailure('图形子进程无法启动', new Error(`GPU process: ${details.reason} (${details.exitCode})。请导出诊断，核对系统对子进程的限制；不自动关闭沙箱。`));
  }
});

const powershell = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
function runPowerShell(command, timeout = 15000) {
  return new Promise((resolve, reject) => execFile(powershell, ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command', command],
    { encoding: 'utf8', windowsHide: true, timeout, maxBuffer: 16384 }, (error, stdout) => error ? reject(error) : resolve(String(stdout).trim())));
}

async function isWindowsAdministrator() {
  if (process.platform !== 'win32') return true;
  // Both shipped EXEs have a verified requireAdministrator PE manifest.
  // Windows applies it before JS starts; invoking PowerShell again here can
  // itself be blocked by application-control policy and hide an otherwise usable UI.
  if (app.isPackaged && !process.env.__COMPAT_LAYER) { startup.log('elevation-enforced-by-manifest'); return true; }
  try {
    const result = await runPowerShell('$id=[Security.Principal.WindowsIdentity]::GetCurrent(); $p=New-Object Security.Principal.WindowsPrincipal($id); $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)');
    if (!/^(?:true|false)$/i.test(result)) throw new Error('无法确认当前进程权限。');
    return /^true$/i.test(result);
  } catch (error) {
    startup.log('administrator-check-failed', { message: error.message });
    throw new Error('无法检查管理员权限，已停止重复提权。请导出启动诊断。');
  }
}

function quotePowerShell(value) {
  return `'${String(value).replace(/'/g, "''")}'`;
}

async function relaunchAsAdministrator() {
  if (await isWindowsAdministrator()) { startup.log('administrator-confirmed'); return true; }
  if (process.argv.includes('--elevation-attempted')) throw new Error('提权后的进程仍未取得管理员权限，已停止重启循环。');
  const args = [...process.argv.slice(1).filter(value => typeof value === 'string'), '--elevation-attempted'];
  const quoteArgument = value => `"${String(value).replace(/(\\*)"/g, '$1$1\\"').replace(/(\\+)$/g, '$1$1')}"`;
  const argumentList = ` -ArgumentList ${quotePowerShell(args.map(quoteArgument).join(' '))}`;
  // Relaunch the portable shell, not its temporary inner EXE: the first shell
  // may remove its extraction folder as soon as this process exits.
  const portable = process.env.PORTABLE_EXECUTABLE_FILE;
  const target = portable && path.isAbsolute(portable) && /[.]exe$/i.test(portable) && fs.existsSync(portable) ? portable : process.execPath;
  const command = `$ErrorActionPreference='Stop'; $p=Start-Process -FilePath ${quotePowerShell(target)} -WorkingDirectory ${quotePowerShell(path.dirname(target))}${argumentList} -Verb RunAs -PassThru; Start-Sleep -Milliseconds 700; if($p.HasExited){throw '管理员进程启动后立即退出'}; $p.Id`;
  startup.log('elevation-requested');
  try { const childPid = await runPowerShell(command, 60000); startup.log('elevation-child-started', { pid: childPid }); }
  catch (error) { startup.log('elevation-failed', { message: error.message }); throw new Error('未能启动管理员进程：可能取消了提权、被系统阻止，或程序启动后立即退出。请导出启动诊断。'); }
  app.quit();
  return false;
}

function addonArgs(argv) {
  return (argv || []).filter(file => typeof file === 'string' && /(?:\.addon64|\.zip)$/i.test(file));
}

let executableIconCache = null;
async function iconDataFor(file, currentIcon = null) {
  if (typeof file !== 'string' || !path.isAbsolute(file)) return null;
  const current = typeof currentIcon === 'string' && currentIcon.length <= 1024 * 1024 ? currentIcon : null;
  let fallback = null, recognizedShellIcon = !current;
  for (const size of ['large', 'normal', 'small']) {
    try {
      const icon = await app.getFileIcon(file, { size });
      if (icon && !icon.isEmpty()) {
        const data = icon.toDataURL();
        fallback ||= data;
        if (current === data) recognizedShellIcon = true;
      }
    } catch {}
  }
  // Upgrade old automatically extracted shell icons only when their exact
  // bytes match. A user-selected icon is never replaced on a guess.
  if (!recognizedShellIcon) return current;
  try {
    executableIconCache ||= require('./src/product/executable-icon').createExecutableIconCache();
    const resource = executableIconCache.get(file);
    if (resource) return resource;
  } catch {}
  return fallback || current;
}

function createWindow() {
  win = new BrowserWindow({
    width: 1180,
    height: 780,
    minWidth: 900,
    minHeight: 620,
    backgroundColor: '#0c0e10',
    icon: path.join(__dirname, 'build', 'icon.png'),
    frame: false,
    autoHideMenuBar: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  });
  startup.log('window-created');
  windowWatch = watchWindow(win, { log: startup.log, fail: (...args) => { if (!quitting) void startupFailure(...args); } });
  win.loadFile(path.join(__dirname, 'src', 'renderer', 'index.html')).catch(error => { void startupFailure('界面文件加载失败', error); });
  win.on('closed', () => { win = null; });
}

function registerIpc() {
  ipcMain.on('startup-renderer-ready', event => { if (event.sender === win?.webContents) windowWatch?.ready(); });
  ipcMain.on('startup-renderer-failed', (event, message) => {
    if (event.sender === win?.webContents) windowWatch?.failed(new Error(String(message || '界面初始化失败').slice(0, 500)));
  });
  ipcMain.handle('startup-export', () => exportStartupReport());
  const loggedGameActions = new Set([
    'game-install', 'game-repair', 'game-upgrade-addon', 'game-uninstall',
    'game-toggle-d3d12', 'game-launch', 'game-diagnose', 'game-rename', 'game-hotkeys-read',
    'game-hotkey-write', 'nr-read', 'nr-write', 'nr-default', 'nr-recommended',
    'sr-model-read', 'sr-model-write', 'feedback-export', 'launch-settings-inspect',
    'launch-settings-save', 'launch-settings-preview', 'launch-settings-restore', 'launch-settings-recover',
    'fg-components-prepare', 'fg-components-restore', 'launch-settings-update', 'launch-settings-reset-all',
    'game-reframework-inspect', 'game-reframework-prepare', 'game-reframework-restore', 'game-reframework-recover'
  ]);
  const guardedActions = new Set(['game-install', 'game-repair', 'game-upgrade-addon', 'game-toggle-d3d12',
    'game-api-set', 'nr-write', 'nr-default', 'nr-recommended', 'game-hotkey-write', 'game-reframework-prepare', 'game-reframework-restore']);
  const serializedActions = new Set([...guardedActions, 'game-launch', 'game-uninstall', 'game-dismiss',
    'game-confirm', 'game-rename', 'settings-update', 'sr-model-write', 'launch-settings-save', 'launch-settings-preview',
    'launch-settings-apply', 'launch-settings-restore', 'launch-settings-recover', 'fg-components-prepare', 'fg-components-restore',
    'payload-source-pick', 'payload-source-reset', 'payload-source-recheck', 'launch-settings-update', 'launch-settings-reset-all',
    'game-reframework-recover']);
  const call = (name, fn) => ipcMain.handle(name, async (_event, ...args) => service.withError(
    () => {
      const work = async () => {
        if (guardedActions.has(name)) await launchCoordinator.assertMutationReady(args[0]);
        return fn(...args);
      };
      return serializedActions.has(name) ? launchCoordinator.serialize(work) : work();
    },
    { action: name, gameId: loggedGameActions.has(name) && typeof args[0] === 'string' ? args[0] : null }
  ));
  call('boot', async () => {
    const data = await service.boot();
    for (const warning of data.discoveryWarnings || []) startup.log('launcher-discovery-warning', { code: warning.code, message: warning.message });
    return data;
  });
  call('games-refresh', () => service.refresh());
  call('games-list', () => service.listGames());
  call('game-install', (id, options) => service.install(id, options));
  call('game-repair', (id, options) => service.repair(id, options));
  call('game-upgrade-addon', (id, version, options) => service.upgradeAddon(id, version, options));
  call('game-dismiss', id => launchCoordinator.dismiss(id));
  call('game-rename', (id, name) => service.renameGame(id, name));
  call('game-api-set', (id, api, options) => service.setGameApi(id, api, options));
  call('game-toggle-d3d12', (id, enabled, options) => service.toggleD3D12(id, enabled === true, options));
  call('game-uninstall', async (id, removeSettings) => {
    await launchCoordinator.restoreForUninstall(id);
    return service.uninstall(id, removeSettings === true);
  });
  call('game-launch', async id => {
    let result;
    try { result = await launchCoordinator.launch(id); }
    catch (error) {
      const outcomes = error?.details?.launchSettings;
      if (Array.isArray(outcomes) && win && !win.isDestroyed()) {
        win.webContents.send('launch-settings-applied', {
          id,
          outcomes: outcomes.filter(row => row?.applied === true || row?.skipped === true),
          launchFailed: true
        });
      }
      throw error;
    }
    if (win && !win.isDestroyed()) {
      if (result.srModel) win.webContents.send('sr-model-applied', { id, ...result.srModel });
      win.webContents.send('launch-settings-applied', { id, outcomes: result.launchSettings });
    }
    return result;
  });
  call('game-diagnose', id => service.diagnose(id));
  call('nr-read', id => service.readNrSettings(id));
  call('nr-write', (id, patch) => service.writeNrSettings(id, patch));
  call('sr-model-read', id => srModel.read(id));
  call('sr-model-write', (id, selection) => launchCoordinator.writeLegacySr(id, selection));
  call('launch-settings-inspect', id => launchCoordinator.inspect(id));
  call('launch-settings-save', (id, domain, request) => launchSettings.save(id, domain, request));
  call('launch-settings-preview', (id, domain, request) => launchSettings.preview(id, domain, request));
  call('launch-settings-apply', (planId, consent) => launchSettings.apply(planId, { confirm: consent?.confirm === true }));
  call('launch-settings-restore', (id, domain) => launchSettings.restore(id, domain));
  call('launch-settings-recover', id => launchSettings.recover(id));
  call('launch-settings-update', async (id, domain, input, options = {}) => {
    const request = require('./src/product/launch-settings-policy').validateRequest(domain, input);
    await launchSettings.assertReady(id);
    if (!require('./src/product/launch-settings-policy').isRestore(domain, request) && !/^nvngx_dlss\.dll$/i.test(service.gameScan(id)?.primaryDlss?.name || ''))
      throw Object.assign(new Error('未检测到此游戏的普通 DLSS 组件，请先确认选择的是实际游戏 EXE。'),{code:'ERR_NO_DLSS'});
    if (domain === 'fg' && request.mode !== 'restore') {
      const components = await fgComponents.inspect(id);
      const expectedRoute = request.backend === 'rtx40' ? 'compatibility' : 'native';
      if (components.route !== expectedRoute) throw Object.assign(new Error('所选补帧路线与当前显卡不一致，未准备或修改组件。'), { code: 'SETTINGS_FG_ROUTE_MISMATCH' });
      if (components.needsCleanup) {
        await launchSettings.restore(id, 'fg');
        await fgComponents.restore(id);
      } else if (!components.ready) await fgComponents.prepare(id, { allowAntiCheat: options.allowAntiCheat === true });
    }
    const plan = await launchSettings.preview(id, domain, request);
    const result = await launchSettings.apply(plan.id, { confirm: true, automatic: true });
    try { await launchSettings.save(id, domain, request); }
    catch (error) { throw Object.assign(new Error(`设置已应用，但未能记住选择：${error.message}`), { code: error.code || 'SETTINGS_SAVE_FAILED', details: { applied: true } }); }
    return { ...result, saved: true };
  });
  call('launch-settings-reset-all', async id => {
    const outcomes = [];
    for (const domain of ['fg', 'sr']) {
      try { const result = await launchSettings.restore(id, domain); outcomes.push({ domain, ok: true, result }); }
      catch (error) { outcomes.push({ domain, ok: false, code: error.code, message: error.message }); }
    }
    if (outcomes.some(row => !row.ok)) throw Object.assign(new Error(outcomes.filter(row => !row.ok).map(row => `${row.domain.toUpperCase()}：${row.message}`).join('；')),
      { code: 'SETTINGS_RESTORE_INCOMPLETE', details: { outcomes } });
    return { restored: true, outcomes };
  });
  call('fg-components-prepare', async (id, options) => {
    await launchSettings.assertReady(id);
    const result = await fgComponents.prepare(id, { allowAntiCheat: options?.allowAntiCheat === true });
    return service.refreshAfterMutation(result);
  });
  call('fg-components-restore', async id => {
    await launchSettings.assertReady(id);
    await launchSettings.restore(id, 'fg');
    const result = await fgComponents.restore(id);
    return service.refreshAfterMutation(result);
  });
  call('game-hotkeys-read', id => service.readGameHotkeys(id));
  call('game-reframework-inspect', id => service.readReframework(id));
  call('game-reframework-prepare', (id, options) => service.prepareReframework(id, options));
  call('game-reframework-restore', id => service.restoreReframework(id));
  call('game-reframework-recover', id => service.recoverReframework(id));
  call('game-hotkey-write', (id, target, binding) => service.writeGameHotkey(id, target, binding));
  call('nr-default', id => service.applyDefault(id));
  call('nr-recommended', id => service.applyDefault(id));
  call('settings-update', patch => service.updateSettings(patch));
  call('addon-list', () => service.listAddonVersions());
  call('payload-source-read', () => service.payloadState());
  call('payload-source-pick', async () => {
    const result = await dialog.showOpenDialog(win, { properties: ['openDirectory'], title: '选择完整组件目录',
      defaultPath: service.store.read().payloadSourcePath || service.payloadDir });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.selectPayloadSource(result.filePaths[0]);
  });
  call('payload-source-reset', () => service.selectPayloadSource(null));
  call('payload-source-recheck', () => service.recheckPayloadSource());
  call('addon-import', file => service.importAddonFile(file));
  call('addon-remove', id => service.removeAddonVersion(id));
  call('game-art', id => service.fetchGameArt(id));
  call('game-icon', async (id, currentIcon) => {
    const file = service.gameExecutable(id);
    return iconDataFor(file, currentIcon);
  });
  call('game-selection-icon', file => iconDataFor(file));

  call('pick-game', async () => {
    const result = await dialog.showOpenDialog(win, { properties: ['openDirectory'], title: '选择游戏安装目录' });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.prepareGameSelection(result.filePaths[0]);
  });
  call('pick-executable', async () => {
    const result = await dialog.showOpenDialog(win, { properties: ['openFile'], title: '选择游戏运行程序', filters: [{ name: 'Windows 游戏程序', extensions: ['exe'] }] });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.prepareGameSelection(result.filePaths[0], result.filePaths[0]);
  });
  call('game-confirm', selection => launchCoordinator.confirmSelection(selection));
  call('pick-game-icon', async () => {
    const result = await dialog.showOpenDialog(win, {
      properties: ['openFile'],
      title: '选择游戏图标',
      filters: [{ name: '图标或程序', extensions: ['ico', 'exe', 'dll'] }]
    });
    if (result.canceled || !result.filePaths[0]) return null;
    return { file: result.filePaths[0], icon: await iconDataFor(result.filePaths[0]) };
  });
  call('pick-addon', async () => {
    const result = await dialog.showOpenDialog(win, {
      properties: ['openFile'],
      title: '选择 DLSS 5 Addon 或标准 OTA 包',
      filters: [{ name: 'DLSS 5 Addon / 标准 OTA', extensions: ['addon64', 'zip'] }]
    });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.importAddonFile(result.filePaths[0]);
  });
  call('pick-scan-folder', async () => {
    const result = await dialog.showOpenDialog(win, { properties: ['openDirectory'], title: '选择游戏库目录' });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.addScanFolder(result.filePaths[0]);
  });
  call('open-external', async key => {
    const url = service.product[key];
    if (typeof url !== 'string' || !/^https:\/\//i.test(url)) return false;
    await shell.openExternal(url);
    return true;
  });
  call('open-folder', async id => {
    const dir = service.gameDirectory(id);
    return (await shell.openPath(dir)) === '';
  });
  call('copy-text', text => {
    if (typeof text !== 'string' || text.length > 1024 * 1024) return false;
    clipboard.writeText(text);
    return true;
  });
  call('feedback-export', async (id, options = {}) => {
    const report = await service.collectFeedback(id, options);
    const result = await dialog.showSaveDialog(win, {
      title: '保存问题反馈日志',
      defaultPath: path.join(app.getPath('downloads'), report.suggestedName),
      filters: [{ name: '反馈日志（TXT）', extensions: ['txt'] }]
    });
    if (result.canceled || !result.filePath) return null;
    await fs.promises.writeFile(result.filePath, report.text, 'utf8');
    return result.filePath;
  });
  call('feedback-build', (id, options = {}) => service.collectFeedback(id, options));

  ipcMain.on('window-minimize', () => win && win.minimize());
  ipcMain.on('window-close', () => win && win.close());
}

async function startApplication() {
  startup.log('electron-entry', { version: app.getVersion(), packaged: app.isPackaged });
  const reportIndex = process.argv.indexOf('--diagnostics-output');
  if (reportIndex >= 0) {
    const target = process.argv[reportIndex + 1];
    if (!target || !path.isAbsolute(target)) throw new Error('诊断输出需要指定完整文件路径。');
    await app.whenReady(); startup.exportTo(target); app.quit(); return;
  }
  if (process.argv.includes('--diagnostics')) { await app.whenReady(); await exportStartupReport(); app.quit(); return; }
  if (!await relaunchAsAdministrator()) return;
  if (!app.requestSingleInstanceLock({ startupNonce: startup.sessionId })) {
    startup.log('existing-instance-detected');
    if (await startup.waitForAcknowledgement()) { startup.log('existing-instance-focused'); app.quit(); }
    else await startupFailure('已有管理器进程，但没有确认窗口已显示', new Error('已有进程可能尚未完成启动或没有响应。请先导出诊断，再从任务管理器核对；不会自动结束进程或删除单实例文件。'));
    return;
  }
  startup.log('single-instance-acquired');
  app.on('second-instance', async (_event, argv, _cwd, additional) => {
    if (win && !win.isDestroyed()) { if (win.isMinimized()) win.restore(); win.show(); win.focus(); }
    startup.acknowledge(additional?.startupNonce);
    startup.log('second-instance-focus', { windowAvailable: Boolean(win) });
    if (!service) return;
    for (const file of addonArgs(argv)) {
      try { await service.importAddonFile(file); } catch {}
    }
    if (win && !win.isDestroyed()) win.webContents.send('addon-imported');
  });

  app.setAppUserModelId('com.xiaofeng.dlss5.manager');
  await app.whenReady();
  startup.log('electron-ready');
  {
    ({ createAppService } = require('./src/product/app-service'));
    ({ createSrModelService } = require('./src/product/sr-model-service'));
    ({ createLaunchSettingsService } = require('./src/product/launch-settings-service'));
    ({ createLaunchCoordinator } = require('./src/product/launch-coordinator'));
    installGuards = require('./src/core/install-guards');
    ({ createFgComponents } = require('./src/product/fg-components'));
    ({ classifyApi } = require('./src/product/game-support'));
    startup.log('business-modules-loaded');
    const userData = app.getPath('userData');
    service = createAppService({
      userData,
      resourcesPath: process.resourcesPath,
      appDir: __dirname,
      version: app.getVersion()
    });
    startup.log('state-store-opened');
    const configRecovery = service.store.readRecoveryStatus?.();
    if (configRecovery && !['ok', 'missing', 'unread'].includes(configRecovery.state)) {
      startup.log('settings-recovery', { state: configRecovery.state, reason: configRecovery.reason, message: configRecovery.message });
      void dialog.showMessageBox({ type: 'warning', title: '已保留旧配置', message: configRecovery.message,
        detail: '原配置和游戏备份没有被删除。需要协助时，可通过启动诊断导出当前状态。', buttons: ['知道了'] });
    }
    srModel = createSrModelService({
      userData,
      resourcesPath: process.resourcesPath,
      appDir: __dirname,
      gameDirectory: id => service.gameDirectory(id),
      gameExecutable: id => service.gameExecutable(id)
    });
    launchSettings = createLaunchSettingsService({
      userData, resourcesPath: process.resourcesPath, appDir: __dirname,
      gameDirectory: id => service.gameDirectory(id),
      gameExecutable: id => service.gameExecutable(id), legacySrModel: srModel,
      assertComponents: async id => {
        const state = await fgComponents.inspect(id);
        if (!state.ready) throw Object.assign(new Error(state.needsCleanup
          ? '请先切换为原生帧生成路线，处理旧兼容组件。'
          : '请先准备完整的帧生成兼容组件，再应用设置。'), { code: 'SETTINGS_COMPONENTS_NOT_READY' });
      }
    });
    fgComponents = createFgComponents({
      resourcesPath: process.resourcesPath, appDir: __dirname,
      gameDirectory: id => service.gameDirectory(id), gameExecutable: id => service.gameExecutable(id),
      scan: id => {
        const scan = service.gameScan(id);
        const live = rows => (rows || []).filter(row => row.bitness === 64 && fs.existsSync(row.path));
        const streamline = live(scan.streamlineFiles).map(row => row.name.toLowerCase());
        return { api: classifyApi(scan.chosen),
          streamlineFg: streamline.includes('sl.dlss_g.dll') && streamline.some(name => ['sl.interposer.dll', 'sl.common.dll'].includes(name)),
          reshadeAddon: Boolean(scan.reshade?.installed && scan.reshade?.addonSupport)
        };
      },
      getReShadeSource: () => {
        try { return service.reShadeSource(); } catch { return null; }
      }
    });
    launchCoordinator = createLaunchCoordinator({ service, settings: launchSettings, legacySrModel: srModel, guards: installGuards, components: fgComponents });
    for (const file of addonArgs(process.argv)) {
      try { await service.importAddonFile(file); } catch {}
    }
    registerIpc();
    createWindow();
  }
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => { startup.log('app-quitting'); service?.dispose?.(); });
}
void startApplication().catch(error => startupFailure('管理器初始化失败', error));
