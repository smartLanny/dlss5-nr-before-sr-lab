'use strict';

// Run with: electron test/game-card-actions.electron.cjs
// This loads the real renderer in an isolated offscreen window. The manager
// IPC is mocked; no game directory, driver, registry, or installation file is touched.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'game-card-ui-smoke-'));
app.setPath('userData', path.join(temporary, 'profile'));
app.disableHardwareAcceleration();

const launchHarness = fs.readFileSync(path.join(__dirname, 'launch-settings-frontend.electron.cjs'), 'utf8');
const installMock = launchHarness.slice(launchHarness.indexOf('function installMock()'), launchHarness.indexOf('async function smoke()'));
const preload = path.join(temporary, 'preload.cjs');
fs.writeFileSync(preload, `${installMock}
installMock();
(() => {
  const ok = value => ({ ok: true, value: structuredClone(value) });
  const mock = window.__cardMock = { games: null, calls: [], installResolve: null };
  const originalBoot = window.manager.boot;
  const clone = () => structuredClone(mock.games || []);
  const seedGames = rows => {
    const seed = rows[0];
    const chosen = { ...seed.chosen, path: 'C:\\\\Games\\\\Scanned\\\\Game.exe', apiResolution: { api: 'dx12', source: 'fixture', evidence: [] }, detectedApiResolution: { api: 'dx12', source: 'fixture', evidence: [] } };
    return [
      { ...seed, id: 'ui-scanned', name: '扫描游戏', dir: 'C:\\\\Games\\\\Scanned', installed: false, supported: true, supportCode: null, supportText: '支持安装', chosen, apiOverride: 'auto' },
      { ...seed, id: 'ui-added', name: '已添加游戏', dir: 'C:\\\\Games\\\\Added', installed: true, supported: true, supportCode: null, supportText: '支持安装', chosen: { ...chosen, path: 'C:\\\\Games\\\\Added\\\\Game.exe' }, apiOverride: 'auto' }
    ];
  };
  window.manager.boot = async () => {
    const response = await originalBoot();
    if (!mock.games) mock.games = seedGames(response.value.games);
    response.value.games = clone();
    return response;
  };
  window.manager.listGames = async () => ok(clone());
  window.manager.renameGame = async (id, name) => {
    mock.calls.push(['rename', id, name]);
    const game = mock.games.find(row => row.id === id);
    if (game) game.name = name;
    return ok(clone());
  };
  window.manager.applyGameRoute = (id, options) => new Promise(resolve => {
    mock.calls.push(['install', id, options]);
    mock.installResolve = result => {
      if (result && result.ok === true) {
        const game = mock.games.find(row => row.id === id);
        if (game) game.installed = true;
      }
      resolve(result);
      mock.installResolve = null;
    };
  });
})();
`, 'utf8');

async function smoke() {
  const checks = [];
  const assert = (condition, description) => { if (!condition) throw new Error(description); checks.push(description); };
  const waitUntil = async predicate => {
    const deadline = Date.now() + 8000;
    while (!predicate()) {
      if (Date.now() > deadline) throw new Error('Timed out waiting for renderer state');
      await new Promise(resolve => setTimeout(resolve, 15));
    }
  };
  const card = id => document.querySelector(`[data-id="${id}"]`);
  const title = id => card(id)?.querySelector('h3')?.textContent;
  const waitForCall = (kind, id) => waitUntil(() => window.__cardMock.calls.some(row => row[0] === kind && (!id || row[1] === id)));
  const waitForModal = visible => waitUntil(() => document.getElementById('modal').classList.contains('hidden') !== visible);

  await waitUntil(() => card('ui-scanned') && card('ui-added'));
  assert(Boolean(card('ui-scanned').querySelector('.rename-game-btn')), 'Scanned games expose the rename entry');
  assert(Boolean(card('ui-added').querySelector('.rename-game-btn')), 'Added and installed games expose the same rename entry');
  assert(Boolean(card('ui-scanned').querySelector('.install-btn')), 'Supported uninstalled games expose one-click install');

  const scannedBefore = JSON.stringify(window.__cardMock.games.find(row => row.id === 'ui-scanned').chosen);
  card('ui-scanned').querySelector('.rename-game-btn').click();
  await waitForModal(true);
  const input = document.getElementById('renameGameInput');
  assert(input.value === '扫描游戏' && document.getElementById('modalBody').textContent.includes('API/EXE'), 'Rename modal pre-fills the scanned name and explains binding safety');
  input.value = '扫描游戏（新名称）';
  document.getElementById('modalConfirm').click();
  await waitForCall('rename', 'ui-scanned'); await waitForModal(false); await waitUntil(() => title('ui-scanned') === '扫描游戏（新名称）');
  assert(title('ui-scanned') === '扫描游戏（新名称）', 'Saving a scanned game name updates the visible card');
  assert(JSON.stringify(window.__cardMock.games.find(row => row.id === 'ui-scanned').chosen) === scannedBefore, 'Renaming preserves the scanned API/EXE binding');

  const renamesBeforeCancel = window.__cardMock.calls.filter(row => row[0] === 'rename').length;
  card('ui-added').querySelector('.rename-game-btn').click();
  await waitForModal(true);
  input.value = '取消不会保存';
  document.getElementById('modalCancel').click();
  await waitForModal(false);
  assert(window.__cardMock.calls.filter(row => row[0] === 'rename').length === renamesBeforeCancel && title('ui-added') === '已添加游戏', 'Cancel leaves an added game name unchanged');

  card('ui-added').querySelector('.rename-game-btn').click();
  await waitForModal(true);
  input.value = '按回车保存';
  input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
  await waitForCall('rename', 'ui-added'); await waitForModal(false); await waitUntil(() => title('ui-added') === '按回车保存');
  assert(title('ui-added') === '按回车保存', 'Enter submits the rename modal');

  card('ui-added').querySelector('.rename-game-btn').click();
  await waitForModal(true);
  input.value = '   ';
  document.getElementById('modalConfirm').click();
  assert(!document.getElementById('modal').classList.contains('hidden') && window.__cardMock.calls.filter(row => row[0] === 'rename').length === renamesBeforeCancel + 1, 'Blank names keep the modal open without an IPC write');
  document.getElementById('modalCancel').click();
  await waitForModal(false);

  const installButton = () => card('ui-scanned')?.querySelector('.install-btn');
  installButton().click();
  await waitForCall('install', 'ui-scanned');
  const pending = installButton();
  assert(pending.disabled && pending.getAttribute('aria-busy') === 'true' && pending.getAttribute('aria-live') === 'polite', 'Install pending disables the button and exposes ARIA busy state');
  assert(pending.querySelector('.button-spinner') && pending.textContent.includes('正在安装') && !/\\d+%/.test(pending.textContent), 'Install pending shows the real spinner label without a fake percentage');
  window.__cardMock.installResolve({ ok: false, error: { code: 'ERR_TEST_INSTALL', message: '模拟安装失败' } });
  await waitUntil(() => installButton() && !installButton().disabled && installButton().getAttribute('aria-busy') === 'false');
  assert(!installButton().querySelector('.button-spinner') && installButton().textContent.includes('一键安装'), 'Failed install clears the busy state and restores the button');

  installButton().click();
  await waitForCall('install', 'ui-scanned');
  assert(installButton().disabled && installButton().querySelector('.button-spinner'), 'A second install attempt re-enters the visible pending state');
  window.__cardMock.installResolve({ ok: true, value: { installed: true } });
  await waitUntil(() => card('ui-scanned')?.querySelector('.launch-btn'));
  assert(!card('ui-scanned').querySelector('.install-btn'), 'Completed install refreshes to the installed card and removes pending UI');
  return { checks };
}

app.whenReady().then(async () => {
  const win = new BrowserWindow({ width: 1100, height: 760, show: false, webPreferences: {
    preload, sandbox: false, contextIsolation: false, nodeIntegration: false, backgroundThrottling: false, offscreen: true
  } });
  try {
    await win.loadFile(path.join(process.env.MANAGER_UI_ROOT || path.resolve(__dirname, '..'), 'src/renderer/index.html'));
    const result = await win.webContents.executeJavaScript(`(${smoke.toString()})()`);
    console.log(JSON.stringify({ ok: true, ...result }, null, 2));
    win.destroy(); app.exit(0);
  } catch (error) {
    console.error(error.stack || error);
    win.destroy(); app.exit(1);
  }
});
