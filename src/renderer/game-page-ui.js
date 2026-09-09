'use strict';

(function (scope) {
  const esc = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]);
  const API = { auto: '自动检测', dx11: 'DirectX 11', dx12: 'DirectX 12', vulkan: 'Vulkan', dx9: 'DirectX 9', dx10: 'DirectX 10', opengl: 'OpenGL', mixed: '多种能力，待确认', unknown: '尚未确认' };
  const STATUS = { deployed: '已部署', passed: '已确认', 'not-observed': '未观察到变化', unverified: '待确认', bypassed: '已旁路', failed: '失败', 'not-applicable': '不适用',
    configurable: '可配置', unavailable: '暂不可用', reusable: '受管版本一致', 'version-conflict': '版本冲突', 'duplicate-load': '重复加载风险', 'other-mod': '其他组件 · 保留', unknown: '来源待确认', 'waiting-confirmation': '等待确认游戏开关', 'waiting-game-setting': '请先在游戏中开启',
    'waiting-first-run': '请先运行一次游戏', enabled: '已启用', disabled: '被配置禁用', missing: '文件缺失', 'version-mismatch': '版本不符', 'path-mismatch': '路径不符' };
  const LAUNCH = { preflight: '正在检查启动条件', 'waiting-helper': '等待加载助手就绪', 'request-sending': '正在发送启动请求', 'waiting-launcher': '等待启动器产生所选游戏进程',
    'waiting-game': '等待所选游戏进程', 'game-matched': '已匹配游戏进程', 'waiting-enhancement': '游戏已启动，增强待确认', 'enhancement-failed': '游戏仍保留，加载失败', failed: '本次启动失败', cancelled: '已取消等待' };
  const NR_CHOICES = { WorkMode: ['参考 · Reference', '质量 · Quality', '平衡 · Balanced', '性能 · Performance', '极致性能 · Ultra', '自定义 · Custom'], Style: ['默认', '自然', '电影'] };
  const NR = [ ['Intensity', '增强强度', 0, 2, .05], ['WorkMode', 'NR 工作模式', 0, 5, 1], ['CustomWorkScale', '自定义工作比例', .25, 1, .05],
    ['Style', '画面风格', 0, 2, 1], ['ColorStrength', '颜色强度', 0, 1, .05], ['SkinStructureStrength', '皮肤结构', -1, 2, .05],
    ['TransferStrength', '前置传递强度', 0, 4, .05], ['PostTransferStrength', '后置传递强度', 0, 4, .05] ];
  const unwrap = result => { if (result?.ok !== true) throw Object.assign(new Error(result?.error?.message || '操作未完成。'), result?.error); return result.value; };
  const option = (value, label, selected, disabled = false) => `<option value="${esc(value)}"${String(value) === String(selected) ? ' selected' : ''}${disabled ? ' disabled' : ''}>${esc(label)}</option>`;
  const badge = (status, fallback) => `<span class="badge ${['passed', 'enabled', 'configurable'].includes(status) ? 'good' : ['failed', 'bypassed', 'version-mismatch'].includes(status) ? 'bad' : ''}">${esc(STATUS[status] || fallback || status || '待确认')}</span>`;
  const line = (label, value) => `<div class="gp-fact"><span>${esc(label)}</span><strong>${esc(value ?? '待确认')}</strong></div>`;
  const sourceText = value => typeof value === 'string' ? value : value?.api || value?.value || null;
  const apiLabel = value => API[sourceText(value)] || sourceText(value) || '尚未确认';
  const hasFgComponents = assessment => ['installed', 'managed', 'receipt', 'needsRecovery', 'needsCleanup', 'fileRecoveryPending', 'fileOperationActive', 'migrationPending'].some(key => assessment?.enhancements?.fgComponents?.[key]);
  const TAB_SECTION = { overview: 'installation', enhance: 'enhancements', maintenance: 'diagnostics' };
  const DETAIL_TAB = { overview: 'enhance', enhance: 'graphics', maintenance: 'advanced' };
  const COMPARISON_VERSION = '0.4.7beta-bg3-bridge1411';
  function recommendedModel(quality) { return quality === 'performance' ? 'M' : quality === 'ultraPerformance' ? 'L' : ['quality', 'balanced', 'dlaa', 'custom'].includes(quality) ? 'K' : null; }
  function bindingLabel(binding) { if (!binding) return '未设置'; const key = binding.key === 36 ? 'Home' : binding.key === 45 ? 'Insert' : binding.key >= 112 && binding.key <= 135 ? `F${binding.key - 111}` : `VK ${binding.key}`; return [binding.ctrl && 'Ctrl', binding.shift && 'Shift', binding.alt && 'Alt', key].filter(Boolean).join(' + '); }
  function initialFields(assessment) {
    const settings = assessment.enhancements || {}, hardware = settings.hardware || assessment.hardware || {};
    const helper = scope.launchSettingsUi || (typeof require === 'function' ? require('./launch-settings-ui') : null);
    const backend = helper.hardwareFacts(hardware).fgBackend || '';
    return { sr: helper.initialSrFields(settings, hardware),
      fg: { backend: hasFgComponents(assessment) && settings.fgComponents?.backend || backend, mode: 'restore', multiplier: 2, targetFps: 0, ...(settings.requests?.fg?.request || settings.applied?.fg?.request || {}) } };
  }
  function mount(host, manager, options = {}) {
    let id = null, data = null, draft = {}, fields = {}, invalidFields = {}, tab = 'overview', busy = false, launching = false, message = '', error = false,
      modal = null, generation = 0, session = null, unsubscribe = null, disposed = false, tabController = null;
    const loaded = new Set(), sectionRequests = new Map(), sectionTokens = new Map(), sectionFailures = new Map();
    const draftCount = () => new Set([...Object.keys(draft), ...Object.keys(invalidFields)]).size;
    const dirty = () => draftCount() > 0;
    const act = (name, label, disabled = false, kind = '') => `<button type="button" class="button ${kind}" data-gp-action="${name}"${disabled ? ' disabled' : ''}>${label}</button>`;
    const selected = (name, baseline) => draft[name] ?? baseline;
    const currentHardware = () => data.enhancements?.hardware || data.hardware || {};
    const detectedApi = () => data.api?.detectedApi || data.game.chosen?.detectedApiResolution?.api || data.game.chosen?.detectedApi ||
      (data.game.chosen?.apiResolution?.source !== 'override' ? data.game.chosen?.apiResolution?.api : null) ||
      (data.game.apiOverride && data.game.apiOverride !== 'auto' ? 'unknown' : data.api?.effectiveApi);
    const effectiveApi = () => selected('api', data.game.apiOverride || data.defaults?.api || 'auto') === 'auto'
      ? detectedApi() : selected('api', data.game.apiOverride || data.defaults?.api);
    const apiReady = () => ['dx11', 'dx12', 'vulkan'].includes(effectiveApi());
    const isComparison = row => row.comparisonOnly === true || row.id === COMPARISON_VERSION;
    function versionRows() {
      const special = specialRoute(), info = special ? data.game[special] || {} : null;
      return special ? [{ id: info.packageId || '', label: `${special === 'vulkan' ? 'Vulkan' : 'DX12 Feeder'} · ${info.coreVersion || '固定配套'}`, ready: info.selectionAvailable ?? info.available }] : data.coreVersions || [];
    }
    function currentVersion() {
      const special = specialRoute();
      if (special) return data.game[special]?.packageId || '';
      if (Object.hasOwn(data.defaults || {}, 'version')) return selected('version', data.defaults.version) || '';
      return selected('version', data.deployment?.version || data.game.addonVersion || '') || '';
    }
    function deploymentMode() { return selected('deployment', data.game.installed ? data.layout?.mode || 'local' : data.defaults?.deployment || 'external'); }
    function hasVersionUpdate() {
      const installed = data.deployment?.version || data.game.addonVersion;
      return !specialRoute() && data.game.installed && installed && currentVersion() && installed !== currentVersion();
    }
    function installRequest() {
      const route = specialRoute();
      return { ...draft, api: selected('api', data.game.apiOverride || data.defaults?.api || 'auto'), version: currentVersion(), ...(route ? { route } : {
        deployment: deploymentMode(), ...(deploymentMode() === 'external' ? { loadingMode: selected('loadingMode', data.layout?.loadingMode || data.defaults?.loadingMode || 'proxy') } : {}) }) };
    }
    function apiBlocked(request = draft) {
      if (Object.keys(request).length && Object.keys(request).every(key => key === 'launchMode')) return false;
      if (request.uninstall || request.sr?.quality === 'game' || request.fg?.mode === 'restore') {
        const other = Object.keys(request).filter(key => !['uninstall', 'sr', 'fg'].includes(key));
        if (!other.length && (!request.sr || request.sr.quality === 'game') && (!request.fg || request.fg.mode === 'restore')) return false;
      }
      return !apiReady();
    }
    function specialRoute() {
      const api = effectiveApi();
      if (data.layout?.source === 'vulkan' || api === 'vulkan') return 'vulkan';
      if (data.layout?.source === 'feeder' || api === 'dx12' && data.game.nativeDlssAvailable === false && data.game.feeder) return 'feeder';
      return null;
    }
    function feature(domain) { return data?.enhancements?.featureStates?.[domain] || { eligible: false, blockers: [{ message: '尚未确认此功能的支持条件。' }] }; }
    function head() {
      return `<div class="detail-tabs gp-tabs" role="tablist" aria-label="游戏设置">${[['overview', '安装与画面'], ['enhance', '超分与补帧'], ['maintenance', '高级与维护']].map(([key, label]) => `<button class="detail-tab" type="button" role="tab" data-detail-tab="${DETAIL_TAB[key]}" data-gp-tab="${key}" aria-selected="${tab === key}" tabindex="${tab === key ? 0 : -1}">${label}</button>`).join('')}</div>`;
    }
    function diagnosticsContent() {
      const api = data.api || {}, v = data.verification || {}, modules = data.helperModules?.modules || [], deployment = data.deployment || {};
      return `<section class="gp-section" role="tabpanel" aria-label="概览与兼容性"><div class="gp-section-title"><h3>本次运行验收</h3>${act('refresh', '重新检查', busy, 'subtle')}</div>
        <div class="gp-verification">${[['helper', '加载助手就绪'], ['core', '本 Core 已加载'], ['nr', 'NR 成功并持续提交'], ['visual', '同场景画面变化']].map(([key, label], index) => `<article><div class="gp-step">0${index + 1}</div><h4>${label}</h4>${badge(v[key]?.status || 'unverified')}<p>${esc(v[key]?.detail || '等待本次证据。')}</p></article>`).join('')}</div>
        <p class="gp-caption">配置保存、助手就绪和 HDR 成功分别记录；NR 处理和画面变化需要各自的证据。</p>
        ${manager.recordVisualComparison ? act('record-visual', '记录本次画面对照', busy || !(session || data.launch?.session)?.process || (session || data.launch?.session)?.historical) : ''}</section>
        <section class="gp-section"><h3>API 对照</h3><div class="gp-facts">${line('游戏能力', (api.capabilities || []).map(apiLabel).join(' / ') || '证据不足')}${line('当前配置', apiLabel(api.configuredApi))}
        ${line('当前采用', apiLabel(api.effectiveApi))}${line('用户选择', apiLabel(data.game.apiOverride || 'auto'))}${line('本次观察', apiLabel(api.observedApi))}${line('实际呈现', apiLabel(api.presentationApi))}
        ${line('检测可信度', { confirmed: '本次已确认', high: '高', medium: '中', low: '低', none: '证据不足' }[api.confidence])}${line('桥接条件', api.bridgeStatus?.required ? `${api.bridgeStatus.kind || '需要桥接'} · ${api.bridgeStatus.verified ? '已验证' : '实际运行待验证'}` : api.bridgeStatus?.required === false ? '无需桥接' : '待确认')}</div>
        ${api.effectiveApi === 'mixed' && !api.conflicts?.length ? '<p class="gp-caption">可用 API 不止一种，请按游戏当前设置确认。</p>' : ''}
        ${(api.conflicts || []).map(row => `<p class="gp-message error">${esc(row.message || row.reason || JSON.stringify(row))}</p>`).join('')}
        <details class="gp-evidence"><summary>查看检测依据与覆盖范围</summary><ul>${(api.evidence || []).map(row => `<li><strong>${apiLabel(row.api)}</strong> · ${esc(row.message || row.detail || row.kind || '')}<small>${esc(row.source || '')} ${esc(row.path || '')}</small></li>`).join('') || '<li>尚无足够的检测依据。</li>'}</ul><pre>${esc(JSON.stringify(api.coverage || {}, null, 2))}</pre></details></section>
        <section class="gp-section"><h3>当前部署</h3><div class="gp-facts">${line('运行方式', deployment.mode === 'external' ? `外置目录 · ${deployment.loadingMode === 'helper' ? '加载助手' : '代理加载'}` : '游戏目录')}
        ${line('Core 版本', deployment.version || data.game.addonVersion || '尚未安装')}${line('活动配置', data.layout?.activeConfigPath)}${line('运行目录', data.layout?.runtimeDir)}</div></section>
        ${modules.length ? `<section class="gp-section"><h3>插件允许清单</h3><p class="gp-caption">按部署记录的名称、位数和摘要核验；被禁用的插件需要在配置中启用后重新应用。</p><div class="gp-module-list">${modules.map(row => `<div><strong>${esc(row.name)}</strong>${badge(row.status)}<small>${esc(row.path)}</small><small>${esc(row.architecture || '位数待确认')} · ${esc(row.sha256?.slice(0, 16) || '无摘要')}…</small></div>`).join('')}</div></section>` : ''}
        ${data.components ? `<section class="gp-section"><h3>组件来源与冲突</h3><p class="gp-caption">本 Core、Generic NR、HDR 和补帧组件分别识别。静态文件检查不代表已被本次进程加载。</p>
          <div class="gp-module-list">${(data.components.files || []).map(row => `<div><strong>${esc(row.name)}</strong>${badge(row.status)}<small>${esc(row.label)} · ${{ verified: '固定摘要已核对', declared: '组件自声明', hint: '仅文件名线索', unknown: '证据不足' }[row.confidence] || '待确认'}</small><small>${esc(row.path)}</small><small>${esc(({ enabled: '配置允许加载', disabled: '配置已禁用', candidate: '代理候选', dependency: '依赖文件', 'inactive-or-unverified': '加载位置待确认' })[row.loadState] || '')} · ${esc(row.architecture || '')}</small></div>`).join('') || '<p class="gp-caption">当前范围未发现需核对的组件。</p>'}</div>
          ${[...(data.components.conflicts || []), ...(data.components.warnings || [])].map(row => `<p class="gp-message">${esc(row.detail)}</p>`).join('')}</section>` : ''}
        ${data.antiCheat?.detected ? `<section class="gp-section gp-risk"><h3>游戏保护与账号风险</h3><p>${esc(data.antiCheat.message)}</p><div class="gp-actions">${act('anti-cheat', '官方说明')}${act('compatibility-search', '搜索此游戏兼容资料')}${act('maintenance-tab', '维护与恢复')}</div></section>` : ''}
        ${(data.failures || []).length ? `<details class="gp-section"><summary>部分检查暂不可用</summary>${data.failures.map(row => `<p class="gp-caption">${esc(row.section)}：${esc(row.message)}</p>`).join('')}</details>` : ''}`;
    }
    function selectField(group, key, label, markup, disabled = false, note = '') {
      return `<label class="gp-field"><span>${label}</span><select data-gp-group="${group}" data-gp-field="${key}"${disabled ? ' disabled' : ''}>${markup}</select>${note ? `<small>${note}</small>` : ''}</label>`;
    }
    function nrFields(advanced = false) {
      const nr = { ...(data.nr || {}), ...(draft.nr || {}) }, available = Boolean(data.game.installed && data.nr);
      return `<section class="gp-section"><div class="gp-section-title"><h3>${advanced ? 'NR 详细参数' : 'NR 画面增强'}</h3>${advanced ? '' : `<label class="check-line gp-check"><input type="checkbox" data-gp-group="nr" data-gp-field="Enabled"${nr.Enabled ? ' checked' : ''}${available ? '' : ' disabled'}>开启</label>`}</div>
        ${!available ? '<p class="gp-caption">安装后可调整画面增强。</p>' : ''}
        <div class="gp-controls">${NR.filter(([key]) => advanced ? !['Intensity', 'Style'].includes(key) && (key !== 'CustomWorkScale' || Number(nr.WorkMode) === 5) : ['Intensity', 'Style'].includes(key)).map(([key, label, min, max, step]) => {
          const capable = available && (!Object.hasOwn(data.nr?.capabilities || {}, key) || data.nr.capabilities[key] === true);
          if (NR_CHOICES[key]) return selectField('nr', key, label, NR_CHOICES[key].map((label, value) => option(value, label, nr[key] ?? 0)).join(''), !capable);
          return `<label class="gp-field"><span>${label}</span><div class="gp-range"><input type="range" min="${min}" max="${max}" step="${step}" value="${esc(nr[key] ?? min)}" data-gp-group="nr" data-gp-field="${key}"${capable ? '' : ' disabled'}><output>${esc(key === 'SkinStructureStrength' && Number(nr[key]) === -1 ? '关闭' : nr[key] ?? min)}</output></div>${capable ? key === 'CustomWorkScale' ? '<small>工作模式选为“自定义”后生效。</small>' : '' : '<small>当前 Core 或配置未提供此项。</small>'}</label>`;
        }).join('')}</div>${advanced ? `<label class="check-line gp-check"><input type="checkbox" data-gp-group="nr" data-gp-field="AutoMask"${nr.AutoMask ? ' checked' : ''}${available ? '' : ' disabled'}>自动遮罩</label>` : ''}</section>`;
    }
    function featurePanel(domain) {
      const info = feature(domain), f = fields[domain], owned = data.enhancements?.applied?.[domain], active = info.eligible === true, allowRestore = Boolean(owned) || domain === 'fg' && hasFgComponents(data), sr = domain === 'sr';
      const reasons = (info.blockers || []).map(row => row.message || row);
      let controls;
      if (sr) {
        const model = scope.launchSettingsUi.recommendedPreset(currentHardware()), labels = scope.launchSettingsUi.SR_MODEL_LABELS;
        controls = selectField('sr', 'preset', '超分模型', option('', '保持原有模型', f.preset) + ['M', 'K', 'L'].map(value => option(value, `${labels[value]}${model === value ? ' · 本机推荐' : ''}`, f.preset)).join('') + option('auto', 'NVIDIA 按档位推荐', f.preset), !active || f.quality === 'game', f.preset === 'L' ? '主要优化 4K 超级性能档位。' : f.preset === 'auto' ? `按所选档位推荐${recommendedModel(f.quality) ? ` ${recommendedModel(f.quality)}` : '，当前档位待确认'}。` : '') +
          selectField('sr', 'quality', 'DLSS 档位', Object.entries({ preserve: '保持游戏档位，仅修改模型', game: '恢复游戏控制', dlaa: 'DLAA', quality: '质量', balanced: '平衡', performance: '性能', ultraPerformance: '超级性能', custom: '自定义比例' }).map(([key, label]) => option(key, label, f.quality, !active && key !== 'game')).join(''), !active && !allowRestore) +
          (f.quality === 'custom' ? `<label class="gp-field"><span>输入比例</span><input type="number" min="33" max="100" step="1" value="${esc(f.renderPercent)}" data-gp-group="sr" data-gp-field="renderPercent"${active ? '' : ' disabled'}><small>33–100%。</small></label>` : '');
      } else {
        const modes = info.availableModes || [], multipliers = info.availableMultipliers || [];
        controls = selectField('fg', 'mode', '补帧模式', Object.entries({ restore: '使用原有设置', follow: '跟随游戏倍率', off: '驱动关闭 FG', fixed: f.backend === 'mfgunlock' ? '请求提高倍率' : '固定总倍率', dynamic: '动态目标帧率' }).map(([key, label]) => option(key, label, f.mode, key !== 'restore' && (!active || !modes.includes(key)))).join(''), !active && !allowRestore) +
          (f.mode === 'fixed' ? selectField('fg', 'multiplier', '总帧率倍率', [2, 3, 4, 5, 6].map(value => option(value, `${value}×${multipliers.includes(value) ? '' : ' · 待确认支持'}`, f.multiplier, !multipliers.includes(value))).join(''), !active, f.backend === 'mfgunlock' ? '提高较低的游戏请求；不会降低已有的更高倍率。' : '') : '') +
          (f.mode === 'dynamic' ? `<label class="gp-field"><span>动态目标帧率</span><input type="number" min="0" max="1000" step="1" value="${esc(f.targetFps)}" data-gp-group="fg" data-gp-field="targetFps"${active && modes.includes('dynamic') ? '' : ' disabled'}><small>0 表示自动目标。</small></label>` : '');
      }
      return `<section class="gp-section"><div class="gp-section-title"><div><h3>${sr ? 'DLSS 超分' : f.backend === 'mfgunlock' ? 'RTX40 补帧' : f.backend === 'nvidia' ? 'RTX50 帧生成' : '帧生成'}</h3></div>${badge(info.state || (active ? 'configurable' : 'unavailable'))}</div>
        ${reasons.length ? `<p class="gp-caption" title="${esc(reasons.join('；'))}">${esc(reasons[0])}</p>` : ''}${info.state === 'waiting-confirmation' && info.canConfirm ? `<div class="gp-confirm-feature"><span>请先在游戏内开启${sr ? ' DLSS 超分' : '帧生成'}。</span>${act(`confirm-${domain}`, '我已开启', busy)}</div>` : info.evidence?.confirmation || info.evidence?.gameSetting?.state === 'on' ? '<p class="gp-confirmed" role="status">✓ 已确认开启</p>' : ''}
        <div class="gp-controls">${controls}</div><p class="gp-caption">${owned ? owned.readbackVerified ? '已应用，重启游戏后生效。' : '设置已变化，请重新预览。' : '尚未应用覆盖设置。'}</p>
        <div class="gp-small-actions">${sr ? act('recommend-sr', '恢复推荐', busy || !active || !scope.launchSettingsUi.recommendedPreset(currentHardware()), 'subtle') : ''}${allowRestore ? act(`restore-${domain}`, '恢复原设置', busy, 'subtle') : ''}${owned?.requiresReapply ? act('reapply', '重新预览', busy) : ''}</div></section>`;
    }
    function installation() {
      const game = data.game, api = selected('api', game.apiOverride || data.defaults?.api || 'auto'), effective = effectiveApi(), special = specialRoute();
      const version = currentVersion(), visibleVersion = version === COMPARISON_VERSION ? '0.4.7beta' : version;
      const versions = versionRows().filter(row => !isComparison(row));
      const pending = data.operation?.pending || data.deployment?.needsRecovery;
      const unresolved = !effective || ['mixed', 'unknown', 'auto'].includes(effective);
      const automatic = detectedApi(), automaticLabel = !automatic || ['mixed', 'unknown', 'auto'].includes(automatic) ? '需要手动选择' : `${apiLabel(automatic)}（自动）`;
      let status = !loaded.has('installation') ? '正在检查安装条件…' : pending ? '有未完成操作，请先恢复。' : !apiReady()
        ? unresolved ? '需要手动选择游戏实际使用的 API。' : `${apiLabel(effective)} 当前不支持安装。`
        : game.installed ? data.deployment?.verified === false && data.deployment?.inspection !== 'summary' ? '安装需要检查，可预览修复。' : '已安装，可以调整画面或启动游戏。'
        : '基础条件已确认，安装前将预览本次变更。';
      if (data.antiCheat?.detected && !pending && apiReady()) status = '检测到游戏保护，安装前需确认兼容提示。';
      if (loaded.has('installation') && !pending && apiReady() && !version) status = '配套版本尚未确定，请选择可用 Core。';
      return `<section class="gp-section gp-install-section"><div class="gp-controls">
        ${selectField('route', 'api', '游戏 API', option('auto', automaticLabel, api) + ['dx11', 'dx12', 'vulkan'].map(key => option(key, API[key], api)).join(''), busy)}
        ${selectField('route', 'version', 'Core 配套', (!visibleVersion ? option('', '请选择 Core 配套', '', true) : versions.some(row => row.id === visibleVersion) ? '' : option(visibleVersion, `${visibleVersion} · 来源待检查`, visibleVersion, true)) + versions.map(row => option(row.id, row.label || row.id, visibleVersion, row.ready === false)).join(''), busy || Boolean(special))}</div>
        <div class="gp-compatibility ${pending || !apiReady() ? 'needs-attention' : ''}" role="status"><strong>兼容性</strong><span>${esc(status)}</span>${pending ? act('recover-operation', '恢复操作', busy, 'subtle') : ''}</div>
        ${hasVersionUpdate() ? `<p class="gp-caption">当前已安装 ${esc(data.deployment?.version || game.addonVersion)}；应用后才会更新所选配套。</p>` : !game.installed && !special ? `<p class="gp-caption">默认使用外置目录，加载方式可在“高级与维护”调整。</p>` : ''}</section>${nrFields()}`;
    }
    function enhancements() {
      const facts = scope.launchSettingsUi.hardwareFacts(currentHardware());
      return `<div class="gp-hardware"><strong>${esc(facts.label)}</strong>${loaded.has('enhancements') ? '' : '<span role="status">正在读取超分补帧条件…</span>'}</div><div class="launch-grid">${featurePanel('sr')}${featurePanel('fg')}</div>`;
    }
    function advanced() {
      const layout = data.layout || {}, special = specialRoute(), mode = deploymentMode(), version = currentVersion();
      return `<section class="gp-section"><h3>加载与启动</h3><div class="gp-controls">
        ${selectField('route', 'deployment', '组件目录', special ? option(mode, `${special === 'vulkan' ? 'Vulkan 外置' : 'Feeder 专用'}目录`, mode) : option('external', '每游戏外置目录', mode) + option('local', '游戏目录', mode), busy || Boolean(special))}
        ${selectField('route', 'loadingMode', '加载方式', option('proxy', '代理加载', selected('loadingMode', layout.loadingMode || 'proxy')) + option('helper', '外置加载助手', selected('loadingMode', layout.loadingMode || 'proxy'), !data.game.installed), busy || Boolean(special) || mode !== 'external', !data.game.installed && !special ? '首次安装使用代理；安装后可切换助手。' : '')}
        ${selectField('route', 'launchMode', '启动方式', option('auto', '自动 · Steam 优先', selected('launchMode', data.launch?.selected || 'auto')) + option('steam', '通过 Steam', selected('launchMode', data.launch?.selected || 'auto'), !data.launch?.steamAvailable) + option('exe', '直接启动 EXE', selected('launchMode', data.launch?.selected || 'auto')), busy)}
        ${effectiveApi() === 'dx11' && !special ? ['0.4.7beta', COMPARISON_VERSION].includes(version) ? selectField('bridge', 'version', 'DX11 桥接', option('0.4.7beta', '默认 1.4.12', version) + option(COMPARISON_VERSION, '旧桥 1.4.11 · BG3 排查', version), busy, '两项使用相同 Core，仅桥接器不同。') : '<p class="gp-caption">桥接器随当前 Core 配套；0.4.7beta 提供旧桥对照。</p>' : '<p class="gp-caption">当前路线无需 DX11 桥接器。</p>'}</div></section>
        ${nrFields(true)}<details class="gp-section"><summary>游戏内面板与快捷键</summary><p class="gp-caption">ReShade：${esc(bindingLabel(draft.hotkeys?.reshade || data.hotkeys?.reshade))} · NR：${esc(data.hotkeys?.nr?.label || '由 Core 提供')}</p>
        <div class="gp-small-actions">${act('panel-insert', '改用 Insert', busy || !data.game.installed)}${act('panel-ctrl-home', '改用 Ctrl + Home', busy || !data.game.installed)}${act('panel-home', '恢复 Home', busy || !data.game.installed)}</div></details>
        ${loaded.has('diagnostics') && loaded.has('enhancements') ? `<details class="gp-section gp-maintenance-details"><summary>卸载、恢复与环境清理</summary>${maintenance()}</details><details class="gp-section gp-diagnostics-details"><summary>运行验收与详细检测</summary>${diagnosticsContent()}</details>` : '<p class="gp-caption" role="status">正在读取恢复记录与详细检测…</p>'}`;
    }
    function maintenance() {
      const dep = data.deployment || {}, env = data.maintenance || {}, records = data.operation?.record;
      const hasSettings = Object.keys(data.enhancements?.applied || {}).length > 0 || hasFgComponents(data);
      return `<section class="gp-section" role="tabpanel" aria-label="维护与备份"><h3>恢复记录</h3><div class="gp-facts">${line('安装前基线', dep.baseline ? `${dep.baseline.version || '已记录'} · ${dep.baseline.api || ''}` : '由原安装记录保留')}
        ${line('上一完整部署', dep.previous ? `${dep.previous.version || ''} · ${dep.previous.mode || ''}` : '尚无外置切换记录')}${line('当前部署', `${dep.version || data.game.addonVersion || '未安装'} · ${dep.mode === 'external' ? '外置' : '游戏目录'}`)}
        ${line('清理归档', env.backupDirectory || (env.isolated ? '已备份隔离' : '尚无清理归档'))}</div>
        ${records ? `<div class="gp-message error"><strong>上次统一应用尚未完成</strong><p>${esc(records.error?.message || '请恢复未完成事务后继续。')}</p>${(records.stages || []).map(row => `<span class="badge">${esc(row.kind)} · ${esc(row.status)}</span>`).join('')}${act('recover-operation', '恢复未完成操作', busy, 'primary')}</div>` : ''}
        ${data.enhancements?.pending?.length ? act('recover-settings', '恢复超分补帧事务', busy) : ''}${dep.needsRecovery ? act('recover-operation', '恢复部署事务', busy) : ''}
        <div class="gp-actions">${act('back-local', '迁回游戏目录', busy || dep.mode !== 'external')}${act('open-folder', '打开游戏文件夹')}${act('feedback', '保存反馈与验收记录', busy)}</div></section>
        <section class="gp-section"><h3>卸载与原样恢复</h3><p class="gp-caption">每次选择本次的处理方式，预览文件后再应用。安装前备份和历史归档都会保留。</p><div class="gp-uninstall"><div><h4>干净移除</h4><p>移除摘要一致的受管文件，旧代理和旧插件继续留在备份中。</p>${act('uninstall-clean', '预览干净移除', busy || !data.game.installed, 'danger')}</div><div><h4>恢复安装前</h4><p>恢复有原始记录及摘要的文件。未知 .bak 不会自动当作原件。</p>${act('uninstall-restore', '预览恢复安装前', busy || !data.game.installed)}</div></div></section>
        <section class="gp-section"><h3>环境检查与清理</h3><p class="gp-caption">${esc(env.scope || '')}</p><div class="gp-module-list">${(env.remainingFiles || []).map(row => `<div><strong>${esc(row.name)}</strong><small>${esc(row.kind || row.classification || '需核对来源')}</small><small>${esc(row.sha256?.slice(0, 20) || '')}</small></div>`).join('') || '<p class="gp-caption">当前检查未发现额外代理或 Add-on。</p>'}</div>
        ${data.game.installed || hasSettings ? '<p class="gp-caption">请先明确移除配套并恢复超分补帧设置，再隔离剩余文件。</p>' : ''}<div class="gp-actions">${act('clean-environment', '预览剩余文件隔离', busy || data.game.installed || hasSettings || env.isolated)}${act('restore-environment', '撤销上次清理', busy || !env.canRestore)}</div></section>
        ${manager.removeGame ? `<section class="gp-section"><h3>游戏库条目</h3><p class="gp-caption">先完成卸载与设置恢复，再移出游戏库；之后可重新添加。</p>${act('remove-game', '移出游戏库', busy || dirty() || data.game.installed || hasSettings || data.operation?.pending || dep.needsRecovery || env.isolated, 'subtle')}</section>` : ''}`;
    }
    function footer() {
      const invalid = Object.keys(invalidFields).length > 0, pending = data.operation?.pending || data.deployment?.needsRecovery;
      let primary;
      if (dirty()) primary = act('preview', '预览并应用', busy || invalid || pending || apiBlocked(), 'primary');
      else if (tab === 'overview' && (!data.game.installed || hasVersionUpdate() || data.deployment?.verified === false && data.deployment?.inspection !== 'summary')) primary = act('prepare', !data.game.installed ? '安装并启用' : hasVersionUpdate() ? '预览更新配套' : '预览修复安装', busy || pending || !loaded.has('installation') || !apiReady() || !currentVersion() || versionRows().find(row => row.id === currentVersion())?.ready === false, 'primary');
      else if (tab === 'enhance') primary = act('preview-sr', '应用超分设置', busy || pending || !apiReady() || !loaded.has('enhancements') || (!feature('sr').eligible && !(fields.sr.quality === 'game' && data.enhancements?.applied?.sr)), 'primary');
      else primary = '';
      const launch = session || data.launch?.session;
      return `<div class="gp-apply-bar"><div><strong>${invalid ? '请先修正输入' : !apiReady() && apiBlocked() ? '请先在“安装与画面”确认 API' : dirty() ? `${draftCount()} 组修改待应用` : LAUNCH[launch?.status] || (data.game.installed ? '设置已就绪' : '确认 API 后安装')}</strong>${dirty() ? '<small>修改仅暂存，应用前会显示预览。</small>' : ''}</div><div>${dirty() ? act('discard', '放弃修改', busy, 'subtle') : act('refresh', '重新检查', busy, 'subtle')}${primary}${act('launch', launching ? '等待游戏…' : data.game.installed ? '启动游戏' : '原样启动', busy || launching || dirty() || pending, primary ? '' : 'primary')}${launching ? act('cancel-launch', '取消等待') : ''}${act('back', '收起', busy, 'subtle')}</div></div>`;
    }
    function render() {
      if (disposed) return;
      if (!data) { host.innerHTML = `<div class="gp-section">${act('back', '← 返回游戏库')}<p role="status">${esc(message || '正在读取游戏状态…')}</p>${error ? act('refresh', '重试') : ''}</div>`; return; }
      const focused = host.contains(document.activeElement) ? document.activeElement : null;
      const focusKey = focused?.dataset?.gpField, focusGroup = focused?.dataset?.gpGroup;
      const focusTab = focused?.dataset?.gpTab, focusAction = focused?.dataset?.gpAction;
      const top = host.closest('.view')?.scrollTop;
      tabController?.dispose();
      host.innerHTML = head() + `<div class="gp-message${error ? ' error' : ''}" role="${error ? 'alert' : 'status'}"${message ? '' : ' hidden'}>${esc(message)}</div>` + ['overview', 'enhance', 'maintenance'].map(key => `<div class="detail-panel" data-detail-panel="${DETAIL_TAB[key]}" role="tabpanel"${tab === key ? '' : ' hidden'}>${tab === key ? key === 'overview' ? installation() : key === 'enhance' ? enhancements() : advanced() : ''}</div>`).join('') + footer();
      if (scope.GameDetailTabs) tabController = scope.GameDetailTabs.mount(host, { initial: DETAIL_TAB[tab], onSelect: value => selectTab(Object.keys(DETAIL_TAB).find(key => DETAIL_TAB[key] === value)) });
      if (focusKey) host.querySelector(`[data-gp-group="${focusGroup}"][data-gp-field="${focusKey}"]`)?.focus({ preventScroll: true });
      else if (focusTab) host.querySelector(`[data-gp-tab="${focusTab}"]`)?.focus({ preventScroll: true });
      else if (focusAction) host.querySelector(`[data-gp-action="${focusAction}"]`)?.focus({ preventScroll: true });
      if (top !== undefined) host.closest('.view').scrollTop = top;
      host.setAttribute('aria-busy', String(busy)); if (modal) renderModal();
    }
    function renderModal() {
      host.querySelector('.gp-modal')?.remove();
      const plan = modal.plan;
      const modalTitle = ({ leave: '离开前处理修改', visual: '本次画面对照记录', cleanup: '备份隔离文件预览', remove: '移出游戏库' })[modal.kind] || '本次操作预览';
      const content = modal.kind === 'remove' ? '<p>将此游戏移出管理器列表。游戏文件与历史备份保留，之后可以重新添加。</p>' : modal.kind === 'leave' ? '<p>当前修改尚未应用。离开后可放弃这些修改。</p>' : modal.kind === 'visual' ?
        '<p>记录绑定本次游戏程序、会话和 Core 摘要。来源会标为用户观察，NR 成功状态仍独立核对。</p><label class="check-line gp-check"><input type="checkbox" data-gp-same-scene>我已完成同场景、相同设置下的增强开关对照</label><label class="gp-field"><span>观察结果</span><select data-gp-visual-result><option value="uncertain">暂时无法确认</option><option value="changed">观察到画面变化</option><option value="unchanged">没有观察到画面变化</option></select></label><label class="gp-field"><span>观察说明</span><textarea data-gp-visual-note maxlength="2000" rows="3" placeholder="例如同一存档位置的人脸、材质或光照变化"></textarea></label><label class="gp-field"><span>F8 / ColorDiag 或截图材料名称（可选）</span><input data-gp-visual-evidence maxlength="240"></label>' : modal.kind === 'cleanup'
        ? `<p>${esc(plan.scope)}</p>${plan.candidates.map(row => `<label class="gp-file-choice"><input type="checkbox" data-gp-clean="${esc(row.name)}"${row.selectedByDefault ? ' checked' : ''}${row.selectable ? '' : ' disabled'}><span><strong>${esc(row.name)}</strong><small>${esc(row.kind)} · ${esc(row.note)}</small><small>${esc(row.sha256)}</small></span></label>`).join('')}`
        : `<p>核对本次变更；操作过程中请保持游戏关闭。</p><div class="gp-change-list">${(plan.changes || []).map(row => `<div><strong>${esc(row.name || row.key || row.domain || row.action)}</strong><span>${esc(({ create: '新增', replace: '替换', remove: '移除', keep: '保留', 'set-config-key': '写入配置', 'set-launch-mode': '修改启动方式' })[row.action] || row.description || row.action)}${row.value !== undefined ? ` → ${esc(row.value)}` : ''}</span><small>${esc(row.path || '')}</small>${row.beforeSha256 !== undefined ? `<details><summary>核验摘要</summary><small>变更前 ${esc(row.beforeSha256 || '不存在')}<br>变更后 ${esc(row.afterSha256 || '移除')}</small></details>` : ''}</div>`).join('')}</div>
        ${(plan.blockers || []).map(row => `<p class="gp-message error">${esc(row.message || row)}</p>`).join('')}
        ${data.antiCheat?.detected || plan.deployment?.requiresAntiCheat ? '<label class="check-line gp-check"><input type="checkbox" data-gp-consent>我已了解反作弊可能阻止加载及账号风险，并决定应用。</label>' : ''}`;
      host.insertAdjacentHTML('beforeend', `<div class="gp-modal" role="dialog" aria-modal="true" aria-label="${modalTitle}"><div class="gp-modal-card"><h3>${modalTitle}</h3>${content}<div class="gp-message gp-modal-message" role="status"></div><div class="gp-modal-actions">${act('modal-cancel', modal.kind === 'leave' ? '继续编辑' : '取消', busy)}${modal.kind === 'leave' ? act('leave-confirm', '放弃修改并离开', busy, 'danger') : modal.kind === 'visual' ? act('save-visual', '保存用户观察', busy, 'primary') : modal.kind === 'remove' ? act('remove-confirm', '确认移出', busy, 'danger') : `${modal.kind === 'apply' && manager.applyOperationElevated ? act('apply-elevated', '以管理员权限应用本次操作', busy || plan.blockers?.length > 0) : ''}${act('modal-apply', modal.kind === 'cleanup' ? '备份并隔离所选文件' : '应用本次变更', busy || plan.blockers?.length > 0, 'primary')}`}</div></div></div>`);
      host.querySelector('.gp-modal button')?.focus();
    }
    function updateBar() {
      const bar = host.querySelector('.gp-apply-bar'); if (bar) bar.outerHTML = footer();
      const start = host.querySelector('[data-gp-action="launch"]'); if (start) start.disabled = busy || launching || dirty() || data.operation?.pending;
      const notice = host.querySelector(':scope > .gp-message');
      if (notice) { notice.textContent = message; notice.hidden = !message; notice.classList.toggle('error', error); notice.setAttribute('role', error ? 'alert' : 'status'); }
    }
    function mergeAssessment(value, section) {
      if (value.gameId && value.gameId !== id) throw new Error('检测结果与当前游戏不一致。');
      const currentExe = data.game?.chosen?.path?.toLowerCase(), nextExe = value.game?.chosen?.path?.toLowerCase();
      if (currentExe && nextExe && currentExe !== nextExe) throw new Error('游戏程序已改变，请收起后重新打开设置。');
      data = { ...data, ...value, ...(value.game ? { game: { ...data.game, ...value.game } } : {}) };
      sectionFailures.set(section, value.failures || []);
      data.failures = [...sectionFailures.values()].flat();
      if (value.launch) session = value.launch.session;
      const initial = initialFields(data);
      for (const domain of ['sr', 'fg']) if (!draft[domain] && !invalidFields[domain]) fields[domain] = initial[domain];
    }
    async function loadSection(section, force = false) {
      if (disposed || !id) return;
      if (!force && loaded.has(section)) return;
      if (!force && sectionRequests.has(section)) return sectionRequests.get(section);
      const currentGeneration = generation, token = (sectionTokens.get(section) || 0) + 1;
      sectionTokens.set(section, token);
      const task = Promise.resolve().then(() => manager.assessGame(id, { sections: [section] })).then(unwrap).then(value => {
        if (disposed || generation !== currentGeneration || sectionTokens.get(section) !== token) return;
        mergeAssessment(value, section);
        for (const name of value.sections || [section]) loaded.add(name);
        if (!modal) render();
      }).catch(failure => {
        if (!disposed && generation === currentGeneration && sectionTokens.get(section) === token) {
          message = failure.message; error = true; if (!modal) render();
        }
      }).finally(() => { if (sectionTokens.get(section) === token) sectionRequests.delete(section); });
      sectionRequests.set(section, task); return task;
    }
    async function refresh(preserve = true) {
      if (!id || disposed) return;
      // A write or an explicit recheck can change any section. Reopen unseen
      // sections lazily, and reject responses that started before this refresh.
      generation++; loaded.clear(); sectionRequests.clear();
      if (!preserve) { fields = initialFields(data); }
      const needed = new Set(['installation', TAB_SECTION[tab]]);
      if (tab === 'maintenance') needed.add('enhancements');
      await Promise.all([...needed].map(section => loadSection(section, true)));
    }
    function selectTab(next) {
      if (!Object.hasOwn(TAB_SECTION, next) || disposed) return;
      tab = next; render();
      host.querySelector(`[data-gp-tab="${tab}"]`)?.focus({ preventScroll: true });
      void loadSection(TAB_SECTION[tab]);
      if (tab === 'maintenance') void loadSection('enhancements');
    }
    async function work(fn, success, preserve = false) {
      if (busy) return; busy = true; message = ''; error = false; render();
      try { const result = unwrap(await fn()); message = result?.notice || success || '操作已完成。'; if (!preserve) { draft = {}; fields = {}; invalidFields = {}; } modal = null; await refresh(preserve); return result; }
      catch (failure) { message = failure.message; error = true; modal = null; try { await refresh(true); } catch {} }
      finally { busy = false; render(); }
    }
    async function preview(request = draft) {
      if (busy) return; busy = true; message = ''; error = false; render();
      try {
        if (apiBlocked(request)) throw new Error('请先选择游戏实际使用的 API。');
        if (request === draft) for (const domain of ['sr', 'fg']) if (draft[domain] || invalidFields[domain]) {
          // Validate the visible values again; an earlier valid draft must not
          // survive an invalid edit and silently become the request sent out.
          draft[domain] = scope.launchSettingsUi.createRequest(domain, fields[domain]); delete invalidFields[domain];
        }
        if (request === draft && ['api', 'version', 'route', 'deployment', 'loadingMode'].some(key => Object.hasOwn(draft, key))) request = installRequest();
        modal = { kind: 'apply', plan: unwrap(await manager.previewOperation(id, request)) };
      }
      catch (failure) { message = failure.message; error = true; }
      finally { busy = false; render(); }
    }
    async function apply(elevated = false) {
      if (!modal || busy) return;
      const value = modal, allowAntiCheat = host.querySelector('[data-gp-consent]')?.checked === true;
      if (value.kind === 'apply' && (data.antiCheat?.detected || value.plan?.deployment?.requiresAntiCheat) && !allowAntiCheat) { host.querySelector('.gp-modal-message').textContent = '请先确认已了解本次游戏的反作弊提示。'; return; }
      if (value.kind === 'cleanup') {
        const names = [...host.querySelectorAll('[data-gp-clean]:checked')].map(input => input.dataset.gpClean);
        if (!names.length) { host.querySelector('.gp-modal-message').textContent = '请先选择需要隔离的文件。'; return; }
        await work(() => manager.applyEnvironmentCleanup(id, value.plan.planId, names), '所选文件已备份隔离，可撤销。');
      } else await work(() => (elevated ? manager.applyOperationElevated : manager.applyOperation)(id, value.plan.planId,
        { confirm: true, fingerprint: value.plan.fingerprint, allowAntiCheat }), '配置已应用；实际增强待游戏验证。');
      options.onChanged?.(id);
    }
    function requestLeave(callback) { if (busy) return false; callback(); return true; }
    async function open(gameId, initialTab = 'overview', seed = {}) {
      generation++; id = gameId; loaded.clear(); sectionTokens.clear(); sectionRequests.clear(); sectionFailures.clear(); draft = {}; invalidFields = {};
      tab = initialTab; modal = null; message = ''; error = false;
      const game = seed.game || { id: gameId, name: '游戏', installed: false };
      data = { gameId, game, api: game.chosen?.apiAssessment || { effectiveApi: game.apiOverride && game.apiOverride !== 'auto' ? game.apiOverride : game.chosen?.apiResolution?.api || 'unknown' }, ...seed };
      fields = initialFields(data); render();
      await loadSection('installation');
      if (tab !== 'overview') { void loadSection(TAB_SECTION[tab]); if (tab === 'maintenance') void loadSection('enhancements'); }
    }
    function resume(game) {
      if (disposed || !data || game.id !== id) return;
      data.game = { ...data.game, ...game }; render();
      void loadSection('installation'); if (tab !== 'overview') void loadSection(TAB_SECTION[tab]);
    }
    async function launchGame() {
      if (busy || launching || dirty()) { message = '请先应用或放弃当前修改。'; error = true; render(); return; }
      launching = true; message = ''; error = false; render();
      try { const result = unwrap(await manager.launch(id)); session = result.launched || result; void loadSection('installation', true); }
      catch (failure) { message = failure.message; error = true; session = failure.details?.launchSession || session; }
      finally { launching = false; render(); }
    }
    function change(input) {
      const group = input.dataset.gpGroup, key = input.dataset.gpField; if (!key || input.disabled || busy) return;
      const value = input.type === 'checkbox' ? Number(input.checked) : input.type === 'number' && input.value === '' ? '' : group === 'nr' || input.type === 'range' || input.type === 'number' ? Number(input.value) : input.value;
      if (group === 'nr') {
        draft.nr = { ...(draft.nr || {}), [key]: value };
        if (value === data.nr?.[key]) delete draft.nr[key]; if (!Object.keys(draft.nr).length) delete draft.nr;
        const out = input.parentElement.querySelector('output'); if (out) out.textContent = key === 'SkinStructureStrength' && value === -1 ? '关闭' : value;
      } else if (group === 'bridge') {
        draft.version = value;
      } else if (group === 'route') {
        draft[key] = value;
        if (key === 'deployment' && value === 'local') delete draft.loadingMode;
        const route = specialRoute(); if (route) { draft.route = route; draft.version = data.game[route]?.packageId || data.game[route]?.coreVersion; delete draft.loadingMode; }
        else {
          delete draft.route;
          if (key === 'api' && effectiveApi() !== 'dx11' && currentVersion() === COMPARISON_VERSION) draft.version = '0.4.7beta';
        }
      } else if (group === 'sr' || group === 'fg') {
        fields[group][key] = value;
        if (group === 'sr' && key === 'quality') {
          const ratio = { dlaa: 100, quality: 67, balanced: 59, performance: 50, ultraPerformance: 33 }[value];
          if (ratio) fields.sr.renderPercent = ratio;
        }
        try { draft[group] = scope.launchSettingsUi.createRequest(group, fields[group]); delete invalidFields[group]; }
        catch (failure) { invalidFields[group] = failure.message; }
        message = Object.values(invalidFields).join('；'); error = Boolean(message);
      }
      if (input.tagName === 'SELECT') { const field = input.dataset.gpField; render(); host.querySelector(`[data-gp-group="${group}"][data-gp-field="${field}"]`)?.focus({ preventScroll: true }); }
      else updateBar();
    }
    host.addEventListener('input', event => { if (event.target.type === 'range' || event.target.type === 'number') change(event.target); });
    host.addEventListener('change', event => { if (!['range', 'number'].includes(event.target.type)) change(event.target); });
    host.addEventListener('click', async event => {
      const tabButton = event.target.closest('[data-gp-tab]'); if (tabButton) { if (!scope.GameDetailTabs) selectTab(tabButton.dataset.gpTab); return; }
      const button = event.target.closest('[data-gp-action]'); if (!button || button.disabled) return; const action = button.dataset.gpAction;
      try {
        if (action === 'back') requestLeave(() => options.onBack?.());
        else if (action === 'refresh') { message = ''; error = false; await refresh(true); if (!error) message = '已重新检查。'; render(); }
        else if (action === 'preview') await preview();
        else if (action === 'preview-sr') { draft.sr = scope.launchSettingsUi.createRequest('sr', fields.sr); delete invalidFields.sr; await preview(); }
        else if (action === 'recommend-sr') { fields.sr = scope.launchSettingsUi.initialSrFields({}, currentHardware()); draft.sr = scope.launchSettingsUi.createRequest('sr', fields.sr); delete invalidFields.sr; message = ''; error = false; render(); }
        else if (action === 'discard') { draft = {}; invalidFields = {}; fields = initialFields(data); message = '已放弃尚未应用的修改。'; error = false; render(); }
        else if (action === 'prepare') { Object.assign(draft, installRequest()); await preview(); }
        else if (action.startsWith('panel-')) { draft.hotkeys = { reshade: { key: action === 'panel-insert' ? 45 : 36, ctrl: action === 'panel-ctrl-home', shift: false, alt: false } }; render(); }
        else if (action === 'restore-sr' || action === 'restore-fg') { const domain = action.slice(-2); fields[domain][domain === 'sr' ? 'quality' : 'mode'] = domain === 'sr' ? 'game' : 'restore'; draft[domain] = scope.launchSettingsUi.createRequest(domain, fields[domain]); delete invalidFields[domain]; message = Object.values(invalidFields).join('；'); error = Boolean(message); render(); }
        else if (action === 'reapply') { draft.reapplyExternalChanges = true; for (const domain of ['sr', 'fg']) if (data.enhancements?.applied?.[domain]?.requiresReapply) draft[domain] = data.enhancements.applied[domain].request; await preview(); }
        else if (action === 'modal-cancel') { modal = null; host.querySelector('.gp-modal')?.remove(); }
        else if (action === 'leave-confirm') { const callback = modal.callback; draft = {}; invalidFields = {}; modal = null; callback?.(); }
        else if (action === 'modal-apply' || action === 'apply-elevated') await apply(action === 'apply-elevated');
        else if (action === 'record-visual') { modal = { kind: 'visual' }; renderModal(); }
        else if (action === 'remove-game') { modal = { kind: 'remove' }; renderModal(); }
        else if (action === 'remove-confirm') {
          busy = true; render();
          try { unwrap(await manager.removeGame(id)); draft = {}; modal = null; await options.onChanged?.(id); options.onBack?.(); }
          finally { busy = false; if (modal) render(); }
        }
        else if (action === 'save-visual') {
          if (!host.querySelector('[data-gp-same-scene]').checked) { host.querySelector('.gp-modal-message').textContent = '请先完成并确认同场景开关对照。'; return; }
          const input = { sessionId: (session || data.launch?.session).sessionId, sameScene: true, result: host.querySelector('[data-gp-visual-result]').value,
            note: host.querySelector('[data-gp-visual-note]').value, evidenceLabel: host.querySelector('[data-gp-visual-evidence]').value };
          await work(() => manager.recordVisualComparison(id, input), '已保存用户观察。', true);
        }
        else if (action === 'confirm-sr' || action === 'confirm-fg') {
          const domain = action.slice(-2); busy = true; message = ''; error = false; render();
          // A confirmation response already contains the fresh feature state.
          // Invalidate earlier section reads so they cannot revive the question.
          sectionTokens.set('enhancements', (sectionTokens.get('enhancements') || 0) + 1);
          sectionRequests.delete('enhancements');
          try {
            const result = unwrap(await manager.confirmGameFeature(id, domain, { enabled: true }));
            if (!result || result.domain !== domain || typeof result.eligible !== 'boolean') throw new Error('游戏开关确认结果无效，请重新检查。');
            data.enhancements = { ...(data.enhancements || {}), featureStates: { ...(data.enhancements?.featureStates || {}), [domain]: result } };
            message = '已确认开启。';
          } finally { busy = false; render(); }
        }
        else if (action === 'uninstall-clean' || action === 'uninstall-restore') await preview({ uninstall: action.slice('uninstall-'.length) });
        else if (action === 'back-local') { draft.deployment = 'local'; await preview(); }
        else if (action === 'recover-operation') await work(() => manager.recoverOperation(id), '已检查并恢复未完成操作。', true);
        else if (action === 'recover-settings') await work(() => manager.recoverLaunchSettings(id), '未完成设置事务已恢复。', true);
        else if (action === 'restore-environment') await work(() => manager.restoreEnvironment(id), '已恢复隔离前环境。', true);
        else if (action === 'clean-environment') { modal = { kind: 'cleanup', plan: unwrap(await (manager.previewEnvironmentCleanup || manager.prepareEnvironmentCleanup)(id)) }; renderModal(); }
        else if (action === 'open-folder') unwrap(await manager.openFolder(id));
        else if (action === 'feedback') await manager.exportFeedback(id);
        else if (action === 'anti-cheat') unwrap(await manager.openExternal('antiCheatPolicy'));
        else if (action === 'compatibility-search') unwrap(await manager.openExternal(`gameCompatibility:${id}`));
        else if (action === 'maintenance-tab') selectTab('maintenance');
        else if (action === 'cancel-launch') unwrap(await manager.cancelLaunch(id));
        else if (action === 'launch') await launchGame();
      } catch (failure) { message = failure.message; error = true; render(); }
    });
    host.addEventListener('keydown', event => {
      const current = event.target.closest('[data-gp-tab]');
      if (current && !scope.GameDetailTabs && ['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
        event.preventDefault(); const keys = ['overview', 'enhance', 'maintenance'], at = keys.indexOf(tab);
        selectTab(event.key === 'Home' ? keys[0] : event.key === 'End' ? keys[2] : keys[(at + (event.key === 'ArrowRight' ? 1 : 2)) % 3]);
      }
      if (event.key === 'Escape' && modal && !busy) { modal = null; host.querySelector('.gp-modal')?.remove(); }
      if (event.key === 'Tab' && modal) {
        const focusable = [...host.querySelectorAll('.gp-modal button:not([disabled]),.gp-modal input:not([disabled]),.gp-modal select:not([disabled]),.gp-modal textarea:not([disabled]),.gp-modal summary')], first = focusable[0], last = focusable.at(-1);
        if (event.shiftKey && event.target === first) { event.preventDefault(); last?.focus(); } else if (!event.shiftKey && event.target === last) { event.preventDefault(); first?.focus(); }
      }
    });
    if (manager.onLaunchSession) unsubscribe = manager.onLaunchSession(value => { if (!disposed && value.gameId === id) { session = value; if (!modal) render(); } });
    return { open, resume, refresh, selectTab, launchGame, requestLeave, hasDraft: dirty, discard: () => { draft = {}; invalidFields = {}; }, getState: () => ({ id, data, draft: structuredClone(draft), fields: structuredClone(fields), tab, busy, launching, loaded: [...loaded] }), dispose: () => { disposed = true; generation++; tabController?.dispose(); unsubscribe?.(); } };
  }
  const api = { mount, recommendedModel, initialFields };
  if (typeof module === 'object' && module.exports) module.exports = api; else scope.GamePageUi = api;
})(typeof window === 'object' ? window : globalThis);
