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
  function recommendedModel(quality) { return quality === 'performance' ? 'M' : quality === 'ultraPerformance' ? 'L' : ['quality', 'balanced', 'dlaa', 'custom'].includes(quality) ? 'K' : null; }
  function bindingLabel(binding) { if (!binding) return '未设置'; const key = binding.key === 36 ? 'Home' : binding.key === 45 ? 'Insert' : binding.key >= 112 && binding.key <= 135 ? `F${binding.key - 111}` : `VK ${binding.key}`; return [binding.ctrl && 'Ctrl', binding.shift && 'Shift', binding.alt && 'Alt', key].filter(Boolean).join(' + '); }
  function initialFields(assessment) {
    const settings = assessment.enhancements || {}, hardware = settings.hardware || assessment.hardware || {};
    const series = hardware.series || [], backend = series.includes('RTX40') && !series.includes('RTX50') ? 'mfgunlock' : series.includes('RTX50') && !series.includes('RTX40') ? 'nvidia' : '';
    return { sr: { backend: 'native', quality: 'quality', preset: 'auto', renderPercent: 67, ...(settings.requests?.sr?.request || settings.applied?.sr?.request || {}) },
      fg: { backend: hasFgComponents(assessment) && settings.fgComponents?.backend || backend, mode: 'restore', multiplier: 2, targetFps: 0, ...(settings.requests?.fg?.request || settings.applied?.fg?.request || {}) } };
  }
  function mount(host, manager, options = {}) {
    let id = null, data = null, draft = {}, fields = {}, tab = 'overview', busy = false, launching = false, message = '', error = false,
      modal = null, generation = 0, session = null, unsubscribe = null;
    const dirty = () => Object.keys(draft).length > 0;
    const act = (name, label, disabled = false, kind = '') => `<button type="button" class="button ${kind}" data-gp-action="${name}"${disabled ? ' disabled' : ''}>${label}</button>`;
    const selected = (name, baseline) => draft[name] ?? baseline;
    function specialRoute() {
      const api = selected('api', data.game.apiOverride === 'auto' || !data.game.apiOverride ? data.api?.effectiveApi : data.game.apiOverride);
      if (data.layout?.source === 'vulkan' || api === 'vulkan') return 'vulkan';
      if (data.layout?.source === 'feeder' || api === 'dx12' && data.game.nativeDlssAvailable === false && data.game.feeder) return 'feeder';
      return null;
    }
    function feature(domain) { return data?.enhancements?.featureStates?.[domain] || { eligible: false, blockers: [{ message: '尚未确认此功能的支持条件。' }] }; }
    function head() {
      const launch = session || data.launch?.session, pending = data.operation?.pending || data.deployment?.needsRecovery;
      return `<div class="gp-header"><div>${act('back', '← 返回游戏库', busy, 'subtle')}<h2>${esc(data.game.name)}</h2><p>${esc(data.game.launcher || '')} · ${apiLabel(data.api?.effectiveApi)} · ${esc(data.game.chosen?.bitness || 64)} 位</p></div>
        <div class="gp-start">${badge(data.game.installed ? 'deployed' : 'unverified', data.game.installed ? '' : '尚未安装')}
          ${act('launch', launching ? '正在等待游戏…' : '启动游戏', busy || launching || dirty() || pending, 'primary')}${launching ? act('cancel-launch', '取消等待') : ''}</div></div>
        <div class="gp-launch-status" role="status"><span>${esc(LAUNCH[launch?.status] || '尚未记录本次启动')}</span><span>${esc(data.launch?.effective === 'steam' ? '通过 Steam 启动' : '直接启动所选 EXE')}${launch?.process?.pid ? ` · PID ${esc(launch.process.pid)}` : ''}</span></div>
        ${launch?.error ? `<p class="gp-message error">${esc(launch.error.message)}</p>` : ''}
        <div class="gp-tabs" role="tablist" aria-label="游戏详情">${[['overview', '概览与兼容性'], ['enhance', '增强设置'], ['maintenance', '维护与备份']].map(([key, label]) => `<button type="button" role="tab" data-gp-tab="${key}" aria-selected="${tab === key}" tabindex="${tab === key ? 0 : -1}">${label}</button>`).join('')}</div>`;
    }
    function overview() {
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
    function nrFields() {
      const nr = { ...(data.nr || {}), ...(draft.nr || {}) }, available = Boolean(data.game.installed && data.nr);
      return `<section class="gp-section"><div class="gp-section-title"><h3>NR 画面增强</h3>${badge(available ? 'configurable' : 'unavailable')}</div>
        ${!available ? '<p class="gp-caption">应用完整 Core 配套后可调整；游戏内的格式支持与 NR 成功仍需验证。</p>' : ''}
        <label class="check-line gp-check"><input type="checkbox" data-gp-group="nr" data-gp-field="Enabled"${nr.Enabled ? ' checked' : ''}${available ? '' : ' disabled'}>开启 NR</label>
        <div class="gp-controls">${NR.map(([key, label, min, max, step]) => {
          const capable = available && (!Object.hasOwn(data.nr?.capabilities || {}, key) || data.nr.capabilities[key] === true);
          if (NR_CHOICES[key]) return selectField('nr', key, label, NR_CHOICES[key].map((label, value) => option(value, label, nr[key] ?? 0)).join(''), !capable);
          return `<label class="gp-field"><span>${label}</span><div class="gp-range"><input type="range" min="${min}" max="${max}" step="${step}" value="${esc(nr[key] ?? min)}" data-gp-group="nr" data-gp-field="${key}"${capable ? '' : ' disabled'}><output>${esc(key === 'SkinStructureStrength' && Number(nr[key]) === -1 ? '关闭' : nr[key] ?? min)}</output></div>${capable ? key === 'CustomWorkScale' ? '<small>工作模式选为“自定义”后生效。</small>' : '' : '<small>当前 Core 或配置未提供此项。</small>'}</label>`;
        }).join('')}</div><label class="check-line gp-check"><input type="checkbox" data-gp-group="nr" data-gp-field="AutoMask"${nr.AutoMask ? ' checked' : ''}${available ? '' : ' disabled'}>自动遮罩</label>
        <p class="gp-caption">NR 工作比例、DLSS 超分档位和输出分辨率分别控制不同阶段。</p></section>`;
    }
    function featurePanel(domain) {
      const info = feature(domain), f = fields[domain], owned = data.enhancements?.applied?.[domain], active = info.eligible === true, allowRestore = Boolean(owned) || domain === 'fg' && hasFgComponents(data), sr = domain === 'sr';
      const reasons = (info.blockers || []).map(row => row.message || row).join('；');
      let controls;
      if (sr) {
        const model = recommendedModel(f.quality);
        controls = selectField('sr', 'quality', 'DLSS 档位', Object.entries({ game: '恢复游戏控制', preserve: '保持游戏档位，仅选模型', dlaa: 'DLAA', quality: '质量', balanced: '平衡', performance: '性能', ultraPerformance: '超级性能', custom: '自定义比例' }).map(([key, label]) => option(key, label, f.quality, !active && key !== 'game')).join(''), !active && !allowRestore) +
          selectField('sr', 'preset', '模型预设', option('auto', 'NVIDIA Recommended', f.preset) + ['K', 'L', 'M'].map(value => option(value, value, f.preset)).join(''), !active || f.quality === 'game', f.preset === 'auto' ? model ? `按所选档位请求 ${model}；实际采用待游戏确认。` : '推荐值取决于游戏实际档位，目前未读取该档位。' : '') +
          `<label class="gp-field"><span>输入比例</span><input type="number" min="33" max="100" step="1" value="${esc(f.renderPercent)}" data-gp-group="sr" data-gp-field="renderPercent"${active && f.quality === 'custom' ? '' : ' disabled'}><small>仅自定义档位写入，范围 33–100%。</small></label>`;
      } else {
        const modes = info.availableModes || [], multipliers = info.availableMultipliers || [];
        controls = selectField('fg', 'mode', '补帧模式', Object.entries({ restore: '恢复原有设置', follow: '跟随游戏倍率', off: '驱动关闭 FG', fixed: '固定总倍率', dynamic: '动态目标帧率' }).map(([key, label]) => option(key, label, f.mode, key !== 'restore' && (!active || !modes.includes(key)))).join(''), !active && !allowRestore) +
          selectField('fg', 'multiplier', '总帧率倍率', [2, 3, 4, 5, 6].map(value => option(value, `${value}×${multipliers.includes(value) ? '' : ' · 尚未确认支持'}`, f.multiplier, !multipliers.includes(value))).join(''), !active || f.mode !== 'fixed', '包含一帧渲染；倍率按具体游戏、驱动和后端开放。') +
          `<label class="gp-field"><span>动态目标帧率</span><input type="number" min="0" max="1000" step="1" value="${esc(f.targetFps)}" data-gp-group="fg" data-gp-field="targetFps"${active && f.mode === 'dynamic' && modes.includes('dynamic') ? '' : ' disabled'}><small>${f.backend === 'mfgunlock' ? '当前 RTX40 MFG Unlock 没有动态模式。' : '0 表示自动目标。'}</small></label>`;
      }
      return `<section class="gp-section"><div class="gp-section-title"><div><h3>${sr ? 'DLSS 超分' : f.backend === 'mfgunlock' ? 'RTX40 · 社区 MFG Unlock' : f.backend === 'nvidia' ? 'RTX50 · NVIDIA 官方帧生成' : '帧生成'}</h3></div>${badge(info.state || (active ? 'configurable' : 'unavailable'))}</div>
        ${reasons ? `<p class="gp-caption">${esc(reasons)}</p>` : ''}${info.canConfirm ? `<div class="gp-confirm-feature"><span>已在游戏设置中开启 ${sr ? 'DLSS 超分' : '帧生成'}，并退出游戏？</span>${act(`confirm-${domain}`, '我已开启，重新检查', busy)}</div>` : ''}
        <div class="gp-controls">${controls}</div><p class="gp-caption">${owned ? owned.readbackVerified ? '配置读回一致；已应用，需重启游戏，实际效果待确认。' : '受管配置已变化，请重新预览后明确应用。' : '尚未应用本管理器的覆盖请求。'}${info.evidence?.confirmation ? ' 游戏开关来源：用户确认。' : ''}</p>
        ${allowRestore ? act(`restore-${domain}`, '将恢复原设置加入本次应用', busy) : ''}${owned?.requiresReapply ? act('reapply', '重新预览外部修改后的设置', busy) : ''}</section>`;
    }
    function enhance() {
      const game = data.game, layout = data.layout || {}, special = specialRoute(), info = special ? game[special] || {} : null;
      const versions = special ? [{ id: info.packageId || info.coreVersion || '', label: `${special === 'vulkan' ? 'Vulkan' : 'DX12 Feeder'} · ${info.coreVersion || '固定配套'}`, notes: info.reason || info.blockers?.join('；') || '使用此路线的固定成套组件，实际 NR 仍需本次游戏验证。', ready: info.installed || info.available || info.selectionAvailable }] : Array.isArray(data.coreVersions) ? data.coreVersions : [], mode = selected('deployment', layout.mode || 'local'),
        api = selected('api', game.apiOverride || 'auto'), version = selected('version', game.addonVersion || data.deployment?.version || versions.find(row => row.id === '0.4.7beta')?.id || versions[0]?.id || '');
      const choice = versions.find(row => row.id === version), nativeApi = ['dx11', 'dx12', 'mixed', 'unknown'].includes(data.api?.effectiveApi);
      return `<section class="gp-section" role="tabpanel" aria-label="增强设置"><h3>API、核心与加载</h3><div class="gp-controls">
        ${selectField('route', 'api', '游戏 API', ['auto', 'dx11', 'dx12', 'vulkan'].map(key => option(key, API[key], api)).join(''))}
        ${selectField('route', 'version', 'Core 配套', versions.map(row => option(row.id, row.label || row.id, special ? versions[0]?.id : version, row.ready === false)).join(''), Boolean(special))}
        ${selectField('route', 'deployment', '运行目录', special ? option(mode, `${special === 'vulkan' ? 'Vulkan 外置' : 'Feeder 专用'}运行目录`, mode) : option('local', '游戏目录', mode) + option('external', '每游戏外置目录', mode, !nativeApi), Boolean(special), '组件来源与活动运行目录分别记录。')}
        ${selectField('route', 'loadingMode', '加载方式', option('proxy', '图形代理加载', selected('loadingMode', layout.loadingMode || 'proxy')) + option('helper', '外置加载助手', selected('loadingMode', layout.loadingMode || 'proxy'), mode !== 'external'), mode !== 'external', '助手只报告本次就绪与加载结果；不代表 NR 成功。')}
        ${selectField('route', 'launchMode', '启动方式', option('auto', '自动 · Steam 游戏优先 Steam', selected('launchMode', data.launch?.selected || 'auto')) + option('steam', '通过 Steam', selected('launchMode', data.launch?.selected || 'auto'), !data.launch?.steamAvailable) + option('exe', '直接启动所选 EXE', selected('launchMode', data.launch?.selected || 'auto')))}</div>
        ${special ? `<p class="gp-caption">${esc(versions[0]?.notes)}</p>` : choice?.notes ? `<p class="gp-caption">${esc(choice.notes)}</p>` : ''}${!game.installed ? act('prepare', '将完整配套加入本次应用', busy || !version || special && versions[0]?.ready === false, 'primary') : ''}</section>
        ${nrFields()}${featurePanel('sr')}${featurePanel('fg')}
        <section class="gp-section"><h3>游戏内面板</h3><div class="gp-facts">${line('ReShade 面板快捷键', bindingLabel(draft.hotkeys?.reshade || data.hotkeys?.reshade))}${line('NR 切换键', data.hotkeys?.nr?.label || '由当前 Core 提供')}</div>
        <p class="gp-caption">RenoDX HDR 与本项目 NR 分别加载。面板或快捷键冲突时，可先调整 ReShade 面板键；此操作保留 HDR 与滤镜设置。</p>
        <div class="gp-actions">${act('panel-insert', '面板改用 Insert', busy || !data.game.installed)}${act('panel-ctrl-home', '面板改用 Ctrl + Home', busy || !data.game.installed)}${act('panel-home', '恢复 Home', busy || !data.game.installed)}</div></section>`;
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
    function footer() { return `<div class="gp-apply-bar"><div><strong>${dirty() ? `有 ${Object.keys(draft).length} 组修改待应用` : '暂无待应用修改'}</strong><small>${dirty() ? '修改暂存；组件准备和设置将在同一次应用中完成。' : '实际增强效果请在概览中分别核对。'}</small></div><div>${act('discard', '放弃修改', busy || !dirty())}${act('preview', '预览并应用', busy || !dirty() || data.operation?.pending, 'primary')}</div></div>`; }
    function render() {
      if (!data) { host.innerHTML = `<div class="gp-section">${act('back', '← 返回游戏库')}<p role="status">${esc(message || '正在读取游戏状态…')}</p>${error ? act('refresh', '重试') : ''}</div>`; return; }
      host.innerHTML = head() + `<div class="gp-message${error ? ' error' : ''}" role="${error ? 'alert' : 'status'}"${message ? '' : ' hidden'}>${esc(message)}</div>` + (tab === 'overview' ? overview() : tab === 'enhance' ? enhance() : maintenance()) + footer();
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
    function updateBar() { const bar = host.querySelector('.gp-apply-bar'); if (bar) bar.outerHTML = footer(); const start = host.querySelector('[data-gp-action="launch"]'); if (start) start.disabled = busy || launching || dirty() || data.operation?.pending; }
    async function refresh(preserve = true) {
      if (!id) return; const token = ++generation; const value = unwrap(await manager.assessGame(id)); if (token !== generation) return;
      data = value; session = value.launch?.session; if (!preserve || !fields.sr) fields = initialFields(data); render();
    }
    async function work(fn, success, preserve = false) {
      if (busy) return; busy = true; message = ''; error = false; render();
      try { const result = unwrap(await fn()); message = result?.notice || success || '操作已完成。'; if (!preserve) { draft = {}; fields = {}; } modal = null; await refresh(preserve); return result; }
      catch (failure) { message = failure.message; error = true; modal = null; try { await refresh(true); } catch {} }
      finally { busy = false; render(); }
    }
    async function preview(request = draft) {
      if (busy) return; busy = true; message = ''; error = false; render();
      try { modal = { kind: 'apply', plan: unwrap(await manager.previewOperation(id, request)) }; }
      catch (failure) { message = failure.message; error = true; }
      finally { busy = false; render(); }
    }
    async function apply(elevated = false) {
      if (!modal || busy) return;
      const value = modal, allowAntiCheat = host.querySelector('[data-gp-consent]')?.checked === true;
      if ((data.antiCheat?.detected || value.plan?.deployment?.requiresAntiCheat) && !allowAntiCheat) { host.querySelector('.gp-modal-message').textContent = '请先确认已了解本次游戏的反作弊提示。'; return; }
      if (value.kind === 'cleanup') {
        const names = [...host.querySelectorAll('[data-gp-clean]:checked')].map(input => input.dataset.gpClean);
        if (!names.length) { host.querySelector('.gp-modal-message').textContent = '请先选择需要隔离的文件。'; return; }
        await work(() => manager.applyEnvironmentCleanup(id, value.plan.planId, names), '所选文件已备份隔离，可撤销。');
      } else await work(() => (elevated ? manager.applyOperationElevated : manager.applyOperation)(id, value.plan.planId,
        { confirm: true, fingerprint: value.plan.fingerprint, allowAntiCheat }), '配置已应用；实际增强待游戏验证。');
      options.onChanged?.(id);
    }
    function requestLeave(callback) { if (busy) return false; if (!dirty()) { callback(); return true; } modal = { kind: 'leave', callback }; renderModal(); return false; }
    async function open(gameId, initialTab = 'overview') {
      id = gameId; data = null; draft = {}; fields = {}; tab = initialTab; modal = null; message = ''; error = false; render();
      try { await refresh(false); } catch (failure) { message = failure.message; error = true; render(); }
    }
    function change(input) {
      const group = input.dataset.gpGroup, key = input.dataset.gpField; if (!key || input.disabled || busy) return;
      const value = input.type === 'checkbox' ? Number(input.checked) : group === 'nr' || input.type === 'range' || input.type === 'number' ? Number(input.value) : input.value;
      if (group === 'nr') {
        draft.nr = { ...(draft.nr || {}), [key]: value };
        if (value === data.nr?.[key]) delete draft.nr[key]; if (!Object.keys(draft.nr).length) delete draft.nr;
        const out = input.parentElement.querySelector('output'); if (out) out.textContent = key === 'SkinStructureStrength' && value === -1 ? '关闭' : value;
      } else if (group === 'route') {
        draft[key] = value;
        if (key === 'deployment' && value === 'local') delete draft.loadingMode;
        const route = specialRoute(); if (route) { draft.route = route; draft.version = data.game[route]?.packageId || data.game[route]?.coreVersion; delete draft.loadingMode; }
      } else if (group === 'sr' || group === 'fg') {
        fields[group][key] = value;
        if (group === 'sr' && key === 'quality') {
          const ratio = { dlaa: 100, quality: 67, balanced: 59, performance: 50, ultraPerformance: 33 }[value];
          if (ratio) fields.sr.renderPercent = ratio;
        }
        try { draft[group] = scope.launchSettingsUi.createRequest(group, fields[group]); message = ''; error = false; }
        catch (failure) { message = failure.message; error = true; }
      }
      if (input.tagName === 'SELECT') { const field = input.dataset.gpField; render(); host.querySelector(`[data-gp-group="${group}"][data-gp-field="${field}"]`)?.focus(); }
      else updateBar();
    }
    host.addEventListener('input', event => { if (event.target.type === 'range' || event.target.type === 'number') change(event.target); });
    host.addEventListener('change', event => { if (!['range', 'number'].includes(event.target.type)) change(event.target); });
    host.addEventListener('click', async event => {
      const tabButton = event.target.closest('[data-gp-tab]'); if (tabButton) { tab = tabButton.dataset.gpTab; render(); host.querySelector(`[data-gp-tab="${tab}"]`)?.focus(); return; }
      const button = event.target.closest('[data-gp-action]'); if (!button || button.disabled) return; const action = button.dataset.gpAction;
      try {
        if (action === 'back') requestLeave(() => options.onBack?.());
        else if (action === 'refresh') { await refresh(true); message = '已重新检查当前状态。'; render(); }
        else if (action === 'preview') await preview();
        else if (action === 'discard') { draft = {}; fields = initialFields(data); message = '已放弃尚未应用的修改。'; render(); }
        else if (action === 'prepare') { draft.api = selected('api', data.game.apiOverride || 'auto'); draft.version = host.querySelector('[data-gp-field="version"]').value; draft.deployment = selected('deployment', data.layout?.mode || 'local'); const route = specialRoute(); if (route) { draft.route = route; draft.api = route === 'vulkan' ? 'vulkan' : 'dx12'; } updateBar(); }
        else if (action.startsWith('panel-')) { draft.hotkeys = { reshade: { key: action === 'panel-insert' ? 45 : 36, ctrl: action === 'panel-ctrl-home', shift: false, alt: false } }; render(); }
        else if (action === 'restore-sr' || action === 'restore-fg') { const domain = action.slice(-2); fields[domain][domain === 'sr' ? 'quality' : 'mode'] = domain === 'sr' ? 'game' : 'restore'; draft[domain] = scope.launchSettingsUi.createRequest(domain, fields[domain]); render(); }
        else if (action === 'reapply') { draft.reapplyExternalChanges = true; for (const domain of ['sr', 'fg']) if (data.enhancements?.applied?.[domain]?.requiresReapply) draft[domain] = data.enhancements.applied[domain].request; await preview(); }
        else if (action === 'modal-cancel') { modal = null; host.querySelector('.gp-modal')?.remove(); }
        else if (action === 'leave-confirm') { const callback = modal.callback; draft = {}; modal = null; callback?.(); }
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
        else if (action === 'confirm-sr' || action === 'confirm-fg') await work(() => manager.confirmGameFeature(id, action.slice(-2), { enabled: true }), '已记住本游戏程序的开关确认；这不是运行证据。', true);
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
        else if (action === 'maintenance-tab') { tab = 'maintenance'; render(); }
        else if (action === 'cancel-launch') unwrap(await manager.cancelLaunch(id));
        else if (action === 'launch') {
          launching = true; message = ''; error = false; render();
          try { const result = unwrap(await manager.launch(id)); session = result.launched || result; await refresh(true); }
          catch (failure) { message = failure.message; error = true; session = failure.details?.launchSession || session; }
          finally { launching = false; render(); }
        }
      } catch (failure) { message = failure.message; error = true; render(); }
    });
    host.addEventListener('keydown', event => {
      const current = event.target.closest('[data-gp-tab]');
      if (current && ['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
        event.preventDefault(); const keys = ['overview', 'enhance', 'maintenance'], at = keys.indexOf(tab);
        tab = event.key === 'Home' ? keys[0] : event.key === 'End' ? keys[2] : keys[(at + (event.key === 'ArrowRight' ? 1 : 2)) % 3]; render(); host.querySelector(`[data-gp-tab="${tab}"]`)?.focus();
      }
      if (event.key === 'Escape' && modal && !busy) { modal = null; host.querySelector('.gp-modal')?.remove(); }
      if (event.key === 'Tab' && modal) {
        const focusable = [...host.querySelectorAll('.gp-modal button:not([disabled]),.gp-modal input:not([disabled]),.gp-modal select:not([disabled]),.gp-modal textarea:not([disabled]),.gp-modal summary')], first = focusable[0], last = focusable.at(-1);
        if (event.shiftKey && event.target === first) { event.preventDefault(); last?.focus(); } else if (!event.shiftKey && event.target === last) { event.preventDefault(); first?.focus(); }
      }
    });
    if (manager.onLaunchSession) unsubscribe = manager.onLaunchSession(value => { if (value.gameId === id) { session = value; render(); } });
    return { open, refresh, requestLeave, hasDraft: dirty, discard: () => { draft = {}; }, getState: () => ({ id, data, draft: structuredClone(draft), tab, busy, launching }), dispose: () => { generation++; unsubscribe?.(); } };
  }
  const api = { mount, recommendedModel, initialFields };
  if (typeof module === 'object' && module.exports) module.exports = api; else scope.GamePageUi = api;
})(typeof window === 'object' ? window : globalThis);
