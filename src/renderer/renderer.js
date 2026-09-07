'use strict';

const $ = id => document.getElementById(id);
const state = {
  product: null,
  settings: null,
  payload: null,
  hardware: null,
  addons: [],
  games: [],
  activeView: 'games',
  expanded: null,
  expandedLoadToken: 0,
  repairGame: null,
  feedbackGame: null,
  lastFailureGame: null,
  diagnostics: null,
  busy: false,
  pendingModal: null,
  gameSelection: null,
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

const KEY_NAMES = Object.freeze({
  8: 'Backspace', 9: 'Tab', 13: 'Enter', 27: 'Esc', 32: 'Space',
  33: 'PageUp', 34: 'PageDown', 35: 'End', 36: 'Home', 37: 'Left',
  38: 'Up', 39: 'Right', 40: 'Down', 45: 'Insert', 46: 'Delete'
});

function hotkeyLabel(binding) {
  if (!binding) return '未设置';
  const key = Number(binding.key);
  let name = KEY_NAMES[key] || (key >= 112 && key <= 135 ? `F${key - 111}` : String.fromCharCode(key));
  if (key >= 48 && key <= 57) name = String.fromCharCode(key);
  const modifiers = [];
  if (binding.ctrl) modifiers.push('Ctrl');
  if (binding.shift) modifiers.push('Shift');
  if (binding.alt) modifiers.push('Alt');
  return [...modifiers, name].join('+');
}

function keyBindingFromEvent(event) {
  const code = String(event.code || '');
  let key = 0;
  if (/^F(?:[1-9]|1[0-9]|2[0-4])$/.test(code)) key = 111 + Number(code.slice(1));
  else if (/^Key[A-Z]$/.test(code)) key = code.charCodeAt(3);
  else if (/^Digit[0-9]$/.test(code)) key = code.charCodeAt(5);
  else if (/^Numpad[0-9]$/.test(code)) key = 96 + Number(code.slice(6));
  else if (code === 'Home') key = 36;
  else if (code === 'End') key = 35;
  else if (code === 'Insert') key = 45;
  else if (code === 'Delete') key = 46;
  else if (code === 'PageUp') key = 33;
  else if (code === 'PageDown') key = 34;
  else if (code === 'ArrowLeft') key = 37;
  else if (code === 'ArrowUp') key = 38;
  else if (code === 'ArrowRight') key = 39;
  else if (code === 'ArrowDown') key = 40;
  else if (code === 'Space') key = 32;
  else if (code === 'Tab') key = 9;
  else if (code === 'Enter') key = 13;
  if (!key) return null;
  return { key, ctrl: event.ctrlKey, shift: event.shiftKey, alt: event.altKey };
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
  if (game.supportCode === 'ERR_API_SELECTION_REQUIRED') return '<span class="badge">API 待确认</span>';
  if (game.supportCode === 'ERR_CARRIER_NOT_SELECTED') return '<span class="badge">兼容组件已关闭</span>';
  return '<span class="badge bad">暂不支持</span>';
}

function hardwareLabel() {
  if (!state.hardware) return '显卡待识别';
  if (state.hardware.family === 'RTX40') {
    const series = Array.isArray(state.hardware.series) ? state.hardware.series : [];
    return series.length ? `${series.map(value => value.replace('RTX', 'RTX ')).join(' / ')}（使用 RTX 40 兼容组件）` : 'RTX 20 / 30 / 40';
  }
  if (state.hardware.family === 'RTX50') return 'RTX 50';
  if (state.hardware.family === 'mixed') return '多套 RTX';
  return '显卡不支持自动匹配';
}

function payloadReadyForHardware() {
  if (!state.payload) return false;
  if (state.payload.versions) return Boolean(state.payload.ready && state.hardware && ['RTX40', 'RTX50'].includes(state.hardware.family));
  if (!state.payload.variants) return state.payload.ready;
  const family = state.hardware && state.hardware.family;
  return Boolean(family && state.payload.variants[family] && state.payload.variants[family].ready);
}

function refreshSelectedPayload(version) {
  if (!state.payload || !state.payload.versions) return;
  const versions = state.payload.versions;
  // Saved selections can outlive a bundled version after an upgrade. Keep the
  // backend's verified fallback instead of treating an absent key as bad files.
  const selectedVersion = [version, state.payload.selectedVersion, ...Object.keys(versions)]
    .find(id => Object.prototype.hasOwnProperty.call(versions, id));
  const item = versions[selectedVersion];
  const family = state.hardware && state.hardware.family;
  const variant = item && family && item.variants && item.variants[family];
  state.payload.selectedVersion = item ? selectedVersion : state.payload.selectedVersion;
  state.payload.hardwareFamily = family || null;
  state.payload.files = variant ? variant.files : [];
  state.payload.ready = Boolean(variant && variant.ready);
  state.payload.missing = variant ? variant.missing : [];
  state.payload.invalid = variant ? variant.invalid : [];
}

function renderPayloadNotice(removedSelection = false) {
  const notice = $('payloadNotice');
  if (!notice) return;
  const payload = state.payload;
  let message = '';
  if (!state.hardware || !['RTX40', 'RTX50'].includes(state.hardware.family)) {
    message = '未能确认可自动匹配的显卡，请在“设置”中检查显卡识别结果。';
  } else if (!payloadReadyForHardware()) {
    const problems = [...((payload && payload.missing) || []), ...((payload && payload.invalid) || [])];
    message = `安装组件缺失或校验未通过${problems.length ? `：${problems.join('、')}` : ''}。请重新下载完整的一键安装包；若仍有问题，请在“修复”页保存反馈日志。`;
  } else if (removedSelection) {
    const item = payload.versions[payload.selectedVersion];
    message = `上次选择的版本已不在此安装包中，当前使用 ${item.label || payload.selectedVersion}。可在“插件版本”中重新选择。`;
  }
  notice.textContent = message;
  notice.classList.toggle('hidden', !message);
}

function renderVersionSelector() {
  const select = $('addonVersionSelect');
  if (!select) return;
  const versions = state.payload && state.payload.versions;
  if (!versions || !Object.keys(versions).length) {
    select.innerHTML = '<option value="">随安装组件</option>';
    select.disabled = true;
    renderPayloadNotice();
    return;
  }
  const saved = state.settings.addonVersion;
  const removedSelection = Boolean(saved && !Object.prototype.hasOwnProperty.call(versions, saved));
  refreshSelectedPayload(saved);
  const selected = state.payload.selectedVersion;
  select.innerHTML = Object.entries(versions).map(([id, item]) => `<option value="${escapeHtml(id)}">${escapeHtml(item.label || id)}</option>`).join('');
  select.value = selected;
  const item = versions[selected];
  $('addonVersionNote').textContent = item && item.notes
    ? `${item.notes} 切换后点击游戏卡片里的“修复”即可对比。`
    : '安装和修复会使用这里选择的版本；切换后点击游戏卡片里的“修复”即可对比。';
  select.disabled = false;
  renderPayloadNotice(removedSelection);
}

function versionOptionsMarkup(selected = state.payload && state.payload.selectedVersion, includeImported = true) {
  const versions = state.payload && state.payload.versions ? Object.entries(state.payload.versions).map(([id, item]) => [id, item]) : [];
  const imported = includeImported ? (state.addons || []).filter(item => item.addonOnly).map(item => [item.id, item]) : [];
  const all = [...versions, ...imported];
  if (!all.length) return '<option value="">当前组件</option>';
  return all.map(([id, item]) => `<option value="${escapeHtml(id)}"${id === selected ? ' selected' : ''}>${escapeHtml(item.label || id)}${item.addonOnly ? '（导入）' : ''}</option>`).join('');
}

function poster(game) {
  // Steam art is authoritative. An EXE icon is only a fallback when Steam
  // has no poster/icon, so a slow artwork request cannot leave a blurry icon
  // permanently preferred over the later high-resolution cover.
  // Posters are the high-resolution source for every launcher. EXE icons are
  // only the fallback when no artwork could be found; this also lets manually
  // added games upgrade from a shell icon after the artwork lookup completes.
  const source = game.poster || game.icon;
  if (source) {
    const url = /^(?:file:|data:|https?:)/i.test(source)
      ? source
      : `file:///${encodeURI(source.replace(/\\/g, '/').replace(/^\/+/, ''))}`;
    return `<div class="poster"><img src="${escapeHtml(url)}" alt=""></div>`;
  }
  const first = escapeHtml((game.name || '?').trim().slice(0, 1).toUpperCase());
  return `<div class="poster">${first}</div>`;
}

function visibleGames() {
  const query = $('searchInput').value.trim().toLowerCase();
  const filter = $('gameFilter').value;
  return state.games.filter(game => {
    const matchesQuery = !query || `${game.name} ${game.launcher}`.toLowerCase().includes(query);
    const matchesFilter = filter === 'all' ||
      (filter === 'installed' && game.installed) ||
      (filter === 'supported' && !game.installed && game.supported) ||
      (filter === 'unsupported' && !game.supported);
    return matchesQuery && matchesFilter;
  });
}

function cardAction(game) {
  const dismiss = '<button class="icon-button dismiss-game-btn" title="移除这个游戏" aria-label="移除这个游戏">×</button>';
  if (game.installed) return `<button class="button primary launch-btn">启动游戏</button><button class="button subtle card-open">设置</button>${dismiss}`;
  if (game.supported && payloadReadyForHardware()) return `<button class="button primary install-btn">一键安装</button>${dismiss}`;
  if (game.supported) return `<button class="button" disabled>安装组件未准备</button>${dismiss}`;
  if (game.supportCode === 'ERR_API_SELECTION_REQUIRED') return `<button class="button subtle">请选择游戏 API</button>${dismiss}`;
  return `<button class="button" disabled>无法安装</button>${dismiss}`;
}

const CARRIER_TOOLTIP = '当前仅支持 DX11；需要完整配套的兼容核心、nrchain 与 DX11 carrier。关闭后 NR 路线不可用。此选项不修改游戏启动 API，需要自行在游戏或启动器中切换。DX12、API 未确认或 Vulkan 不部署 DX11 carrier。';

function carrierControl(game) {
  if (!game.chosen) return '';
  const resolution = game.chosen.apiResolution || {};
  const selectable = resolution.api === 'dx11';
  const enabled = selectable && (game.components ? game.components.dx11Carrier === true : true);
  const title = escapeHtml(CARRIER_TOOLTIP);
  return `<span class="carrier-option" title="${title}"><label class="check-line"><input class="carrier-component-check" type="checkbox" aria-label="兼容桥接（自动）"${enabled ? ' checked' : ''}${selectable ? '' : ' disabled'}>兼容桥接（自动）</label><button class="button subtle carrier-save-btn" type="button" title="${escapeHtml(selectable ? '应用兼容桥接选择' : '当前路线不部署 DX11 carrier')}"${selectable ? '' : ' disabled'}>应用桥接</button></span>`;
}

function apiControls(game) {
  if (!game.chosen) return '';
  const resolution = game.chosen.apiResolution || {};
  const labels = { dx11: 'DirectX 11', dx12: 'DirectX 12', mixed: '多条 API 线索，待选择', unknown: 'API 未确认', unsupported: '当前路线不支持' };
  const selected = game.apiOverride || 'auto';
  const options = [['auto', '自动（综合证据）'], ['dx11', 'DirectX 11'], ['dx12', 'DirectX 12']]
    .map(([id, label]) => `<option value="${id}"${id === selected ? ' selected' : ''}>${label}</option>`).join('');
  const deployment = resolution.api === 'dx11' ? 'DX11 需要所选核心的完整配套 nrchain 与 DX11 carrier；兼容桥接关闭时该 NR 路线不可用。'
    : resolution.api === 'dx12' ? 'DX12 不使用 DX11 carrier；保存 API 时移出旧桥接，核心与 nrchain 保留。'
    : 'API 未确认或为 Vulkan 时不部署 DX11 carrier；请在游戏或启动器中确认实际启动 API。';
  return `<div class="config-block"><h4>游戏 API 与部署</h4><div class="control-row"><label>实际启动 API</label><select class="game-api-select">${options}</select><button class="button subtle api-save-btn">保存 API</button></div><p>${escapeHtml(labels[resolution.api] || 'API 未确认')} · ${escapeHtml(deployment)}</p><p class="config-note">这里保存管理器的 API 路线，不会修改游戏启动 API；实际启动 API 需自行在游戏或启动器中切换。DX12 保存后会移出 DX11 桥接，其余配套在安装或修复时应用。</p><p class="config-note">${escapeHtml((resolution.evidence || []).join('；'))}</p></div>`;
}

function addonVersionRow(game, applyVersion = false) {
  const selected = game.installed ? (game.addonVersion || game.recommendedAddonVersion || (state.payload && state.payload.selectedVersion))
    : (game.recommendedAddonVersion || (state.payload && state.payload.selectedVersion));
  const options = versionOptionsMarkup(selected, Boolean(game.installed));
  const apply = applyVersion ? '<button class="button subtle version-apply-btn">应用版本</button>' : '';
  return `<div class="control-row addon-version-row"><label>${game.installed ? 'Addon 版本' : '核心版本'}</label><div class="addon-version-control"><select class="game-version-select">${options}</select>${carrierControl(game)}</div>${apply}</div>`;
}

function gameApiLabel(chosen) {
  const resolution = chosen && chosen.apiResolution;
  if (!resolution) return chosen && chosen.apiLabel || 'API 未确认';
  const labels = { dx11: 'DirectX 11', dx12: 'DirectX 12', mixed: 'API 混合线索', unknown: 'API 待确认', unsupported: '不支持的 API' };
  return `${labels[resolution.api] || 'API 待确认'}${resolution.source === 'override' ? '（已指定）' : ''}`;
}

function gameDetail(game) {
  if (!game.installed) {
    const title = game.supported ? '已检测到可用的普通 DLSS 游戏'
      : game.supportCode === 'ERR_API_SELECTION_REQUIRED' ? '请确认游戏实际启动 API' : '当前不满足基础版安装条件';
    const detail = game.supported
      ? (payloadReadyForHardware() ? `已匹配 ${hardwareLabel()} 组件，可以直接一键安装。` : '安装前检查已通过；请确认显卡型号并准备对应组件。')
      : game.supportText;
    return `<div class="game-detail">${apiControls(game)}<div class="config-block"><h4>${escapeHtml(title)}</h4><p>${escapeHtml(detail)}</p>${addonVersionRow(game)}<div class="path-line">${escapeHtml(game.dir)}</div></div></div>`;
  }
  return `<div class="game-detail" data-game-detail="${game.id}">
    ${apiControls(game)}
    <div class="config-grid">
      <div class="config-block">
        <h4>常用设置</h4>
        ${addonVersionRow(game, true)}
        <div class="control-row"><label>风格</label><div class="segmented style-segment"><button data-value="0">默认</button><button data-value="1">自然</button><button data-value="2">电影</button></div><span></span></div>
        <div class="control-row"><label>人脸保护</label><span></span><input class="switch nr-mask" type="checkbox"></div>
        <div class="control-row"><label>NR 颜色跟随</label><div class="range-wrap"><input class="nr-color-strength" type="range" min="0" max="1" step="0.01"><span class="value-pill nr-color-value">0.75</span></div><span></span></div>
        <div class="control-row skin-strength-row"><label>皮肤强度</label><div class="range-wrap"><input class="nr-skin-strength" type="range" min="-1" max="2" step="0.01"><span class="value-pill nr-skin-value">关闭</span></div><span></span></div>
        <p class="config-note">这里提供颜色跟随和人脸保护后的皮肤强度。NR 工作分辨率仍由游戏内 ReShade Overlay 管理，默认与渲染分辨率匹配，不在这里重复设置。</p>
        <div class="shortcut-block"><div class="shortcut-head"><h4>快捷键</h4><span>按游戏单独保存</span></div><div class="shortcut-row"><label>NR 效果切换</label><button class="button subtle nr-hotkey-btn" disabled>F6 开关</button></div><div class="shortcut-row"><label>ReShade 菜单</label><button class="button subtle reshade-hotkey-btn">Home</button></div><p class="config-note">NR 效果切换使用 F6 开关；ReShade 菜单键可以在这里改，下一次启动游戏时生效。</p></div>
        <div class="config-actions"><button class="button default-btn">恢复默认</button><button class="button open-folder-btn">打开目录</button></div>
      </div>
      <div class="config-block component-block"><h4>安装状态</h4><div class="component-body">正在检查…</div><div class="config-actions"><button class="button repair-inline-btn">一键修复</button><button class="button danger uninstall-inline-btn">卸载</button></div><div class="compat-actions"><button class="button subtle d3d12-btn">${game.d3d12Route ? '恢复 DXGI 入口' : '加载 D3D12 入口'}</button><span>${game.d3d12Route ? '当前已加载 D3D12 入口；如需恢复可点击“恢复 DXGI 入口”。' : '部分网游（异环、逆战）不兼容 DXGI，建议加载 D3D12。'}</span></div></div>
    </div>
    <div class="path-line">${escapeHtml(game.dir)}</div>
  </div>`;
}

function renderGames() {
  const rows = visibleGames();
  updateGameListMeta(rows);
  $('gameList').innerHTML = rows.map(game => `<article class="game-card${state.expanded === game.id ? ' expanded' : ''}" data-id="${game.id}">
    <div class="game-card-head">
      ${poster(game)}
      <div class="game-meta"><div class="game-title"><h3>${escapeHtml(game.name)}</h3>${supportBadge(game)}</div><p>${escapeHtml(game.launcher)} · ${escapeHtml(game.chosen ? `${gameApiLabel(game.chosen)} / ${game.chosen.bitness} 位` : game.supportText)}</p></div>
      <div class="card-action">${cardAction(game)}<span class="expand-arrow">⌄</span></div>
    </div>
    ${gameDetail(game)}
  </article>`).join('');
  bindGameCards();
  if (state.expanded) loadExpanded(state.expanded);
  renderRepairSelect();
  renderFeedbackSelect();
}

function visualKey(game) {
  return String(game && (game.dir || game.id || `${game.launcher}:${game.name}`) || '').toLowerCase();
}

function mergeGameVisuals(previous, next) {
  const cached = new Map((previous || []).map(game => [visualKey(game), game]));
  return (next || []).map(game => {
    const old = cached.get(visualKey(game));
    return {
      ...game,
      poster: game.poster || (old && old.poster) || null,
      icon: game.icon || (old && old.icon) || null
    };
  });
}

function updateGameListMeta(rows = visibleGames()) {
  $('gameSummary').textContent = `已找到 ${state.games.length} 个游戏 · 支持 ${state.games.filter(g => g.supported).length} 个`;
  const hasFilter = $('searchInput').value.trim() || $('gameFilter').value !== 'all';
  $('emptyState').querySelector('h3').textContent = hasFilter ? '没有匹配的游戏' : '还没有找到游戏';
  $('emptyState').querySelector('p').textContent = hasFilter ? '换个关键词或筛选条件试试。' : '先自动扫描 Steam、Epic、GOG；也可以选择游戏文件夹或实际运行 EXE。';
  $('emptyAddBtn').classList.toggle('hidden', Boolean(hasFilter));
  $('emptyState').classList.toggle('hidden', rows.length !== 0);
}

function bindGameCards() {
  document.querySelectorAll('.game-card').forEach(card => {
    const id = card.dataset.id;
    card.querySelector('.game-card-head').onclick = event => {
      if (event.target.closest('.install-btn') || event.target.closest('.launch-btn') || event.target.closest('.dismiss-game-btn')) return;
      state.expanded = state.expanded === id ? null : id;
      renderGames();
    };
    const install = card.querySelector('.install-btn');
    if (install) install.onclick = event => { event.stopPropagation(); const version = card.querySelector('.game-version-select')?.value; runConfirmedAction(allowAntiCheat => window.manager.install(id, { allowAntiCheat, version }), '安装完成', true, id); };
    const saveApi = card.querySelector('.api-save-btn');
    if (saveApi) saveApi.onclick = event => {
      event.stopPropagation();
      const api = card.querySelector('.game-api-select').value;
      const success = api === 'dx12'
        ? 'API 路线已保存；DX12 已移出桥接，其他组件请通过安装或修复应用'
        : 'API 路线已保存；其他组件请通过安装或修复应用';
      runConfirmedAction(allowAntiCheat => window.manager.setGameApi(id, api, { allowAntiCheat }), success, true, id);
    };
    const saveCarrier = card.querySelector('.carrier-save-btn');
    if (saveCarrier) saveCarrier.onclick = event => {
      event.stopPropagation();
      const enabled = card.querySelector('.carrier-component-check').checked;
      const installed = state.games.some(game => game.id === id && game.installed);
      runConfirmedAction(allowAntiCheat => window.manager.setGameCarrier(id, enabled, { allowAntiCheat }),
        enabled
          ? (installed ? '兼容桥接选择已应用；请查看安装状态确认组件是否可用' : '兼容桥接已设为自动；安装时应用完整配套')
          : '兼容桥接已关闭；核心与 nrchain 保留，NR 路线不可用', true, id);
    };
    const launch = card.querySelector('.launch-btn');
    if (launch) launch.onclick = event => { event.stopPropagation(); runAction(() => window.manager.launch(id), '游戏已启动', false); };
    const dismiss = card.querySelector('.dismiss-game-btn');
    if (dismiss) dismiss.onclick = event => { event.stopPropagation(); confirmDismissGame(id); };
    const openFolder = card.querySelector('.open-folder-btn');
    if (openFolder) openFolder.onclick = () => window.manager.openFolder(id);
    const repair = card.querySelector('.repair-inline-btn');
    if (repair) repair.onclick = () => runConfirmedAction(allowAntiCheat => window.manager.repair(id, { allowAntiCheat }), '修复完成', true, id);
    const uninstall = card.querySelector('.uninstall-inline-btn');
    if (uninstall) uninstall.onclick = () => confirmUninstall(id);
    const d3d12 = card.querySelector('.d3d12-btn');
    if (d3d12) d3d12.onclick = event => { event.stopPropagation(); confirmD3D12(id); };
    const defaultButton = card.querySelector('.default-btn');
    if (defaultButton) defaultButton.onclick = async () => {
      await runAction(() => window.manager.restoreDefault(id), '已恢复插件默认设置', false);
      await loadExpanded(id);
    };
    const applyVersion = card.querySelector('.version-apply-btn');
    if (applyVersion) applyVersion.onclick = event => {
      event.stopPropagation();
      const version = card.querySelector('.game-version-select').value;
      if (!version) return;
      const imported = (state.addons || []).find(item => item.id === version && item.addonOnly);
      runConfirmedAction(async allowAntiCheat => {
        if (imported) return window.manager.upgradeAddon(id, version, { allowAntiCheat });
        return window.manager.repair(id, { allowAntiCheat, version });
      }, `已切换到 ${version}`, true, id);
    };
  });
}

async function loadExpanded(id) {
  const loadToken = (state.expandedLoadToken || 0) + 1;
  state.expandedLoadToken = loadToken;
  const root = document.querySelector(`[data-game-detail="${id}"]`);
  if (!root) return;
  try {
    const [settings, diagnostic, hotkeys] = await Promise.all([
      window.manager.readNr(id).then(unwrap),
      window.manager.diagnose(id).then(unwrap),
      window.manager.readHotkeys(id).then(unwrap)
    ]);
    if (state.expandedLoadToken !== loadToken || state.expanded !== id) return;
    const versionSelect = root.querySelector('.game-version-select');
    const game = state.games.find(row => row.id === id);
    if (versionSelect) versionSelect.value = (game && game.addonVersion) || (state.payload && state.payload.selectedVersion) || versionSelect.value;
    root.querySelector('.nr-mask').checked = Boolean(settings.AutoMask);
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', Number(b.dataset.value) === Number(settings.Style)));
    const color = root.querySelector('.nr-color-strength');
    const colorValue = root.querySelector('.nr-color-value');
    if (color) color.value = String(settings.ColorStrength ?? 0.75);
    if (colorValue) colorValue.textContent = Number(settings.ColorStrength ?? 0.75).toFixed(2);
    const skin = root.querySelector('.nr-skin-strength');
    const skinValue = root.querySelector('.nr-skin-value');
    if (skin) skin.value = String(settings.SkinStructureStrength ?? -1);
    if (skinValue) skinValue.textContent = Number(settings.SkinStructureStrength ?? -1) < 0 ? '关闭' : Number(settings.SkinStructureStrength).toFixed(2);
    if (skin) skin.disabled = !settings.AutoMask;
    root.querySelector('.reshade-hotkey-btn').textContent = hotkeyLabel(hotkeys && hotkeys.reshade);
    root.querySelector('.nr-hotkey-btn').textContent = 'F6 开关';
    root.querySelector('.component-body').innerHTML = diagnostic.components.map(row => `<div class="diag-row"><span class="diag-icon ${row.ok ? 'ok' : 'bad'}">${row.ok ? '●' : '×'}</span><span>${escapeHtml(row.label)}</span><span class="diag-detail">${escapeHtml(row.detail || (row.ok ? '完整' : '异常'))}</span></div>`).join('');
    bindNrControls(root, id);
    bindHotkeyControls(root, id, hotkeys);
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
      toast('设置已保存，游戏内约半秒自动生效');
    } catch (error) { toast(error.message, true); }
  }, 180));
}

function bindNrControls(root, id) {
  root.querySelector('.nr-mask').onchange = e => {
    const enabled = e.target.checked;
    const skin = root.querySelector('.nr-skin-strength');
    if (skin) skin.disabled = !enabled;
    queueSetting(id, { AutoMask: enabled ? 1 : 0 }, root);
  };
  root.querySelectorAll('.style-segment button').forEach(button => button.onclick = () => {
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', b === button));
    queueSetting(id, { Style: Number(button.dataset.value) }, root);
  });
  const color = root.querySelector('.nr-color-strength');
  const colorValue = root.querySelector('.nr-color-value');
  if (color) color.oninput = () => {
    if (colorValue) colorValue.textContent = Number(color.value).toFixed(2);
    queueSetting(id, { ColorStrength: Number(color.value) }, root);
  };
  const skin = root.querySelector('.nr-skin-strength');
  const skinValue = root.querySelector('.nr-skin-value');
  if (skin) skin.oninput = () => {
    const value = Number(skin.value);
    if (skinValue) skinValue.textContent = value < 0 ? '关闭' : value.toFixed(2);
    queueSetting(id, { SkinStructureStrength: value }, root);
  };
}

function bindHotkeyControls(root, id, hotkeys) {
  const button = root.querySelector('.reshade-hotkey-btn');
  if (!button) return;
  button.onclick = event => {
    event.stopPropagation();
    confirmHotkey(id, 'reshade', hotkeys && hotkeys.reshade);
  };
}

async function runAction(work, success, refresh = true, gameId = null) {
  if (state.busy) return;
  setBusy(true);
  try {
    const outcome = await window.executeUiAction(
      () => Promise.resolve(work()).then(unwrap),
      refresh ? refreshGames : null
    );
    if (!outcome.completed) throw outcome.error;
    toast(success);
    if (outcome.refreshError) queueRefreshRetry();
  } catch (error) {
    if (!error.silent) {
      if (gameId) {
        state.lastFailureGame = gameId;
        state.feedbackGame = gameId;
        renderFeedbackSelect();
      }
      const code = error.code && error.code !== 'ERR_INTERNAL' ? ` [${error.code}]` : '';
      const hint = gameId ? '；可在“修复”页保存反馈日志' : '';
      toast(`${error.message}${code}${hint}`, true);
    }
  }
  finally { setBusy(false); }
}

function queueRefreshRetry(attempt = 0) {
  if (queueRefreshRetry.timer) return;
  queueRefreshRetry.timer = setTimeout(async () => {
    queueRefreshRetry.timer = null;
    try {
      await refreshGames();
    } catch {
      if (attempt < 2) queueRefreshRetry(attempt + 1);
    }
  }, 250 * (attempt + 1));
}

async function refreshGames() {
  state.games = mergeGameVisuals(state.games, unwrap(await window.manager.refresh()));
  renderGames();
  scheduleArtworkEnrichment();
  if (state.repairGame && !state.games.some(g => g.id === state.repairGame)) state.repairGame = null;
}

async function runConfirmedAction(work, success, refresh = true, gameId = null) {
  return runAction(async () => {
    try {
      return await window.resolveUiEnvelope(() => work(false), unwrap);
    } catch (error) {
      if (error.code !== 'ERR_ANTI_CHEAT_CONFIRM') throw error;
      setBusy(false);
      const confirmed = await confirmAntiCheat(gameId);
      if (!confirmed) {
        const cancelled = new Error('已取消操作');
        cancelled.silent = true;
        throw cancelled;
      }
      setBusy(true);
      return await window.resolveUiEnvelope(() => work(true), unwrap);
    }
  }, success, refresh, gameId);
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

function renderFeedbackSelect() {
  const select = $('feedbackGameSelect');
  if (!select) return;
  const current = state.feedbackGame || state.lastFailureGame || state.games[0]?.id || '';
  state.feedbackGame = state.games.some(game => game.id === current) ? current : (state.games[0]?.id || '');
  select.innerHTML = state.games.length
    ? state.games.map(game => `<option value="${escapeHtml(game.id)}">${escapeHtml(game.name)}${game.installed ? '（已安装）' : ''}</option>`).join('')
    : '<option value="">没有可反馈的游戏</option>';
  select.value = state.feedbackGame;
  select.disabled = !state.games.length;
  $('copyFeedbackBtn').disabled = !state.games.length;
  $('exportFeedbackBtn').disabled = !state.games.length;
}

function renderAddonVersions() {
  const root = $('addonVersionList');
  if (!root) return;
  const rows = state.addons || [];
  $('addonListHint').textContent = rows.length ? `${rows.length} 个版本 · 内置版本不可删除` : '还没有可用版本';
  root.innerHTML = rows.length ? rows.map(item => `<div class="addon-row">
    <div class="addon-symbol">${item.addonOnly ? '↓' : '◆'}</div>
    <div><div class="addon-name">${escapeHtml(item.label || item.id)}</div><div class="addon-meta">${item.addonOnly ? `导入文件：${escapeHtml(item.sourceName || '')} · ${item.ota ? '标准 OTA，校验后只替换 Addon' : '只替换 Addon'}` : escapeHtml(item.notes || '完整安装组件')}</div></div>
    <span class="addon-tag">${item.ready ? '可用' : '组件未准备'}</span>
    ${item.deletable ? `<button class="button danger addon-remove-btn" data-id="${escapeHtml(item.id)}">删除</button>` : '<span class="addon-tag">内置</span>'}
  </div>`).join('') : '<p class="muted">点击上面的“选择 addon 文件”，或把 .addon64 拖进来。</p>';
  root.querySelectorAll('.addon-remove-btn').forEach(button => button.onclick = () => confirmRemoveAddon(button.dataset.id));
}

function confirmRemoveAddon(id) {
  const item = (state.addons || []).find(row => row.id === id);
  state.pendingModal = { type: 'remove-addon', id };
  $('modalTitle').textContent = '删除导入的 Addon';
  $('modalBody').textContent = `将从管理器版本库移除“${item ? item.label : id}”。不会删除已经安装到游戏目录的文件。`;
  $('removeSettingsLine').classList.add('hidden');
  $('modal').classList.remove('hidden');
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
    $('diagnostics').innerHTML = `<h3>${state.diagnostics.complete ? '组件完整' : '发现问题'}</h3>${state.diagnostics.components.map(row => `<div class="diag-row"><span class="diag-icon ${row.ok ? 'ok' : 'bad'}">${row.ok ? '●' : '×'}</span><span>${escapeHtml(row.label)}</span><span class="diag-detail">${escapeHtml(row.detail || (row.ok ? '完整' : '异常'))}</span></div>`).join('')}`;
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
    ...state.diagnostics.components.map(row => `${row.label}：${row.detail || (row.ok ? '完整' : '异常')}`)
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

function confirmDismissGame(id) {
  const game = state.games.find(row => row.id === id);
  state.pendingModal = { type: 'dismiss-game', id };
  $('modalTitle').textContent = '移除这个条目';
  $('modalBody').textContent = `确认从游戏列表移除“${game ? game.name : id}”？不会删除游戏文件；之后可以重新选择游戏目录添加。移除记录会保留，重新扫描时不会自动回来。`;
  $('removeSettingsLine').classList.add('hidden');
  $('modal').classList.remove('hidden');
}

async function dismissGameFromList(id) {
  if (state.busy) return;
  setBusy(true);
  try {
    const next = mergeGameVisuals(state.games, unwrap(await window.manager.dismissGame(id)));
    state.games = next;
    if (state.expanded === id) state.expanded = null;
    const card = [...document.querySelectorAll('.game-card')].find(row => row.dataset.id === id);
    if (card) card.remove();
    updateGameListMeta();
    renderRepairSelect();
    toast('已从列表移除');
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}

function confirmD3D12(id) {
  const game = state.games.find(row => row.id === id);
  const enabled = !(game && game.d3d12Route);
  state.pendingModal = { type: 'd3d12', id, enabled };
  $('modalTitle').textContent = enabled ? '加载 D3D12 入口' : '恢复 DXGI 入口';
  $('modalBody').textContent = enabled
    ? '会把管理器的 ReShade 入口从 dxgi.dll 切换为 d3d12.dll，并保留 .bak。部分网游有反作弊或自带保护，若仍无法启动请恢复 DXGI 或卸载插件。'
    : '会把管理器的 ReShade 入口恢复为 dxgi.dll。游戏必须先完全退出。';
  $('removeSettingsLine').classList.add('hidden');
  $('modal').classList.remove('hidden');
}

function confirmAntiCheat(id = null) {
  return new Promise(resolve => {
    const game = id ? state.games.find(row => row.id === id) : null;
    state.pendingModal = { type: 'anti-cheat', resolve };
    $('modalTitle').textContent = '反作弊风险提示';
    $('modalBody').textContent = `检测到“${game ? game.name : '该游戏'}”目录中存在可能的反作弊组件。继续安装可能导致游戏无法启动、触发安全策略或封禁风险。管理器不会修改、删除或绕过反作弊文件；只有你确认后才会继续写入插件文件。`;
    $('removeSettingsLine').classList.add('hidden');
    $('modalConfirm').textContent = '确认继续安装';
    $('modal').classList.remove('hidden');
  });
}

function confirmHotkey(id, target, current) {
  state.pendingModal = { type: 'hotkey', id, target, binding: null };
  $('modalTitle').textContent = target === 'reshade' ? '设置 ReShade 菜单快捷键' : '设置 NR 效果切换快捷键';
  $('modalBody').textContent = `当前快捷键：${hotkeyLabel(current)}。请按下新的组合键，再点击确认保存。`;
  $('removeSettingsLine').classList.add('hidden');
  $('modalConfirm').textContent = '保存快捷键';
  $('modalConfirm').disabled = true;
  $('modal').classList.remove('hidden');
}

function closeModal(result = false) {
  const pending = state.pendingModal;
  state.pendingModal = null;
  $('modal').classList.add('hidden');
  $('removeSettingsLine').classList.add('hidden');
  $('modalConfirm').textContent = '确认';
  $('modalConfirm').disabled = false;
  if (pending && pending.type === 'anti-cheat' && typeof pending.resolve === 'function') pending.resolve(result === true);
}

function switchView(view) {
  state.activeView = view;
  document.querySelectorAll('.view').forEach(el => el.classList.toggle('active', el.id === `view-${view}`));
  document.querySelectorAll('.nav').forEach(el => el.classList.toggle('active', el.dataset.view === view));
  const titles = {
    games: ['我的游戏', '安装、设置和卸载都在同一个页面完成'],
    repair: ['问题修复', '检查组件完整性，并安全恢复缺失文件'],
    settings: ['设置', '只保留真正会影响使用的选项'],
    addons: ['插件版本管理', '导入、删除和切换 Addon，方便退出游戏后快速 A/B 对比']
  };
  $('pageTitle').textContent = titles[view][0];
  $('pageSubtitle').textContent = titles[view][1];
  $('addGameBtn').classList.toggle('hidden', view !== 'games');
  $('refreshBtn').classList.toggle('hidden', view !== 'games');
  if (view === 'repair') loadRepairDiagnostic();
  if (view === 'addons') renderAddonVersions();
}

async function boot() {
  try {
    const data = unwrap(await window.manager.boot());
    state.product = data.product;
    state.settings = data.settings;
    state.hardware = data.hardware;
    state.payload = data.payload;
    state.addons = data.addons || [];
    state.games = data.games;
    document.title = state.product.name;
    $('brandName').textContent = state.product.name.replace(/管理器$/, '').trim();
    $('brandEdition').textContent = state.product.edition;
    $('authorText').textContent = `by ${state.product.author}`;
    $('settingsAuthor').textContent = state.product.author;
    $('versionText').textContent = `v${state.product.version}`;
    $('settingsVersion').textContent = state.product.version;
    $('settingsHardware').textContent = hardwareLabel();
    renderVersionSelector();
    $('scanDrivesToggle').checked = state.settings.scanDrives;
    $('bilibiliBtn').classList.toggle('hidden', !state.product.bilibiliUrl);
    $('qqBtn').textContent = state.product.qqGroup ? `QQ群二群：${state.product.qqGroup}` : '加入交流群';
    $('qqBtn').classList.toggle('hidden', !state.product.qqGroup);
    $('updateBtn').classList.toggle('hidden', !state.product.releaseUrl);
    const gpu = state.hardware;
    $('hardwareNotice').textContent = gpu && ['RTX40', 'RTX50'].includes(gpu.family)
      ? `已识别 ${hardwareLabel()}，安装时会自动选择对应版本。`
      : '未能确认 RTX 20 / 30 / 40 / 50 系列显卡，安装器不会误用不匹配组件。';
    $('hardwareNotice').classList.remove('hidden');
    renderGames();
    renderAddonVersions();
    setTimeout(enrichMissingArtwork, 80);
  } catch (error) { toast(error.message, true); }
}

async function enrichMissingArtwork() {
  for (const game of state.games) {
    let changed = false;
    // Manual games do not have a reliable store identity. Do not search the
    // web for a guessed poster; keep their EXE icon (now extracted at the
    // largest Windows shell size) unless the user supplied artwork elsewhere.
    if (!game.poster && game.launcher !== '手动添加') {
      try {
        const poster = unwrap(await window.manager.fetchGameArt(game.id));
        if (poster) { game.poster = poster; changed = true; }
      } catch {}
    }
    if (!game.poster && !game.icon) {
      try {
        const icon = unwrap(await window.manager.getGameIcon(game.id));
        if (icon) { game.icon = icon; changed = true; }
      } catch {}
    }
    if (changed) updateGameCardArtwork(game);
  }
}

function updateGameCardArtwork(game) {
  const card = [...document.querySelectorAll('.game-card')].find(row => row.dataset.id === game.id);
  const current = card && card.querySelector('.poster');
  if (!current) return;
  const wrapper = document.createElement('div');
  wrapper.innerHTML = poster(game);
  const next = wrapper.firstElementChild;
  if (next) current.replaceWith(next);
}

function scheduleArtworkEnrichment() {
  clearTimeout(scheduleArtworkEnrichment.timer);
  scheduleArtworkEnrichment.timer = setTimeout(() => enrichMissingArtwork(), 80);
}

function formatExeSize(bytes) {
  const value = Number(bytes) || 0;
  if (value >= 1024 * 1024 * 1024) return `${(value / 1024 / 1024 / 1024).toFixed(1)} GB`;
  return `${(value / 1024 / 1024).toFixed(1)} MB`;
}

function selectedGameCandidate() {
  const selection = state.gameSelection;
  if (!selection) return null;
  return selection.candidates.find(candidate => candidate.path === selection.selectedPath) || selection.chosen || null;
}

function renderGameIconPreview() {
  const preview = $('gameIconPreview');
  if (!preview) return;
  const icon = state.gameSelection && state.gameSelection.icon;
  preview.innerHTML = icon
    ? `<img src="${escapeHtml(icon)}" alt="">`
    : '<span>?</span>';
}

async function selectGameCandidate(index) {
  const selection = state.gameSelection;
  if (!selection || !selection.candidates[index]) return;
  const candidate = selection.candidates[index];
  selection.selectedPath = candidate.path;
  document.querySelectorAll('.game-candidate').forEach((row, rowIndex) => row.classList.toggle('selected', rowIndex === index));
  const nameInput = $('gameNameInput');
  if (nameInput && !selection.nameTouched) nameInput.value = selection.name || candidate.name.replace(/\.exe$/i, '');
  try { selection.icon = unwrap(await window.manager.getExecutableIcon(candidate.path)); } catch { selection.icon = null; }
  renderGameIconPreview();
}

function renderGameSelection() {
  const selection = state.gameSelection;
  if (!selection) return;
  const candidates = selection.candidates || [];
  $('gamePickerPath').textContent = selection.root;
  $('gamePickerHint').textContent = candidates.length
    ? '已自动找到可能的运行程序；默认根据路径结构、程序名、API 和 DLSS 关联选择本体，启动器/报告程序会标成辅助项。文件大小仅作参考，不作为硬性门槛。'
    : '这个目录没有检测到可用的图形程序，请返回后选择更上层的游戏目录或实际 EXE。';
  $('gameCandidateList').innerHTML = candidates.length ? candidates.map((candidate, index) => `
    <button type="button" class="game-candidate${candidate.path === selection.selectedPath ? ' selected' : ''}" data-index="${index}">
      <span class="candidate-check">${candidate.path === selection.selectedPath ? '✓' : ''}</span>
      <span class="candidate-main"><strong>${escapeHtml(candidate.rel || candidate.name)}</strong><small>${escapeHtml(candidate.apiLabel || '图形 API 未知')} · ${candidate.bitness || '?'} 位 · ${formatExeSize(candidate.size)}</small></span>
      <span class="candidate-state">${candidate.helper ? '辅助程序' : (candidate.recommended ? '建议' : '手动可选')}</span>
    </button>`).join('') : '<p class="muted">没有候选程序。</p>';
  $('gameCandidateList').querySelectorAll('.game-candidate').forEach(button => {
    button.onclick = () => selectGameCandidate(Number(button.dataset.index));
  });
  const candidate = selectedGameCandidate();
  $('gameNameInput').value = selection.name || (candidate && candidate.name ? candidate.name.replace(/\.exe$/i, '') : '');
  renderGameIconPreview();
  $('confirmGameBtn').disabled = !candidate;
  $('gamePickerModal').classList.remove('hidden');
  if (candidate) {
    const index = candidates.findIndex(row => row.path === candidate.path);
    if (index >= 0) selectGameCandidate(index);
  }
}

async function openGameSelection(picker) {
  if (state.busy) return;
  setBusy(true);
  try {
    const selection = unwrap(await picker());
    if (!selection) return;
    state.gameSelection = { ...selection, selectedPath: selection.chosen && selection.chosen.path, nameTouched: false, icon: null };
    renderGameSelection();
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}

function closeGameSelection() {
  state.gameSelection = null;
  $('gamePickerModal').classList.add('hidden');
}

async function confirmGameSelection() {
  const selection = state.gameSelection;
  const candidate = selectedGameCandidate();
  if (!selection || !candidate) return;
  setBusy(true);
  try {
    state.games = unwrap(await window.manager.confirmGame({
      root: selection.root,
      executable: candidate.path,
      name: $('gameNameInput').value.trim() || selection.name || candidate.name.replace(/\.exe$/i, ''),
      icon: selection.icon || null
    }));
    closeGameSelection();
    renderGames();
    scheduleArtworkEnrichment();
    toast('游戏已添加');
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}

document.querySelectorAll('.nav').forEach(button => button.onclick = () => switchView(button.dataset.view));
$('searchInput').oninput = renderGames;
$('gameFilter').onchange = renderGames;
$('refreshBtn').onclick = () => runAction(async () => { state.games = unwrap(await window.manager.refresh()); return true; }, '扫描完成', false).then(() => { renderGames(); scheduleArtworkEnrichment(); });
async function pickAndRefresh(picker, success) {
  if (state.busy) return;
  setBusy(true);
  try {
    const games = unwrap(await picker());
    if (!games) return;
    state.games = games;
    renderGames();
    scheduleArtworkEnrichment();
    toast(success);
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
}
$('addGameBtn').onclick = $('emptyAddBtn').onclick = () => openGameSelection(window.manager.pickGame);
$('addExeBtn').onclick = () => openGameSelection(window.manager.pickExecutable);
$('gameNameInput').oninput = () => { if (state.gameSelection) state.gameSelection.nameTouched = true; };
$('gamePickerCancel').onclick = closeGameSelection;
$('confirmGameBtn').onclick = confirmGameSelection;
$('chooseGameIconBtn').onclick = async () => {
  if (!state.gameSelection) return;
  try {
    const picked = unwrap(await window.manager.pickGameIcon());
    if (picked && picked.icon) { state.gameSelection.icon = picked.icon; renderGameIconPreview(); }
  } catch (error) { toast(error.message, true); }
};
$('importAddonBtn').onclick = async () => {
  if (state.busy) return;
  setBusy(true);
  try {
    const addons = unwrap(await window.manager.pickAddon());
    if (addons) {
      state.addons = addons;
      renderAddonVersions();
      if (state.activeView === 'games') renderGames();
      toast('Addon / 标准 OTA 已导入');
    }
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
};
$('addonDropZone').ondragover = event => { event.preventDefault(); $('addonDropZone').classList.add('dragging'); };
$('addonDropZone').ondragleave = () => $('addonDropZone').classList.remove('dragging');
$('addonDropZone').ondrop = async event => {
  event.preventDefault();
  $('addonDropZone').classList.remove('dragging');
  const file = event.dataTransfer.files && event.dataTransfer.files[0];
  const filePath = file && window.manager.pathForFile(file);
  if (!filePath) return;
  try {
    state.addons = unwrap(await window.manager.importAddon(filePath));
    renderAddonVersions();
    if (state.activeView === 'games') renderGames();
    toast(/\.zip$/i.test(filePath) ? '标准 OTA 已导入' : 'Addon 已导入');
  } catch (error) { toast(error.message, true); }
};
$('addFolderBtn').onclick = () => pickAndRefresh(window.manager.pickScanFolder, '游戏库已添加');
$('scanDrivesToggle').onchange = event => runAction(async () => {
  state.settings = unwrap(await window.manager.updateSettings({ scanDrives: event.target.checked }));
  state.games = unwrap(await window.manager.refresh());
  return true;
}, '扫描范围已更新', false).then(() => { renderGames(); scheduleArtworkEnrichment(); });
$('addonVersionSelect').onchange = event => runAction(async () => {
  state.settings = unwrap(await window.manager.updateSettings({ addonVersion: event.target.value || null }));
  refreshSelectedPayload(event.target.value);
  renderVersionSelector();
  return true;
}, 'Addon 版本已切换；请点游戏卡片中的“修复”应用', false).then(renderGames);
$('bilibiliBtn').onclick = () => window.manager.openExternal('bilibiliUrl');
$('qqBtn').onclick = async () => { await window.manager.copyText(state.product.qqGroup); toast('群号已复制'); };
$('updateBtn').onclick = () => window.manager.openExternal('releaseUrl');
$('repairGameSelect').onchange = event => { state.repairGame = event.target.value; loadRepairDiagnostic(); };
$('repairBtn').onclick = () => state.repairGame && runConfirmedAction(allowAntiCheat => window.manager.repair(state.repairGame, { allowAntiCheat }), '修复完成', true, state.repairGame).then(loadRepairDiagnostic);
$('uninstallBtn').onclick = () => state.repairGame && confirmUninstall(state.repairGame);
$('copyDiagBtn').onclick = async () => { const text = diagnosticText(); if (text) { await window.manager.copyText(text); toast('诊断信息已复制'); } };
$('feedbackGameSelect').onchange = event => {
  state.feedbackGame = event.target.value;
  state.lastFailureGame = state.feedbackGame;
};
$('copyFeedbackBtn').onclick = async () => {
  const id = $('feedbackGameSelect').value;
  if (!id || state.busy) return;
  setBusy(true);
  try {
    const report = unwrap(await window.manager.buildFeedback(id, { includePaths: $('feedbackIncludePaths').checked }));
    await window.manager.copyText(report.text);
    toast('完整反馈日志已复制，可以直接发给开发者');
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
};
$('exportFeedbackBtn').onclick = async () => {
  const id = $('feedbackGameSelect').value;
  if (!id || state.busy) return;
  setBusy(true);
  try {
    const file = unwrap(await window.manager.exportFeedback(id, { includePaths: $('feedbackIncludePaths').checked }));
    if (file) toast(`反馈日志已保存：${file}`);
  } catch (error) { toast(error.message, true); }
  finally { setBusy(false); }
};
$('modalCancel').onclick = closeModal;
document.addEventListener('keydown', event => {
  const pending = state.pendingModal;
  if (!pending || pending.type !== 'hotkey') return;
  const binding = keyBindingFromEvent(event);
  if (!binding) return;
  event.preventDefault();
  event.stopPropagation();
  pending.binding = binding;
  $('modalBody').textContent = `已捕获：${hotkeyLabel(binding)}。点击“保存快捷键”写入当前游戏的 ReShade.ini。`;
  $('modalConfirm').disabled = false;
}, true);
$('modalConfirm').onclick = async () => {
  const pending = state.pendingModal;
  if (!pending) return;
  if (pending.type === 'anti-cheat') {
    closeModal(true);
    return;
  }
  if (pending.type === 'hotkey') {
    if (!pending.binding) return;
    closeModal();
    await runAction(() => window.manager.writeHotkey(pending.id, pending.target, pending.binding), '快捷键已保存；下次启动游戏生效', false);
    await loadExpanded(pending.id);
    return;
  }
  closeModal();
  if (pending.type === 'uninstall') {
    await runAction(() => window.manager.uninstall(pending.id, $('removeSettingsCheck').checked), '卸载完成');
    await loadRepairDiagnostic();
  }
  if (pending.type === 'remove-addon') {
    await runAction(async () => {
      state.addons = unwrap(await window.manager.removeAddon(pending.id));
      renderAddonVersions();
      return true;
    }, '导入的 Addon 已删除', false);
  }
  if (pending.type === 'dismiss-game') {
    await dismissGameFromList(pending.id);
  }
  if (pending.type === 'd3d12') {
    await runConfirmedAction(allowAntiCheat => window.manager.toggleD3D12(pending.id, pending.enabled, { allowAntiCheat }), pending.enabled ? '已切换到 D3D12 入口' : '已恢复 DXGI 入口', true, pending.id);
  }
};
$('minBtn').onclick = () => window.manager.minimize();
$('closeBtn').onclick = () => window.manager.close();
window.manager.onAddonImported(async () => {
  try {
    state.addons = unwrap(await window.manager.listAddons());
    renderAddonVersions();
    if (state.activeView === 'games') renderGames();
    toast('检测到新的 Addon，已加入版本库');
  } catch (error) { toast(error.message, true); }
});
boot();
