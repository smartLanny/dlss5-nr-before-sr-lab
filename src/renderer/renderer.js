'use strict';

const $ = id => document.getElementById(id);
const state = {
  product: null,
  settings: null,
  payload: null,
  games: [],
  activeView: 'games',
  expanded: null,
  repairGame: null,
  diagnostics: null,
  busy: false,
  pendingModal: null,
  saveTimers: new Map(),
  pendingPatches: new Map()
};

function unwrap(result) {
  if (!result || result.ok !== true) {
    const error = result && result.error ? result.error : { message: '操作失败。' };
    throw Object.assign(new Error(error.message), error);
  }
  return result.value;
}

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>'"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' })[c]);
}

function toast(message, error = false) {
  const el = $('toast');
  el.textContent = message;
  el.className = `toast show${error ? ' error' : ''}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { el.className = 'toast'; }, 3200);
}

function setBusy(value) {
  state.busy = value;
  document.body.classList.toggle('is-busy', value);
}

function supportBadge(game) {
  if (game.installed) return '<span class="badge good">已安装</span>';
  if (game.supported) return '<span class="badge good">支持安装</span>';
  return '<span class="badge bad">暂不支持</span>';
}

function poster(game) {
  if (game.poster) {
    const file = game.poster.replace(/\\/g, '/').replace(/^\/+/, '');
    return `<div class="poster"><img src="file:///${encodeURI(file)}" alt=""></div>`;
  }
  const first = escapeHtml((game.name || '?').trim().slice(0, 1).toUpperCase());
  return `<div class="poster">${first}</div>`;
}

function visibleGames() {
  const query = $('searchInput').value.trim().toLowerCase();
  return state.games.filter(game => !query || `${game.name} ${game.launcher}`.toLowerCase().includes(query));
}

function cardAction(game) {
  if (game.installed) return '<button class="button subtle card-open">设置</button>';
  if (game.supported && state.payload && state.payload.ready) return '<button class="button primary install-btn">一键安装</button>';
  if (game.supported) return '<button class="button" disabled>安装组件未准备</button>';
  return '<button class="button" disabled>无法安装</button>';
}

function gameDetail(game) {
  if (!game.installed) {
    const title = game.supported ? '已检测到可用的普通 DLSS 游戏' : '当前不满足基础版安装条件';
    const detail = game.supported
      ? (state.payload && state.payload.ready ? '可以直接一键安装。' : '兼容性已通过；准备好 release payload 后即可安装。')
      : game.supportText;
    return `<div class="game-detail"><div class="config-block"><h4>${escapeHtml(title)}</h4><p>${escapeHtml(detail)}</p><div class="path-line">${escapeHtml(game.dir)}</div></div></div>`;
  }
  return `<div class="game-detail" data-game-detail="${game.id}">
    <div class="config-grid">
      <div class="config-block">
        <h4>常用设置</h4>
        <div class="control-row"><label>启用插件</label><span></span><input class="switch nr-enabled" type="checkbox"></div>
        <div class="control-row"><label>渲染模式</label><div class="segmented mode-segment"><button data-value="2">性能优先</button><button data-value="1">画质优先</button></div><span></span></div>
        <div class="control-row"><label>AI 强度</label><div class="range-wrap"><input class="nr-intensity" type="range" min="0" max="2" step="0.05"><span class="value-pill intensity-value">1.20</span></div><span></span></div>
        <div class="control-row"><label>风格</label><div class="segmented style-segment"><button data-value="0">默认</button><button data-value="1">自然</button><button data-value="2">电影</button></div><span></span></div>
        <div class="control-row"><label>人脸保护</label><span></span><input class="switch nr-mask" type="checkbox"></div>
        <div class="config-actions"><button class="button recommended-btn">恢复晓风推荐</button><button class="button open-folder-btn">打开目录</button></div>
      </div>
      <div class="config-block component-block"><h4>安装状态</h4><div class="component-body">正在检查…</div><div class="config-actions"><button class="button repair-inline-btn">一键修复</button><button class="button danger uninstall-inline-btn">卸载</button></div></div>
    </div>
    <div class="path-line">${escapeHtml(game.dir)}</div>
  </div>`;
}

function renderGames() {
  const rows = visibleGames();
  $('gameSummary').textContent = `已找到 ${state.games.length} 个游戏 · 支持 ${state.games.filter(g => g.supported).length} 个`;
  $('gameList').innerHTML = rows.map(game => `<article class="game-card${state.expanded === game.id ? ' expanded' : ''}" data-id="${game.id}">
    <div class="game-card-head">
      ${poster(game)}
      <div class="game-meta"><div class="game-title"><h3>${escapeHtml(game.name)}</h3>${supportBadge(game)}</div><p>${escapeHtml(game.launcher)} · ${escapeHtml(game.chosen ? `${game.chosen.apiLabel} / ${game.chosen.bitness} 位` : game.supportText)}</p></div>
      <div class="card-action">${cardAction(game)}<span class="expand-arrow">⌄</span></div>
    </div>
    ${gameDetail(game)}
  </article>`).join('');
  $('emptyState').classList.toggle('hidden', state.games.length !== 0);
  bindGameCards();
  if (state.expanded) loadExpanded(state.expanded);
  renderRepairSelect();
}

function bindGameCards() {
  document.querySelectorAll('.game-card').forEach(card => {
    const id = card.dataset.id;
    card.querySelector('.game-card-head').onclick = event => {
      if (event.target.closest('.install-btn')) return;
      state.expanded = state.expanded === id ? null : id;
      renderGames();
    };
    const install = card.querySelector('.install-btn');
    if (install) install.onclick = event => { event.stopPropagation(); runAction(() => window.manager.install(id), '安装完成'); };
    const openFolder = card.querySelector('.open-folder-btn');
    if (openFolder) openFolder.onclick = () => window.manager.openFolder(id);
    const repair = card.querySelector('.repair-inline-btn');
    if (repair) repair.onclick = () => runAction(() => window.manager.repair(id), '修复完成');
    const uninstall = card.querySelector('.uninstall-inline-btn');
    if (uninstall) uninstall.onclick = () => confirmUninstall(id);
    const recommended = card.querySelector('.recommended-btn');
    if (recommended) recommended.onclick = async () => {
      await runAction(() => window.manager.recommended(id), '已恢复推荐设置', false);
      await loadExpanded(id);
    };
  });
}

async function loadExpanded(id) {
  const root = document.querySelector(`[data-game-detail="${id}"]`);
  if (!root) return;
  try {
    const [settings, diagnostic] = await Promise.all([
      window.manager.readNr(id).then(unwrap),
      window.manager.diagnose(id).then(unwrap)
    ]);
    root.querySelector('.nr-enabled').checked = Boolean(settings.Enabled);
    root.querySelector('.nr-mask').checked = Boolean(settings.AutoMask);
    root.querySelector('.nr-intensity').value = settings.Intensity;
    root.querySelector('.intensity-value').textContent = Number(settings.Intensity).toFixed(2);
    root.querySelectorAll('.mode-segment button').forEach(b => b.classList.toggle('active', Number(b.dataset.value) === Number(settings.Mode)));
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', Number(b.dataset.value) === Number(settings.Style)));
    root.querySelector('.component-body').innerHTML = diagnostic.components.map(row => `<div class="diag-row"><span class="diag-icon ${row.ok ? 'ok' : 'bad'}">${row.ok ? '●' : '×'}</span><span>${escapeHtml(row.label)}</span><span class="diag-detail">${escapeHtml(row.ok ? '完整' : row.detail || '异常')}</span></div>`).join('');
    bindNrControls(root, id);
  } catch (error) { toast(error.message, true); }
}

function queueSetting(id, patch, root) {
  state.pendingPatches.set(id, { ...(state.pendingPatches.get(id) || {}), ...patch });
  clearTimeout(state.saveTimers.get(id));
  state.saveTimers.set(id, setTimeout(async () => {
    const pending = state.pendingPatches.get(id) || {};
    state.pendingPatches.delete(id);
    try {
      const settings = unwrap(await window.manager.writeNr(id, pending));
      if (root.isConnected) root.querySelector('.intensity-value').textContent = Number(settings.Intensity).toFixed(2);
      toast('设置已保存，游戏内约半秒自动生效');
    } catch (error) { toast(error.message, true); }
  }, 180));
}

function bindNrControls(root, id) {
  root.querySelector('.nr-enabled').onchange = e => queueSetting(id, { Enabled: e.target.checked ? 1 : 0 }, root);
  root.querySelector('.nr-mask').onchange = e => queueSetting(id, { AutoMask: e.target.checked ? 1 : 0 }, root);
  root.querySelector('.nr-intensity').oninput = e => {
    root.querySelector('.intensity-value').textContent = Number(e.target.value).toFixed(2);
    queueSetting(id, { Intensity: Number(e.target.value) }, root);
  };
  root.querySelectorAll('.mode-segment button').forEach(button => button.onclick = () => {
    root.querySelectorAll('.mode-segment button').forEach(b => b.classList.toggle('active', b === button));
    queueSetting(id, { Mode: Number(button.dataset.value) }, root);
  });
  root.querySelectorAll('.style-segment button').forEach(button => button.onclick = () => {
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', b === button));
    queueSetting(id, { Style: Number(button.dataset.value) }, root);
  });
}

async function runAction(work, success, refresh = true) {
  if (state.busy) return;
  setBusy(true);
  try {
    unwrap(await work());
    toast(success);
    if (refresh) await refreshGames();
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}

async function refreshGames() {
  state.games = unwrap(await window.manager.refresh());
  renderGames();
  if (state.repairGame && !state.games.some(g => g.id === state.repairGame)) state.repairGame = null;
}

function renderRepairSelect() {
  const installed = state.games.filter(game => game.installed);
  const select = $('repairGameSelect');
  const current = state.repairGame || installed[0]?.id || '';
  state.repairGame = current;
  select.innerHTML = installed.length ? installed.map(game => `<option value="${game.id}">${escapeHtml(game.name)}</option>`).join('') : '<option value="">没有已安装的游戏</option>';
  select.value = current;
  select.disabled = !installed.length;
  $('repairBtn').disabled = !installed.length;
  $('copyDiagBtn').disabled = !installed.length;
  $('uninstallBtn').disabled = !installed.length;
  if (state.activeView === 'repair') loadRepairDiagnostic();
}

async function loadRepairDiagnostic() {
  const id = state.repairGame;
  if (!id) {
    $('diagnostics').innerHTML = '<p>先在“游戏”页面安装插件。</p>';
    state.diagnostics = null;
    return;
  }
  $('diagnostics').innerHTML = '<p>正在检查组件…</p>';
  try {
    state.diagnostics = unwrap(await window.manager.diagnose(id));
    $('diagnostics').innerHTML = `<h3>${state.diagnostics.complete ? '组件完整' : '发现问题'}</h3>${state.diagnostics.components.map(row => `<div class="diag-row"><span class="diag-icon ${row.ok ? 'ok' : 'bad'}">${row.ok ? '●' : '×'}</span><span>${escapeHtml(row.label)}</span><span class="diag-detail">${escapeHtml(row.ok ? '完整' : row.detail || '异常')}</span></div>`).join('')}`;
  } catch (error) { $('diagnostics').innerHTML = `<p>${escapeHtml(error.message)}</p>`; }
}

function diagnosticText() {
  const game = state.games.find(row => row.id === state.repairGame);
  if (!game || !state.diagnostics) return '';
  return [
    'DLSS 5 AI 超分管理器诊断',
    `版本：${state.product.version || ''}`,
    `游戏：${game.name}`,
    `目录：${game.dir}`,
    `接口：${game.chosen ? game.chosen.apiLabel : '未知'}`,
    ...state.diagnostics.components.map(row => `${row.label}：${row.ok ? '完整' : row.detail || '异常'}`)
  ].join('\n');
}

function confirmUninstall(id) {
  state.pendingModal = { type: 'uninstall', id };
  $('modalTitle').textContent = '卸载插件';
  $('modalBody').textContent = '将恢复管理器安装前的文件。默认保留你的 nr_before_sr.ini 参数设置。';
  $('removeSettingsLine').classList.remove('hidden');
  $('removeSettingsCheck').checked = false;
  $('modal').classList.remove('hidden');
}

function closeModal() {
  state.pendingModal = null;
  $('modal').classList.add('hidden');
  $('removeSettingsLine').classList.add('hidden');
}

function switchView(view) {
  state.activeView = view;
  document.querySelectorAll('.view').forEach(el => el.classList.toggle('active', el.id === `view-${view}`));
  document.querySelectorAll('.nav').forEach(el => el.classList.toggle('active', el.dataset.view === view));
  const titles = {
    games: ['我的游戏', '安装、设置和卸载都在同一个页面完成'],
    repair: ['问题修复', '检查组件完整性，并安全恢复缺失文件'],
    settings: ['设置', '只保留真正会影响使用的选项']
  };
  $('pageTitle').textContent = titles[view][0];
  $('pageSubtitle').textContent = titles[view][1];
  $('addGameBtn').classList.toggle('hidden', view !== 'games');
  $('refreshBtn').classList.toggle('hidden', view !== 'games');
  if (view === 'repair') loadRepairDiagnostic();
}

async function boot() {
  try {
    const data = unwrap(await window.manager.boot());
    state.product = data.product;
    state.settings = data.settings;
    state.payload = data.payload;
    state.games = data.games;
    document.title = state.product.name;
    $('brandName').textContent = state.product.name.replace(/管理器$/, '').trim();
    $('brandEdition').textContent = state.product.edition;
    $('authorText').textContent = `by ${state.product.author}`;
    $('settingsAuthor').textContent = state.product.author;
    $('versionText').textContent = `v${state.product.version}`;
    $('settingsVersion').textContent = state.product.version;
    $('scanDrivesToggle').checked = state.settings.scanDrives;
    $('bilibiliBtn').classList.toggle('hidden', !state.product.bilibiliUrl);
    $('qqBtn').classList.toggle('hidden', !state.product.qqGroup);
    $('updateBtn').classList.toggle('hidden', !state.product.releaseUrl);
    if (!state.payload.ready) {
      const problems = [...(state.payload.missing || []), ...(state.payload.invalid || [])];
      $('payloadNotice').textContent = `当前开发版安装组件未准备完成${problems.length ? `：${problems.join('、')}` : ''}。仍可扫描和查看兼容性；安装前请按 payload/nr-before-sr/README.md 生成 bundle.json。`;
      $('payloadNotice').classList.remove('hidden');
    }
    renderGames();
  } catch (error) { toast(error.message, true); }
}

document.querySelectorAll('.nav').forEach(button => button.onclick = () => switchView(button.dataset.view));
$('searchInput').oninput = renderGames;
$('refreshBtn').onclick = () => runAction(async () => { state.games = unwrap(await window.manager.refresh()); return true; }, '扫描完成', false).then(renderGames);
async function pickAndRefresh(picker, success) {
  if (state.busy) return;
  setBusy(true);
  try {
    const games = unwrap(await picker());
    if (!games) return;
    state.games = games;
    renderGames();
    toast(success);
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}
$('addGameBtn').onclick = $('emptyAddBtn').onclick = () => pickAndRefresh(window.manager.pickGame, '游戏已添加');
$('addFolderBtn').onclick = () => pickAndRefresh(window.manager.pickScanFolder, '游戏库已添加');
$('scanDrivesToggle').onchange = event => runAction(async () => {
  state.settings = unwrap(await window.manager.updateSettings({ scanDrives: event.target.checked }));
  state.games = unwrap(await window.manager.refresh());
  return true;
}, '扫描范围已更新', false).then(renderGames);
$('bilibiliBtn').onclick = () => window.manager.openExternal('bilibiliUrl');
$('qqBtn').onclick = async () => { await window.manager.copyText(state.product.qqGroup); toast('群号已复制'); };
$('updateBtn').onclick = () => window.manager.openExternal('releaseUrl');
$('repairGameSelect').onchange = event => { state.repairGame = event.target.value; loadRepairDiagnostic(); };
$('repairBtn').onclick = () => state.repairGame && runAction(() => window.manager.repair(state.repairGame), '修复完成').then(loadRepairDiagnostic);
$('uninstallBtn').onclick = () => state.repairGame && confirmUninstall(state.repairGame);
$('copyDiagBtn').onclick = async () => { const text = diagnosticText(); if (text) { await window.manager.copyText(text); toast('诊断信息已复制'); } };
$('modalCancel').onclick = closeModal;
$('modalConfirm').onclick = async () => {
  const pending = state.pendingModal;
  if (!pending) return;
  closeModal();
  if (pending.type === 'uninstall') {
    await runAction(() => window.manager.uninstall(pending.id, $('removeSettingsCheck').checked), '卸载完成');
    await loadRepairDiagnostic();
  }
};
$('minBtn').onclick = () => window.manager.minimize();
$('closeBtn').onclick = () => window.manager.close();
boot();
