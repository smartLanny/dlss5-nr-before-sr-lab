'use strict';

const path = require('path');
const { app, BrowserWindow, ipcMain, dialog, shell, clipboard } = require('electron');
const { createAppService } = require('./src/product/app-service');

let win = null;
let service = null;

function createWindow() {
  win = new BrowserWindow({
    width: 1180,
    height: 780,
    minWidth: 900,
    minHeight: 620,
    backgroundColor: '#0c0e10',
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
  const call = (name, fn) => ipcMain.handle(name, async (_event, ...args) => service.withError(() => fn(...args)));
  call('boot', () => service.boot());
  call('games-refresh', () => service.refresh());
  call('game-install', id => service.install(id));
  call('game-repair', id => service.repair(id));
  call('game-uninstall', (id, removeSettings) => service.uninstall(id, removeSettings === true));
  call('game-diagnose', id => service.diagnose(id));
  call('nr-read', id => service.readNrSettings(id));
  call('nr-write', (id, patch) => service.writeNrSettings(id, patch));
  call('nr-recommended', id => service.applyRecommended(id));
  call('settings-update', patch => service.updateSettings(patch));

  call('pick-game', async () => {
    const result = await dialog.showOpenDialog(win, { properties: ['openDirectory'], title: '选择游戏安装目录' });
    if (result.canceled || !result.filePaths[0]) return null;
    return service.addManualGame(result.filePaths[0]);
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

  ipcMain.on('window-minimize', () => win && win.minimize());
  ipcMain.on('window-close', () => win && win.close());
}

app.setAppUserModelId('com.xiaofeng.dlss5.manager');
app.whenReady().then(() => {
  service = createAppService({
    userData: app.getPath('userData'),
    resourcesPath: process.resourcesPath,
    appDir: __dirname,
    version: app.getVersion()
  });
  registerIpc();
  createWindow();
});
app.on('window-all-closed', () => app.quit());
