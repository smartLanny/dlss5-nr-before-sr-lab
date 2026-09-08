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
  repairLoadToken: 0,
  detailTabs: new Map(),
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
  const selectedVersion = [catalogReplacement(version), version, state.payload.selectedVersion, ...Object.keys(versions)]
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
    message = payload?.source?.error?.message || `安装组件缺失或校验未通过${problems.length ? `：${problems.slice(0, 3).join('、')}` : ''}。请到“插件版本”检查组件来源，或选择完整组件目录；仍失败时在“修复”页保存反馈。`;
  } else if (removedSelection) {
    const item = payload.versions[payload.selectedVersion];
    message = `上次选择的版本已退出常规分发，本安装包提供 ${coreVersionLabel(payload.selectedVersion, item)}。已安装核心不会因此自动更换；可在游戏设置中选择并应用新版本。`;
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
  select.innerHTML = Object.entries(versions).map(([id, item]) => `<option value="${escapeHtml(id)}">${escapeHtml(coreVersionLabel(id, item))}</option>`).join('');
  select.value = selected;
  $('addonVersionNote').textContent = '用于新安装；已安装游戏保留自己的版本，在游戏卡片中选择并应用。';
  select.disabled = false;
  renderPayloadNotice(removedSelection);
}

function coreVersionLabel(id, item) {
  const raw = String(item && item.label || id);
  const version = raw.match(/^(0\.\d+\.\d+(?:\.\d+)?(?:-(?:hotfix|beta)\.\d+)?)/);
  if (version) {
    const value = version[1];
    if (value.startsWith('0.2.')) return value.replace(/-beta\.\d+$/i, '');
    if (value.startsWith('0.3.')) return `${value} · 稳定基线`;
    if (value.startsWith('0.4.')) return `${value} · Beta`;
    return value;
  }
  return raw.replace(/(?:\s*[-–—]\s*|\s*[（(])(?:DXGI|DX11|DX12|DirectX\s*11|DirectX\s*12)\b.*$/i, '').trim() || String(id);
}

function catalogReplacement(id) {
  const map = state.payload?.bundle?.supersededVersions || state.payload?.supersededVersions;
  if (!id || !map || typeof map !== 'object') return null;
  const seen = new Set(); let current = id;
  while (Object.prototype.hasOwnProperty.call(map, current)) {
    if (seen.has(current) || seen.size >= 16 || typeof map[current] !== 'string') return null;
    seen.add(current); current = map[current];
  }
  return current !== id && Object.prototype.hasOwnProperty.call(state.payload?.versions || {}, current) ? current : null;
}

function actionSuccessMessage(success, value) {
  const replacement = value?.payloadReplacement;
  return replacement?.from && replacement?.to && replacement.from !== replacement.to
    ? `${success}；已应用替代核心 ${coreVersionLabel(replacement.to)}（原选择 ${coreVersionLabel(replacement.from)} 已退出常规分发）`
    : success;
}

function versionOptionsMarkup(selected = state.payload && state.payload.selectedVersion, includeImported = true) {
  const versions = state.payload && state.payload.versions ? Object.entries(state.payload.versions).map(([id, item]) => [id, item]) : [];
  const imported = includeImported ? (state.addons || []).filter(item => item.addonOnly).map(item => [item.id, item]) : [];
  const all = [...versions, ...imported];
  if (!all.length) return '<option value="">当前组件</option>';
  if (!all.some(([id]) => id === selected)) selected = catalogReplacement(selected) || (state.payload && state.payload.selectedVersion);
  return all.map(([id, item]) => `<option value="${escapeHtml(id)}"${id === selected ? ' selected' : ''}>${escapeHtml(coreVersionLabel(id, item))}${item.addonOnly ? '（导入）' : ''}</option>`).join('');
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
  if (game.supported) return `<button class="button subtle payload-open-btn">检查安装组件</button>${dismiss}`;
  if (game.supportCode === 'ERR_API_SELECTION_REQUIRED') return `<button class="button subtle">请选择游戏 API</button>${dismiss}`;
  return `<button class="button" disabled>无法安装</button>${dismiss}`;
}

const API_LABELS = { dx9: 'DirectX 9', dx10: 'DirectX 10', dx11: 'DirectX 11', dx12: 'DirectX 12',
  vulkan: 'Vulkan', opengl: 'OpenGL', mixed: '多条 API 线索，待确认', unknown: 'API 待确认', unsupported: '不支持的 API' };

function detectedApiResolution(chosen) {
  if (chosen?.detectedApiResolution) return chosen.detectedApiResolution;
  if (chosen?.detectedApi) return { api: chosen.detectedApi };
  // A manual override is not an automatic detection result.
  return chosen?.apiResolution?.source !== 'override' ? chosen?.apiResolution || { api: 'unknown' } : { api: 'unknown' };
}

function apiControls(game) {
  if (!game.chosen) return '';
  const resolution = game.chosen.apiResolution || {};
  const detected = detectedApiResolution(game.chosen);
  const selected = game.apiOverride || 'auto';
  const detectedLabel = detected.api === 'dx11' ? 'DX11 桥接' : detected.api === 'dx12' ? 'DX12' : API_LABELS[detected.api] || 'API 待确认';
  const options = [['auto', `${detectedLabel}（自动）`],
    ['dx9', 'DirectX 9（暂不支持 NR）'], ['dx10', 'DirectX 10（暂不支持 NR）'],
    ['dx11', 'DX11 桥接'], ['dx12', 'DX12（无需桥接）'],
    ['vulkan', 'Vulkan（暂不支持 NR）'], ['opengl', 'OpenGL（暂不支持 NR）']]
    .map(([id, label]) => `<option value="${id}"${id === selected ? ' selected' : ''}>${escapeHtml(label)}</option>`).join('');
  const deployment = resolution.api === 'dx11' ? '兼容桥接随 DirectX 11 自动部署。'
    : resolution.api === 'dx12' ? 'DirectX 12 无需兼容桥接。'
      : ['dx9', 'dx10', 'vulkan', 'opengl', 'unsupported'].includes(resolution.api) ? '当前 API 暂不支持 NR，不部署兼容桥接。' : 'API 待确认，暂不部署兼容桥接。';
  const evidence = (detected.evidence || []).join('；');
  return `<div class="config-block api-config-block"><h4>游戏图形 API</h4><div class="control-row"><select class="game-api-select" aria-label="游戏图形 API">${options}</select><button class="button subtle api-save-btn">保存 API</button></div><p class="api-route-status">${escapeHtml(deployment)}</p><details class="api-help"><summary>检测与桥接说明</summary><p>自动项展示检测结果；可手动改为实际使用的 API，桥接随选择自动处理。选择绑定当前 EXE 保存，重新扫描不会覆盖；不会替你修改游戏启动参数。已有安装在保存 API 时同步，未安装游戏在安装时应用。</p>${evidence ? `<p>检测线索：${escapeHtml(evidence)}</p>` : ''}</details></div>`;
}

function addonVersionRow(game, applyVersion = false) {
  const selected = game.installed ? (game.addonVersion || game.recommendedAddonVersion || (state.payload && state.payload.selectedVersion))
    : (game.recommendedAddonVersion || (state.payload && state.payload.selectedVersion));
  const options = versionOptionsMarkup(selected, Boolean(game.installed));
  const apply = applyVersion ? '<button class="button subtle version-apply-btn">应用版本</button>' : '';
  const installed = game.installed && game.addonVersion;
  const replacement = installed ? catalogReplacement(installed) : null;
  const installedNote = installed ? `<p class="config-note installed-version-note">已安装：${escapeHtml(coreVersionLabel(installed))}${replacement ? `；可更新为 ${escapeHtml(coreVersionLabel(replacement))}。点击“应用版本”或“修复”后才会更换核心。` : '。下拉框用于选择要应用的版本。'}</p>` : '';
  return `<div class="control-row addon-version-row"><label>${installed ? '应用核心版本' : '核心版本'}</label><select class="game-version-select" aria-label="核心版本">${options}</select>${apply || '<span></span>'}</div>${installedNote}`;
}

function gameApiLabel(chosen) {
  const resolution = chosen && chosen.apiResolution;
  if (!resolution) return chosen && chosen.apiLabel || 'API 未确认';
  const label = resolution.api === 'dx11' ? 'DX11 桥接' : resolution.api === 'dx12' ? 'DX12' : API_LABELS[resolution.api] || 'API 待确认';
  return `${label}${resolution.source === 'override' ? '（已指定）' : ['dx11', 'dx12'].includes(resolution.api) ? '（自动）' : ''}`;
}

function detailKey(game) {
  return `${game.id}:${String(game.chosen?.path || game.dir || '').toLowerCase()}`;
}

function gameDetail(game) {
  const key = detailKey(game);
  const active = state.detailTabs.get(key) === 'graphics' ? 'graphics' : 'enhance';
  const title = game.supported ? '安装条件已确认'
    : game.supportCode === 'ERR_API_SELECTION_REQUIRED' ? '请先确认游戏图形 API' : '安装前需要处理';
  const installDetail = game.supported
    ? (payloadReadyForHardware() ? `已匹配 ${hardwareLabel()} 组件，可使用上方“一键安装”。` : '请到“组件管理”选择完整安装组件，再返回安装。')
    : game.supportText || '请先选择游戏实际运行的 EXE。';
  const installationStatus = game.installed ? `<div class="config-block component-block"><div class="config-title"><div><h4>安装状态</h4><p class="section-caption">文件与安装记录</p></div><span class="component-summary" role="status">检查中</span></div><div class="component-body">正在检查…</div><div class="diagnostic-actions"><button class="button primary repair-inline-btn">一键修复</button><button class="button subtle feedback-inline-btn">保存反馈</button><button class="button danger uninstall-inline-btn">卸载插件</button></div><details class="advanced-settings route-advanced"><summary>高级加载入口</summary><div class="compat-actions"><button class="button subtle d3d12-btn">${game.d3d12Route ? '恢复 DXGI 入口' : '使用 D3D12 入口'}</button><span>仅在 DX12 游戏存在加载入口冲突时调整；保留原始文件和恢复记录。</span></div></details></div>` : '';
  const nr = game.installed ? `<div class="enhancement-layout">
    <div class="config-block enhancement-controls">
      <div class="config-title"><div><h4>画面设置</h4><p class="section-caption">调整后自动保存到这个游戏</p></div><span class="save-state" role="status">正在读取…</span></div>
      <div class="control-row"><label>风格</label><div class="segmented style-segment"><button data-value="0">默认</button><button data-value="1">自然</button><button data-value="2">电影</button></div><span></span></div>
      <div class="control-row"><label title="模型内部强度，默认 1.00，范围 0–2。">模型强度</label><div class="range-wrap"><input class="nr-model-strength" aria-label="NR 模型强度" type="range" min="0" max="2" step="0.01"><span class="value-pill nr-model-value">1.00</span></div><span></span></div>
      <div class="control-row"><label title="对应核心 NR 效果倍率，默认 1.00，范围 0–4。">额外强度</label><div class="range-wrap"><input class="nr-effect-strength" aria-label="NR 额外强度" type="range" min="0" max="4" step="0.01"><span class="value-pill nr-effect-value">1.00</span></div><span></span></div>
      <p class="config-note">模型强度影响内部处理；额外强度控制最终混合。默认均为 1.00。</p>
      <div class="skin-settings">
        <div class="control-row"><label title="模型内部的皮肤结构控制，不是独立人脸遮罩。">皮肤结构保护</label><span class="skin-summary">读取中</span><input class="switch nr-mask" aria-label="皮肤结构保护" type="checkbox"></div>
        <div class="control-row skin-strength-row"><label>保护强度</label><div class="range-wrap"><input class="nr-skin-strength" aria-label="皮肤结构强度" type="range" min="-1" max="2" step="0.01"><span class="value-pill nr-skin-value">关闭</span></div><span></span></div>
        <p class="config-note">对应模型内部控制，不等于独立人脸遮罩。NR 工作分辨率与色调跟随在游戏内 ReShade 面板调整。</p>
      </div>
      <div class="config-actions"><button class="button subtle default-btn">恢复 NR 默认</button></div>
    </div>
    <aside class="enhancement-sidebar">${installationStatus}<div class="config-block enhancement-guide"><h4>快捷操作</h4><p class="config-note">用 F6 对比增强前后的画面，实际效果以游戏内表现为准。</p>
      <div class="shortcut-block"><div class="shortcut-row"><label>NR 效果切换</label><button class="button subtle nr-hotkey-btn" disabled>F6 开关</button></div><div class="shortcut-row"><label>ReShade 菜单</label><button class="button subtle reshade-hotkey-btn">Home</button></div><p class="config-note">修改菜单快捷键后，下次启动游戏生效。</p></div>
      <button class="button subtle launch-jump-btn" type="button">调整超分补帧</button>
    </div></aside>
  </div>` : `<div class="config-block detail-empty"><h4>${title}</h4><p class="config-note">${escapeHtml(installDetail)}</p><p class="config-note">安装完成后，可在这里调整风格和增强强度。</p></div>`;
  const graphics = game.chosen ? '<section class="launch-settings-host" aria-label="超分补帧启动设置"><p class="config-note">正在读取超分补帧设置…</p></section>'
    : '<div class="config-block detail-empty"><h4>请先确认游戏 EXE</h4><p class="config-note">选择实际运行程序后，才能读取此游戏的超分补帧设置。</p></div>';
  const route = `<div class="route-layout game-route-controls">${apiControls(game)}<div class="config-block core-selection"><h4>核心版本</h4>${addonVersionRow(game, Boolean(game.installed))}${!game.installed ? '<p class="config-note">安装时使用这个核心；0.4 系列仍为 Beta，0.3 为稳定基线。</p>' : ''}</div></div>`;
  const tabs = [['enhance', '画面设置'], ['graphics', '超分补帧']];
  return `<div class="game-detail" data-game-detail="${escapeHtml(game.id)}" data-detail-key="${escapeHtml(key)}">
    ${route}
    <div class="detail-tabs" role="tablist" aria-label="游戏设置分类">${tabs.map(([id, label]) => `<button id="detail-${encodeURIComponent(game.id)}-${id}-tab" type="button" data-detail-tab="${id}" aria-controls="detail-${encodeURIComponent(game.id)}-${id}-panel" aria-selected="${id === active}" tabindex="${id === active ? 0 : -1}">${label}</button>`).join('')}</div>
    ${[['enhance', nr], ['graphics', graphics]].map(([id, content]) => `<section id="detail-${encodeURIComponent(game.id)}-${id}-panel" data-detail-panel="${id}" aria-labelledby="detail-${encodeURIComponent(game.id)}-${id}-tab"${id !== active ? ' hidden' : ''}>${content}</section>`).join('')}
    <div class="detail-footer"><span class="path-line" title="${escapeHtml(game.chosen?.path || game.dir)}">${escapeHtml(game.chosen?.path || game.dir)}</span><button class="button subtle open-folder-btn">打开目录</button></div>
  </div>`;
}


function renderGames(options = {}) {
  const previousExpanded = options?.preserveExpanded === true
    ? [...document.querySelectorAll('.game-card')].find(card => card.dataset.id === state.expanded) : null;
  const rows = visibleGames();
  updateGameListMeta(rows);
  $('gameList').innerHTML = rows.map(game => `<article class="game-card${state.expanded === game.id ? ' expanded' : ''}" data-id="${game.id}">
    <div class="game-card-head">
      ${poster(game)}
      <div class="game-meta"><div class="game-title"><h3>${escapeHtml(game.name)}</h3>${supportBadge(game)}</div><p>${escapeHtml(game.launcher)} · ${escapeHtml(game.chosen ? `${gameApiLabel(game.chosen)} / ${game.chosen.bitness} 位` : game.supportText)}</p></div>
      <div class="card-action">${cardAction(game)}<button class="expand-arrow" type="button" aria-label="展开或收起游戏详情" aria-expanded="${state.expanded === game.id}">⌄</button></div>
    </div>
    ${state.expanded === game.id ? gameDetail(game) : ''}
  </article>`).join('');
  let reusedExpanded = false;
  if (previousExpanded) {
    const replacement = [...document.querySelectorAll('.game-card')].find(card => card.dataset.id === previousExpanded.dataset.id);
    if (replacement) {
      replacement.replaceWith(previousExpanded); reusedExpanded = true;
      const select = previousExpanded.querySelector('.game-version-select');
      if (select) select.innerHTML = versionOptionsMarkup(select.value, Boolean(state.games.find(game => game.id === previousExpanded.dataset.id)?.installed));
    }
  }
  bindGameCards();
  if (state.expanded && !reusedExpanded) loadExpanded(state.expanded);
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
  document.querySelectorAll('.launch-jump-btn').forEach(button => {
    button.onclick = event => {
      event.stopPropagation();
      const host = button.closest('.game-card')?.querySelector('.launch-settings-host');
      if (!host) return;
      button.closest('.game-detail')?.detailTabController?.select('graphics');
      const view = host.closest('.view');
      if (view) view.scrollTop += host.getBoundingClientRect().top - view.getBoundingClientRect().top - 12;
      host.querySelector('select, button')?.focus({ preventScroll: true });
    };
  });
  document.querySelectorAll('.game-card').forEach(card => {
    const id = card.dataset.id;
    const detail = card.querySelector('.game-detail');
    if (detail && !detail.detailTabController && window.GameDetailTabs) {
      const mountGraphics = () => {
        const host = detail.querySelector('.launch-settings-host');
        if (host && window.launchSettingsUi && !host.launchSettingsController) window.launchSettingsUi.mount(host, id);
      };
      detail.detailTabController = window.GameDetailTabs.mount(detail, {
        initial: state.detailTabs.get(detail.dataset.detailKey) === 'graphics' ? 'graphics' : 'enhance',
        onSelect: tab => { state.detailTabs.set(detail.dataset.detailKey, tab); if (tab === 'graphics') mountGraphics(); }
      });
      if (detail.detailTabController.active() === 'graphics') mountGraphics();
    }
    card.querySelector('.game-card-head').onclick = event => {
      if (event.target.closest('.install-btn') || event.target.closest('.launch-btn') || event.target.closest('.dismiss-game-btn')) return;
      state.expanded = state.expanded === id ? null : id;
      renderGames();
    };
    const install = card.querySelector('.install-btn');
    if (install) install.onclick = event => { event.stopPropagation(); const version = card.querySelector('.game-version-select')?.value; runConfirmedAction(allowAntiCheat => window.manager.install(id, { allowAntiCheat, version }), '安装完成', true, id); };
    const sourceButton = card.querySelector('.payload-open-btn');
    if (sourceButton) sourceButton.onclick = event => { event.stopPropagation(); switchView('addons'); $('choosePayloadSourceBtn').focus(); };
    const saveApi = card.querySelector('.api-save-btn');
    if (saveApi) saveApi.onclick = event => {
      event.stopPropagation();
      const api = card.querySelector('.game-api-select').value;
      const success = 'API 选择已保存；兼容桥接已按当前路线处理';
      runConfirmedAction(allowAntiCheat => window.manager.setGameApi(id, api, { allowAntiCheat }), success, true, id);
    };
    const launch = card.querySelector('.launch-btn');
    if (launch) launch.onclick = event => { event.stopPropagation(); runAction(() => window.manager.launch(id), '游戏已启动', false, id); };
    const dismiss = card.querySelector('.dismiss-game-btn');
    if (dismiss) dismiss.onclick = event => { event.stopPropagation(); confirmDismissGame(id); };
    const openFolder = card.querySelector('.open-folder-btn');
    if (openFolder) openFolder.onclick = () => window.manager.openFolder(id);
    const repair = card.querySelector('.repair-inline-btn');
    if (repair) repair.onclick = () => runConfirmedAction(allowAntiCheat => window.manager.repair(id, { allowAntiCheat }), '修复完成', true, id);
    const feedback = card.querySelector('.feedback-inline-btn');
    if (feedback) feedback.onclick = () => { state.repairGame = id; state.feedbackGame = id; switchView('repair'); };
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
      }, `已应用核心 ${coreVersionLabel(version, imported)}`, true, id);
    };
  });
}

async function loadExpanded(id) {
  const loadToken = (state.expandedLoadToken || 0) + 1;
  state.expandedLoadToken = loadToken;
  const root = document.querySelector(`[data-game-detail="${id}"]`);
  if (!root || !root.querySelector('.nr-mask')) return;
  try {
    const [settings, diagnostic, hotkeys] = await Promise.all([
      window.manager.readNr(id).then(unwrap),
      window.manager.diagnose(id).then(unwrap),
      window.manager.readHotkeys(id).then(unwrap)
    ]);
    if (state.expandedLoadToken !== loadToken || state.expanded !== id) return;
    const versionSelect = root.querySelector('.game-version-select');
    const game = state.games.find(row => row.id === id);
    if (versionSelect) {
      const requested = (game && game.addonVersion) || (state.payload && state.payload.selectedVersion);
      if ([...versionSelect.options].some(option => option.value === requested)) versionSelect.value = requested;
    }
    root.querySelector('.nr-mask').checked = Boolean(settings.AutoMask);
    root.querySelector('.skin-summary').textContent = settings.AutoMask ? '已开启' : '已关闭';
    root.querySelector('.save-state').textContent = '已读取设置';
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', Number(b.dataset.value) === Number(settings.Style)));
    const intensity = Number(settings.Intensity ?? 1);
    const model = root.querySelector('.nr-model-strength');
    const modelValue = root.querySelector('.nr-model-value');
    if (model) model.value = String(intensity);
    if (modelValue) modelValue.textContent = intensity.toFixed(2);
    const effect = root.querySelector('.nr-effect-strength');
    const effectValue = root.querySelector('.nr-effect-value');
    const effectSupported = settings.capabilities?.TransferStrength === true && settings.capabilities?.PostTransferStrength === true;
    if (effect) {
      effect.value = String(settings.TransferStrength ?? 1);
      effect.disabled = !effectSupported;
      effect.title = effectSupported ? 'NR 效果倍率；调整时同步最终混合强度。' : '当前核心配置未提供额外强度参数。';
    }
    if (effectValue) effectValue.textContent = effectSupported ? Number(settings.TransferStrength ?? 1).toFixed(2) : '未提供';
    const skin = root.querySelector('.nr-skin-strength');
    const skinValue = root.querySelector('.nr-skin-value');
    if (skin) skin.value = String(settings.SkinStructureStrength ?? -1);
    if (skinValue) skinValue.textContent = Number(settings.SkinStructureStrength ?? -1) < 0 ? '关闭' : Number(settings.SkinStructureStrength).toFixed(2);
    if (skin) skin.disabled = !settings.AutoMask;
    root.querySelector('.reshade-hotkey-btn').textContent = hotkeyLabel(hotkeys && hotkeys.reshade);
    root.querySelector('.nr-hotkey-btn').textContent = 'F6 开关';
    root.querySelector('.component-body').innerHTML = diagnosticRows(diagnostic.components);
    const problems = diagnostic.components.filter(row => !row.ok).length;
    root.querySelector('.component-summary').textContent = problems ? `${problems} 项需要处理` : '文件检查通过';
    bindNrControls(root, id);
    bindHotkeyControls(root, id, hotkeys);
  } catch (error) {
    const indicator = root.querySelector('.save-state');
    if (indicator) { indicator.textContent = '读取失败'; indicator.classList.add('error'); }
    toast(error.message, true);
  }
}

function queueSetting(id, patch, root) {
  const indicator = root.querySelector('.save-state');
  if (indicator) { indicator.textContent = '正在保存…'; indicator.classList.remove('error'); }
  state.pendingPatches.set(id, { ...(state.pendingPatches.get(id) || {}), ...patch });
  clearTimeout(state.saveTimers.get(id));
  state.saveTimers.set(id, setTimeout(async () => {
    const pending = state.pendingPatches.get(id) || {};
    state.pendingPatches.delete(id);
    try {
      unwrap(await window.manager.writeNr(id, pending));
      if (indicator) indicator.textContent = '已保存';
    } catch (error) { if (indicator) { indicator.textContent = '保存失败'; indicator.classList.add('error'); } toast(error.message, true); }
  }, 180));
}

function bindNrControls(root, id) {
  root.querySelector('.nr-mask').onchange = e => {
    const enabled = e.target.checked;
    root.querySelector('.skin-summary').textContent = enabled ? '已开启' : '已关闭';
    const skin = root.querySelector('.nr-skin-strength');
    if (skin) skin.disabled = !enabled;
    queueSetting(id, { AutoMask: enabled ? 1 : 0 }, root);
  };
  root.querySelectorAll('.style-segment button').forEach(button => button.onclick = () => {
    root.querySelectorAll('.style-segment button').forEach(b => b.classList.toggle('active', b === button));
    queueSetting(id, { Style: Number(button.dataset.value) }, root);
  });
  const model = root.querySelector('.nr-model-strength');
  const modelValue = root.querySelector('.nr-model-value');
  if (model) model.oninput = () => {
    if (modelValue) modelValue.textContent = Number(model.value).toFixed(2);
    queueSetting(id, { Intensity: Number(model.value) }, root);
  };
  const effect = root.querySelector('.nr-effect-strength');
  const effectValue = root.querySelector('.nr-effect-value');
  if (effect) effect.oninput = () => {
    if (effect.disabled) return;
    const value = Number(effect.value);
    if (effectValue) effectValue.textContent = value.toFixed(2);
    queueSetting(id, { TransferStrength: value, PostTransferStrength: value }, root);
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
    toast(actionSuccessMessage(success, outcome.value));
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
  const response = await (window.manager.listGames ? window.manager.listGames() : window.manager.refresh());
  state.games = mergeGameVisuals(state.games, unwrap(response));
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
  renderPayloadSource();
  $('addonListHint').textContent = rows.length ? `${rows.length} 个版本 · 在游戏设置中应用` : '还没有可用版本';
  root.innerHTML = rows.length ? rows.map(item => `<div class="addon-row">
    <div class="addon-symbol"><span class="nav-icon nav-icon-addons" aria-hidden="true"></span></div>
    <div><div class="addon-name">${escapeHtml(coreVersionLabel(item.id, item))}</div><div class="addon-meta">${item.addonOnly ? `导入文件：${escapeHtml(item.sourceName || '')} · ${item.ota ? '配套更新包，按 API 应用所需组件' : '仅更新核心，保留配套组件'}` : escapeHtml(item.notes || '完整安装组件')}</div></div>
    <span class="addon-tag">${item.ready ? '可用' : '组件未准备'}</span>
    ${item.deletable ? `<button class="button danger addon-remove-btn" data-id="${escapeHtml(item.id)}">移出版本库</button>` : `<span class="addon-tag">${item.source === 'external' ? '外部目录' : '随程序提供'}</span>`}
  </div>`).join('') : '<p class="muted">首次安装请先选择完整组件目录；已有安装可导入标准 OTA 或核心更新文件。</p>';
  root.querySelectorAll('.addon-remove-btn').forEach(button => button.onclick = () => confirmRemoveAddon(button.dataset.id));
}

function renderPayloadSource() {
  const source = state.payload?.source;
  const status = $('payloadSourceStatus');
  if (!status) return;
  const ready = source ? source.ready && !source.error : Boolean(state.payload?.ready);
  status.textContent = ready ? '组件检查通过' : '需要处理';
  status.className = `badge ${ready ? 'good' : 'warn'}`;
  $('payloadSourceLabel').textContent = source?.mode === 'external' ? '外部组件目录' : source?.mode === 'unconfigured' ? '尚未选择组件' : '随程序提供';
  $('payloadSourcePath').textContent = source?.mode === 'unconfigured' ? '选择完整组件目录后会记住位置。' : source?.path || state.payload?.dir || '未提供完整组件目录';
  $('payloadSourceDetail').textContent = source?.mode === 'unconfigured' ? '本程序未附带完整 NR 组件；选择已有的完整组件目录即可继续。'
    : source?.error?.message || (ready ? '已核对清单和文件；安装或修复前还会再次校验。' : '请确认完整组件、文件校验和显卡匹配。');
  $('payloadSourceDetail').classList.toggle('error', Boolean(source?.error) && source?.mode !== 'unconfigured');
  $('resetPayloadSourceBtn').classList.toggle('hidden', source?.mode !== 'external' || source?.bundledAvailable === false);
}

async function changePayloadSource(action) {
  if (state.busy) return;
  const message = $('payloadSourceFeedback');
  const buttons = ['choosePayloadSourceBtn', 'recheckPayloadSourceBtn', 'resetPayloadSourceBtn'].map($);
  setBusy(true); buttons.forEach(button => { button.disabled = true; });
  message.textContent = '正在核对组件目录和文件，请稍候…'; message.className = 'payload-source-feedback';
  const update = data => {
    state.settings = data.settings; state.payload = data.payload; state.addons = data.addons;
    renderVersionSelector(); renderAddonVersions(); renderGames({ preserveExpanded: true });
  };
  try {
    const result = unwrap(await window.manager[action]());
    if (result) { update(result); message.textContent = '组件来源已保存；已安装游戏可在游戏设置中选择并应用版本。'; }
    else message.textContent = '已取消选择，继续使用当前组件来源。';
  } catch (error) {
    try { update(unwrap(await window.manager.readPayloadSource())); } catch {}
    message.textContent = error.message; message.className = 'payload-source-feedback error';
    message.classList.toggle('hidden', state.payload?.source?.error?.message === error.message);
  } finally { setBusy(false); buttons.forEach(button => { button.disabled = false; }); }
}

function confirmRemoveAddon(id) {
  const item = (state.addons || []).find(row => row.id === id);
  state.pendingModal = { type: 'remove-addon', id };
  $('modalTitle').textContent = '删除导入的 Addon';
  $('modalBody').textContent = `将从管理器版本库移除“${coreVersionLabel(id, item)}”。不会删除已经安装到游戏目录的文件。`;
  $('removeSettingsLine').classList.add('hidden');
  $('modal').classList.remove('hidden');
}

function diagnosticRows(rows) {
  const markup = row => `<div class="diag-row"><span class="diag-icon ${row.ok ? 'ok' : 'bad'}">${row.ok ? '●' : '×'}</span><span>${escapeHtml(row.label)}</span><span class="diag-detail">${escapeHtml(row.detail || (row.ok ? '完整' : '异常'))}</span></div>`;
  const failed = rows.filter(row => !row.ok), passed = rows.filter(row => row.ok);
  return `${failed.map(markup).join('')}${passed.length ? `<details class="healthy-components"><summary>${failed.length ? '其余 ' : ''}${passed.length} 项文件检查通过</summary>${passed.map(markup).join('')}</details>` : ''}`;
}

async function loadRepairDiagnostic() {
  const id = state.repairGame;
  const token = ++state.repairLoadToken;
  state.diagnostics = null;
  const game = state.games.find(row => row.id === id);
  $('repairBtn').disabled = !game;
  $('uninstallBtn').disabled = !game?.installed;
  $('copyDiagBtn').disabled = true;
  if (!id) {
    $('diagnostics').innerHTML = '<p>选择需要检查的游戏；尚未安装也可以保存反馈。</p>';
    state.diagnostics = null;
    return;
  }
  $('diagnostics').innerHTML = '<p>正在检查组件…</p>';
  try {
    const diagnostic = unwrap(await window.manager.diagnose(id));
    if (token !== state.repairLoadToken || state.repairGame !== id) return;
    state.diagnostics = diagnostic;
    const problems = diagnostic.components.filter(row => !row.ok).length;
    $('diagnostics').innerHTML = `<div class="diagnostic-heading"><div><h3>${problems ? `${problems} 项需要处理` : '文件检查通过'}</h3><p>${problems ? '先处理以下异常，已通过的项目收在下方。' : '已核对当前安装文件；游戏实际效果仍需在游戏内确认。'}</p></div><span class="badge ${problems ? 'warn' : 'good'}">${escapeHtml(game?.name || '')}</span></div>${diagnosticRows(diagnostic.components)}`;
    $('copyDiagBtn').disabled = false;
  } catch (error) { if (token === state.repairLoadToken) $('diagnostics').innerHTML = `<p class="launch-error">${escapeHtml(error.message)}</p>`; }
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
    addons: ['组件管理', '准备安装来源，按游戏应用与回退核心']
  };
  $('pageTitle').textContent = titles[view][0];
  $('pageSubtitle').textContent = titles[view][1];
  $('addGameBtn').classList.toggle('hidden', view !== 'games');
  $('addExeBtn').classList.toggle('hidden', view !== 'games');
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
$('searchInput').oninput = () => {
  clearTimeout(renderGames.searchTimer);
  renderGames.searchTimer = setTimeout(() => renderGames({ preserveExpanded: true }), 120);
};
$('gameFilter').onchange = () => renderGames({ preserveExpanded: true });
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
$('choosePayloadSourceBtn').onclick = () => changePayloadSource('pickPayloadSource');
$('recheckPayloadSourceBtn').onclick = () => changePayloadSource('recheckPayloadSource');
$('resetPayloadSourceBtn').onclick = () => changePayloadSource('resetPayloadSource');
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
}, '新安装默认版本已保存；已安装游戏保持当前版本', false).then(() => renderGames({ preserveExpanded: true }));
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
if (window.manager.onLaunchSettingsApplied) window.manager.onLaunchSettingsApplied(async payload => {
  if (payload?.id !== state.expanded) return;
  const card = [...document.querySelectorAll('.game-card')].find(row => row.dataset.id === payload.id);
  try { await card?.querySelector('.launch-settings-host')?.launchSettingsController?.refresh(); }
  catch (error) { toast(`启动设置状态读取失败：${error.message}`, true); }
  const failures = (payload.outcomes || []).filter(row => row.code || row.reason);
  if (failures.length) toast(failures.map(row => `${String(row.domain || '').toUpperCase()}：${row.reason || row.code}`).join('；'), true);
});
boot();
