'use strict';
const { app, BrowserWindow, ipcMain } = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { assessEnhancementState } = require('../src/product/game-enhancement-capabilities');
const policy = require('../src/product/launch-settings-policy');
const { installMock, smoke, captureBaseline } = require('./helpers/game-page-beta2-fixture.cjs');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'game-page-ui-'));
app.setPath('userData', path.join(temporary, 'profile')); app.disableHardwareAcceleration();

// Both sides of confirmation use production assessment. Reintroducing the
// canConfirm regression must fail this actual-renderer test.
function featureFixture(series, domain, setting = 'on', confirmed = false) {
  const exe = 'C:\\UI-fixture\\Baldurs Gate 3\\bin\\bg3_dx11.exe', exeIdentity = 'a'.repeat(64);
  const driver = { available: true, version: 60000, settingIds: [...policy.IDS.sr, ...policy.IDS.fg] };
  return assessEnhancementState({ domain, request: { backend: domain === 'sr' ? 'native' : series === 'RTX40' ? 'mfgunlock' : 'nvidia' },
    hardware: { series: [series], source: 'fixture' }, driver,
    game: { exe, exeIdentity, support: { status: 'supported', source: series === 'RTX40' ? 'native-integration' : 'catalog',
      capabilities: series === 'RTX50' ? { dynamic: true, multipliers: [2, 3, 4] } : {} }, gameSetting: { state: setting, source: setting === 'on' ? 'fixture-game-config' : null } },
    confirmation: confirmed ? { domain, exe, exeIdentity, enabled: true, source: 'user-confirmation' } : null });
}
const features = { on40: { sr: featureFixture('RTX40', 'sr'), fg: featureFixture('RTX40', 'fg') },
  on50: { sr: featureFixture('RTX50', 'sr'), fg: featureFixture('RTX50', 'fg') },
  waitingSr: featureFixture('RTX40', 'sr', 'unknown'), confirmedSr: featureFixture('RTX40', 'sr', 'unknown', true) };
const captureOnly = process.env.GAME_UI_CAPTURE_ONLY === '1';
const preload = path.join(temporary, 'preload.cjs'); fs.writeFileSync(preload, `(${installMock.toString()})(${JSON.stringify(features)},${JSON.stringify({ captureOnly })});`);
let win;
ipcMain.handle('game-page-fixture-sr-plan', (event, request, hardware) => {
  if (event.sender !== win?.webContents) throw Error('unexpected fixture renderer');
  const validated = policy.validateRequest('sr', request);
  return validated.quality === 'game' ? [] : policy.nativeSr(validated, hardware).operations;
});
app.whenReady().then(async () => {
  win = new BrowserWindow({ width: Number(process.env.GAME_UI_WIDTH) || 1300, height: Number(process.env.GAME_UI_HEIGHT) || 1000, show: false, useContentSize: true,
    webPreferences: { preload, sandbox: true, contextIsolation: false, nodeIntegration: false, backgroundThrottling: false, offscreen: true } });
  try {
    await win.loadFile(path.resolve(__dirname, '../src/renderer/index.html'));
    const selectedTab = process.env.GAME_UI_TAB || 'overview';
    if (!['overview', 'enhance', 'maintenance'].includes(selectedTab)) throw Error('Unknown screenshot tab');
    const result = await win.webContents.executeJavaScript(`(${(captureOnly ? captureBaseline : smoke).toString()})(${JSON.stringify({ tab: selectedTab })})`);
    if (!captureOnly && process.env.GAME_UI_TAB) {
      const selected = process.env.GAME_UI_TAB; if (!['overview', 'enhance', 'maintenance'].includes(selected)) throw Error('Unknown screenshot tab');
      await win.webContents.executeJavaScript(`document.querySelector('.game-card.expanded [data-gp-tab="${selected}"]').click()`);
    }
    await new Promise(resolve => setTimeout(resolve, 150));
    if (process.argv[2]) fs.writeFileSync(path.resolve(process.argv[2]), (await win.webContents.capturePage()).toPNG());
    console.log(JSON.stringify({ ok: true, sandbox: win.webContents.getLastWebPreferences().sandbox, ...result }, null, 2)); win.destroy(); app.exit(0);
  } catch (error) {
    console.error(error.stack || error);
    try { console.error(await win.webContents.executeJavaScript(`JSON.stringify({ active: document.querySelector('.game-card.expanded')?.dataset.id, text: document.querySelector('.game-card.expanded .game-detail')?.innerText, calls: window.__gpMock?.calls.slice(-12) })`)); } catch {}
    win.destroy(); app.exit(1);
  }
});
