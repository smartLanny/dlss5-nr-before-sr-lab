'use strict';

const path = require('node:path');
const { app, BrowserWindow, ipcMain, clipboard } = require('electron');

const PRODUCT_ID = 'cn.xiaofeng.dlss5-ai-rendering-manager';
const OVERLAY_PROFILE = 'xiaofeng-dlss5-ai-manager';

app.setAppUserModelId(PRODUCT_ID);

const mockGame = {
  id: 'cyberpunk-2077',
  name: '赛博朋克 2077',
  exe: 'Cyberpunk2077.exe',
  api: 'DirectX 12',
  gpu: 'RTX 5090',
  support: 'perfect',
  dlssInput: '1920 × 1080',
  output: '3840 × 2160',
  installed: false,
  running: false,
  recommendation: {
    mode: 'performance',
    intensity: 1.2,
    style: 'natural',
    faceProtection: true
  }
};

function createWindow() {
  const win = new BrowserWindow({
    width: 1180,
    height: 760,
    minWidth: 980,
    minHeight: 660,
    backgroundColor: '#0c0f14',
    title: 'DLSS 5 AI Rendering Manager',
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      sandbox: true,
      nodeIntegration: false
    }
  });

  win.loadFile(path.join(__dirname, 'src', 'index.html'));
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());

ipcMain.handle('prototype:bootstrap', () => ({
  productId: PRODUCT_ID,
  overlayProfile: OVERLAY_PROFILE,
  version: app.getVersion(),
  game: mockGame
}));

ipcMain.handle('prototype:diagnose', () => ({
  timestamp: new Date().toISOString(),
  checks: [
    { key: 'reshade', label: 'ReShade', state: 'ok', detail: '检测到支持 Add-on 的 ReShade' },
    { key: 'dlss', label: 'DLSS 超分', state: 'ok', detail: '普通 DLSS SR 路径可用' },
    { key: 'nr-runtime', label: 'AI 核心组件', state: 'ok', detail: '原型阶段：真实文件检查待接入' },
    { key: 'nr-chain', label: 'NR → SR 插件', state: 'warn', detail: '原型阶段：尚未连接真实运行状态' },
    { key: 'conflict', label: 'Mod 冲突', state: 'ok', detail: '未发现已知冲突（原型模拟）' }
  ]
}));

ipcMain.handle('prototype:copy-diagnostics', (_event, payload) => {
  const text = [
    'DLSS 5 AI Rendering Manager 诊断信息',
    `版本：${app.getVersion()}`,
    `产品ID：${PRODUCT_ID}`,
    `Overlay Profile：${OVERLAY_PROFILE}`,
    `游戏：${payload?.game?.name || '未知'}`,
    `GPU：${payload?.game?.gpu || '未知'}`,
    `API：${payload?.game?.api || '未知'}`,
    `输入：${payload?.game?.dlssInput || '未知'}`,
    `输出：${payload?.game?.output || '未知'}`,
    `模式：${payload?.settings?.mode || '未知'}`,
    `AI强度：${payload?.settings?.intensity ?? '未知'}`,
    '说明：当前为产品原型，部分检测项仍为模拟。'
  ].join('\n');
  clipboard.writeText(text);
  return text;
});
