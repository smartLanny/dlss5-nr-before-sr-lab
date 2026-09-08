'use strict';

const fs = require('fs');
const path = require('path');
const { spawn, execFileSync } = require('child_process');
const { app, BrowserWindow, ipcMain, dialog, shell, clipboard } = require('electron');
const { createAppService } = require('./src/product/app-service');
const { createSrModelService } = require('./src/product/sr-model-service');
const { createLaunchSettingsService } = require('./src/product/launch-settings-service');
const { createLaunchCoordinator } = require('./src/product/launch-coordinator');
const installGuards = require('./src/core/install-guards');
const { createFgComponents } = require('./src/product/fg-components');
const { classifyApi } = require('./src/product/game-support');

let win = null;
let service = null;
let srModel = null;
let launchSettings = null;
let launchCoordinator = null;
let fgComponents = null;

function isWindowsAdministrator() {
  if (process.platform !== 'win32') return true;
  try {
    const result = execFileSync('powershell.exe', [
      '-NoProfile',
      '-NonInteractive',
      '-Command',
      '$id=[Security.Principal.WindowsIdentity]::GetCurrent(); $p=New-Object Security.Principal.WindowsPrincipal($id); $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)'
    ], { encoding: 'utf8', windowsHide: true });
    return /^true$/i.test(String(result).trim());
  } catch {
    return false;
  }
}

function quotePowerShell(value) {
  return `'${String(value).replace(/'/g, "''")}'`;
}

function relaunchAsAdministrator() {
  if (isWindowsAdministrator()) return true;
  const args = process.argv.slice(1).filter(value => typeof value === 'string');
  const argumentList = args.length
    ? ` -ArgumentList @(${args.map(quotePowerShell).join(', ')})`
    : '';
  const command = `Start-Process -FilePath ${quotePowerShell(process.execPath)}${argumentList} -Verb RunAs`;
  try {
    const child = spawn('powershell.exe', [
      '-NoProfile',
      '-NonInteractive',
      '-WindowStyle',
      'Hidden',
      '-Command',
      command
    ], { detached: true, stdio: 'ignore', windowsHide: true });
    child.unref();
  } catch {}
  app.quit();
  return false;
}

function addonArgs(argv) {
  return (argv || []).filter(file => typeof file === 'string' && /(?:\.addon64|\.zip)$/i.test(file));
}

async function iconDataFor(file) {
  if (typeof file !== 'string' || !path.isAbsolute(file)) return null;
  // `small` asks Windows for a 16px shell icon. The renderer then enlarges it
  // to 52-58px, which is why manually added games looked noticeably blurry.
  // Prefer the largest shell resource and keep fallbacks for unusual EXEs.
  for (const size of ['large', 'normal', 'small']) {
    try {
      const icon = await app.getFileIcon(file, { size });
      if (icon && !icon.isEmpty()) return icon.toDataURL();
    } catch {}
  }
  return null;
}

const shouldContinue = relaunchAsAdministrator();
const hasSingleInstance = shouldContinue && app.requestSingleInstanceLock();

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
  win.loadFile(path.join(__dirname, 'src', 'renderer', 'index.html'));
  win.once('ready-to-show', () => win.show());
  win.on('closed', () => { win = null; });
}

function registerIpc() {
  const loggedGameActions = new Set([
    'game-install', 'game-repair', 'game-upgrade-addon', 'game-uninstall',
    'game-toggle-d3d12', 'game-launch', 'game-diagnose', 'game-hotkeys-read',
    'game-hotkey-write', 'nr-read', 'nr-write', 'nr-default', 'nr-recommended',
    'sr-model-read', 'sr-model-write', 'feedback-export', 'launch-settings-inspect',
    'launch-settings-save', 'launch-settings-preview', 'launch-settings-restore', 'launch-settings-recover',
    'fg-components-prepare', 'fg-components-restore'
  ]);
  const guardedActions = new Set(['game-install', 'game-repair', 'game-upgrade-addon', 'game-toggle-d3d12',
    'game-api-set', 'nr-write', 'nr-default', 'nr-recommended', 'game-hotkey-write']);
  const serializedActions = new Set([...guardedActions, 'game-launch', 'game-uninstall', 'game-dismiss',
    'game-confirm', 'settings-update', 'sr-model-write', 'launch-settings-save', 'launch-settings-preview',
    'launch-settings-apply', 'launch-settings-restore', 'launch-settings-recover', 'fg-components-prepare', 'fg-components-restore']);
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
  call('boot', () => service.boot());
  call('games-refresh', () => service.refresh());
  call('games-list', () => service.listGames());
  call('game-install', (id, options) => service.install(id, options));
  call('game-repair', (id, options) => service.repair(id, options));
  call('game-upgrade-addon', (id, version, options) => service.upgradeAddon(id, version, options));
  call('game-dismiss', id => launchCoordinator.dismiss(id));
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
  call('game-hotkey-write', (id, target, binding) => service.writeGameHotkey(id, target, binding));
  call('nr-default', id => service.applyDefault(id));
  call('nr-recommended', id => service.applyDefault(id));
  call('settings-update', patch => service.updateSettings(patch));
  call('addon-list', () => service.listAddonVersions());
  call('addon-import', file => service.importAddonFile(file));
  call('addon-remove', id => service.removeAddonVersion(id));
  call('game-art', id => service.fetchGameArt(id));
  call('game-icon', async id => {
    const file = service.gameExecutable(id);
    return iconDataFor(file);
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

if (!shouldContinue) {
  // The non-elevated process has handed off to a UAC-elevated child.
} else if (!hasSingleInstance) {
  app.quit();
} else {
  app.on('second-instance', async (_event, argv) => {
    if (win) { if (win.isMinimized()) win.restore(); win.focus(); }
    if (!service) return;
    for (const file of addonArgs(argv)) {
      try { await service.importAddonFile(file); } catch {}
    }
    if (win && !win.isDestroyed()) win.webContents.send('addon-imported');
  });

  app.setAppUserModelId('com.xiaofeng.dlss5.manager');
  app.whenReady().then(async () => {
    const userData = app.getPath('userData');
    service = createAppService({
      userData,
      resourcesPath: process.resourcesPath,
      appDir: __dirname,
      version: app.getVersion()
    });
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
  });
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => { service?.dispose?.(); });
}
