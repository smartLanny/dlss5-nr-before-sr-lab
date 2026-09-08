'use strict';

// A renderer-only editor. All reads and mutations go through the launch service.
(function (scope) {
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
  const QUALITY = { game: '交还游戏控制', preserve: '保持当前档位（仅选模型）', dlaa: 'DLAA · 原生分辨率', quality: '质量', balanced: '平衡', performance: '性能', ultraPerformance: '超级性能', custom: '自定义输入比例' };
  const PRESET_PERCENT = { dlaa: 100, quality: 67, balanced: 59, performance: 50, ultraPerformance: 33 };
  const MODES = { restore: '使用原有设置', follow: '跟随游戏开关', off: '关闭帧生成（驱动）', fixed: '固定倍率', dynamic: '动态目标帧率' };
  const BACKENDS = { native: '原生 DLSS', optiscaler: 'OptiScaler', nvidia: 'NVIDIA 官方 FG', rtx40: '社区 RTX40 MFG' };
  const DRIVER_KEYS = { 0x10AFB768: 'SR 画质档位', 0x10E41E01: 'SR 覆盖开关', 0x10E41DF3: 'SR 模型', 0x10E41DF5: 'SR 输入比例', 0x10308298: 'FG 模式', 0x104D6667: 'FG 生成帧数', 0x10562D0F: 'FG 动态上限', 0x10CF4125: 'FG 目标帧率' };
  const option = (value, label, selected) => `<option value="${esc(value)}"${selected === value ? ' selected' : ''}>${esc(label)}</option>`;

  function unwrap(result) {
    if (!result || result.ok !== true) throw Object.assign(new Error(result?.error?.message || '设置操作失败。'), result?.error || {});
    return result.value;
  }

  function hardwareFacts(hardware = {}) {
    const names = Array.isArray(hardware.names) ? hardware.names : [];
    const named = names.map(name => String(name).match(/RTX\s*(20|30|40|50)\d{2}(?:\D|$)/i)?.[1]).filter(Boolean).map(s => `RTX${s}`);
    const series = [...new Set([...(Array.isArray(hardware.series) ? hardware.series : []), ...named].filter(s => /^RTX(?:20|30|40|50)$/.test(s)))];
    // The installation family RTX40 also includes RTX20/30; it is not FG evidence.
    const known = hardware.source !== 'unavailable' && series.length === 1 && hardware.family !== 'mixed';
    const relevantNames = names.filter(name => /RTX\s*(20|30|40|50)\d{2}(?:\D|$)/i.test(name));
    return { label: relevantNames.length ? relevantNames.join(' / ').replace(/NVIDIA (?:GeForce )?/g, '') : known ? series[0].replace('RTX', 'RTX ') : '显卡未确认', allNames: names.join(' / '),
      series: known ? series[0] : null, fgBackend: known && series[0] === 'RTX40' ? 'rtx40' : known && series[0] === 'RTX50' ? 'nvidia' : null };
  }

  function createRequest(domain, fields) {
    const backend = fields.backend;
    if (domain === 'sr') {
      if (!['native', 'optiscaler'].includes(backend)) throw new Error('请选择 SR 后端。');
      const quality = fields.quality;
      if (!Object.hasOwn(QUALITY, quality) || backend === 'optiscaler' && quality === 'preserve') throw new Error('请选择有效 SR 档位。');
      const request = { backend, quality };
      if (quality === 'game') return request;
      if (quality === 'custom') {
        const percent = Number(fields.renderPercent);
        if (fields.renderPercent === '' || !Number.isFinite(percent) || percent > 100 || percent < (backend === 'native' ? 33 : 100 / 3) || backend === 'native' && !Number.isInteger(percent))
          throw new Error(backend === 'native' ? '原生 DLSS 输入比例需为 33–100 的整数。' : 'OptiScaler 输入比例需为 33.334–100%。');
        request.renderPercent = percent;
      }
      if (backend === 'native' && fields.preset) {
        if (!['K', 'L', 'M'].includes(fields.preset)) throw new Error('请选择 K、L 或 M 模型。');
        request.preset = fields.preset;
      }
      if (quality === 'preserve' && !request.preset) throw new Error('仅改模型时，请选择 K、L 或 M。');
      return request;
    }
    if (domain !== 'fg' || !['nvidia', 'rtx40'].includes(backend)) throw new Error('当前显卡尚未确认可用的 FG 设置后端。');
    const mode = fields.mode;
    if (!(backend === 'nvidia' ? ['restore', 'off', 'fixed', 'dynamic'] : ['restore', 'follow', 'fixed', 'dynamic']).includes(mode)) throw new Error('请选择有效 FG 模式。');
    const request = { backend, mode };
    if (mode === 'fixed') {
      const multiplier = Number(fields.multiplier);
      if (!Number.isInteger(multiplier) || multiplier < 2 || multiplier > 6) throw new Error('固定总倍率需为 2–6。');
      request.multiplier = multiplier;
    }
    if (mode === 'dynamic') {
      const targetFps = Number(fields.targetFps);
      if (fields.targetFps === '' || !Number.isInteger(targetFps) || targetFps < 0 || targetFps > 1000) throw new Error('动态目标需为 0（自动）或 1–1000 FPS 的整数。');
      request.targetFps = targetFps;
      if (backend === 'rtx40') request.experimental56 = fields.experimental56 === true;
    }
    return request;
  }

  function requestLabel(domain, request) {
    if (!request) return '未保存覆盖请求';
    const parts = [BACKENDS[request.backend] || request.backend];
    if (domain === 'sr') {
      parts.push(QUALITY[request.quality] || request.quality);
      if (request.quality === 'custom') parts.push(`输入 ${request.renderPercent}%`);
      if (request.preset) parts.push(`模型 ${request.preset}`);
    } else {
      parts.push(MODES[request.mode] || request.mode);
      if (request.mode === 'fixed') parts.push(`总计 ${request.multiplier}×（含 1 帧渲染帧）`);
      if (request.mode === 'dynamic') parts.push(request.targetFps === 0 ? '自动目标' : `${request.targetFps} FPS`);
      if (request.experimental56) parts.push('允许实验性 5/6×');
    }
    return parts.join(' · ');
  }

  function initialFields(domain, data) {
    const request = data.requests?.[domain]?.request || data.applied?.[domain]?.request;
    const legacy = legacyState(data);
    if (domain === 'sr') {
      // A proposed next-launch value is not a measurement of the game's current setting.
      const fields = { backend: 'native', quality: 'quality', preset: legacy?.preset || '', renderPercent: 67, ...request };
      fields.backend = 'native';
      if (!Object.hasOwn(PRESET_PERCENT, fields.quality) && fields.quality !== 'custom') fields.quality = 'quality';
      if (Object.hasOwn(PRESET_PERCENT, fields.quality)) fields.renderPercent = PRESET_PERCENT[fields.quality];
      return fields;
    }
    return { backend: hardwareFacts(data.hardware).fgBackend || '', mode: 'restore', multiplier: 2, targetFps: 0, experimental56: false, ...request };
  }

  function legacyState(data) {
    const legacy = data.legacy;
    if (!legacy || legacy.managed === true || legacy.error || data.requests?.sr || data.applied?.sr) return null;
    const effective = String(legacy.effective || '').toUpperCase();
    const preset = ['K', 'L', 'M'].includes(effective) ? effective : null;
    const label = preset ? `${legacy.selection === 'auto' ? '自动 → ' : ''}模型 ${preset}` : 'NVIDIA 默认 / 不覆盖';
    return { preset, label };
  }

  function statusMarkup(domain, data, dirty) {
    const saved = data.requests?.[domain]?.request;
    const applied = data.applied?.[domain];
    const readback = applied?.readbackVerified === true || applied?.configVerified === true;
    const legacy = domain === 'sr' ? legacyState(data) : null;
    const legacyError = domain === 'sr' && !saved && data.legacy?.error;
    const nextLaunch = legacy ? `沿用原模型设置：${legacy.label}` : legacyError ? '旧模型读取失败，启动覆盖状态未确认' : requestLabel(domain, saved);
    return `<div class="launch-status" aria-live="polite"><span class="badge ${dirty ? 'warn' : saved ? 'good' : ''}">${dirty ? '尚未保存' : saved ? '请求已保存' : legacy ? '沿用原模型设置' : legacyError ? '状态未确认' : '未设启动覆盖'}</span><span class="badge ${readback ? 'good' : ''}">${readback ? '配置已读回' : applied ? '有应用记录' : legacy ? '旧模型未读回' : '尚未应用'}</span><span class="badge">游戏未验证</span></div><p class="launch-saved">下次启动：${esc(nextLaunch)}</p>${legacy ? '<p class="config-note">保存本页后接管 SR 设置；当前仍沿用原模型选择。</p>' : ''}${applied?.request ? `<p class="launch-saved">应用记录：${esc(requestLabel(domain, applied.request))}</p>` : ''}`;
  }

  function srFields(fields) {
    return `<label class="launch-field"><span>画质档位</span><select data-ls-field="quality" aria-label="SR 画质档位">${Object.entries(QUALITY).filter(([key]) => key !== 'game' && key !== 'preserve').map(([key, label]) => option(key, label, fields.quality)).join('')}</select></label>
      <label class="launch-field"><span>输入分辨率比例</span><div class="launch-number"><input data-ls-field="renderPercent" type="number" min="33" max="100" step="1" value="${esc(fields.renderPercent)}" aria-label="SR 输入分辨率百分比"><span>%</span></div><small>档位显示近似比例；修改数字后使用自定义比例。按宽、高计算。</small></label>
      <label class="launch-field"><span>DLSS SR 模型</span><select data-ls-field="preset" aria-label="DLSS SR 模型">${option('', '使用原有模型', fields.preset)}${['K', 'L', 'M'].map(key => option(key, `模型 ${key}`, fields.preset)).join('')}</select></label>
      <p class="config-note">用于游戏已有的原生 DLSS。以上是下次启动设置，不代表已读取游戏当前档位。</p>`;
  }

  function hasLegacyOptiScaler(data) {
    return [data.requests?.sr, data.applied?.sr].some(entry => entry?.request?.backend === 'optiscaler');
  }

  function fgFields(fields, hardware) {
    const facts = hardwareFacts(hardware);
    const available = facts.fgBackend && fields.backend === facts.fgBackend;
    if (!available) return `<p class="launch-unavailable">${facts.series === 'RTX20' || facts.series === 'RTX30' ? '当前识别为 RTX 20 / 30；本页未确认可用的 FG 控制路线。' : !facts.fgBackend ? '未确认单一 RTX 40 / 50 显卡，暂不提供 FG 覆盖。' : '已保存的 FG 后端与当前显卡不一致，请先还原本工具设置。'}</p><p class="config-note">这里仅管理已有 FG 后端，不为没有 FG 的游戏注入帧生成。已保存请求仍显示在下方。</p>`;
    const community = fields.backend === 'rtx40';
    const modes = community ? ['restore', 'follow', 'fixed', 'dynamic'] : ['restore', 'off', 'fixed', 'dynamic'];
    return `<label class="launch-field"><span>控制模式</span><select data-ls-field="mode" aria-label="FG 控制模式">${modes.map(key => option(key, MODES[key], fields.mode)).join('')}</select></label>
      ${fields.mode === 'fixed' ? `<label class="launch-field"><span>总帧倍率</span><select data-ls-field="multiplier" aria-label="FG 总帧倍率">${[2, 3, 4, 5, 6].map(value => option(String(value), `${value}× · 1 帧渲染 + ${value - 1} 帧生成`, String(fields.multiplier))).join('')}</select><small>这是请求范围，实际可用倍率需游戏运行后确认。</small></label>` : ''}
      ${fields.mode === 'dynamic' ? `<label class="launch-field"><span>动态目标</span><div class="launch-number"><input data-ls-field="targetFps" type="number" min="0" max="1000" step="1" value="${esc(fields.targetFps)}" aria-label="FG 动态目标帧率"><span>FPS</span></div><small>0 为自动，或填写 1–1000 FPS。</small></label>${community ? `<label class="launch-check"><input data-ls-field="experimental56" type="checkbox"${fields.experimental56 ? ' checked' : ''}> 允许动态模式实验性 5/6×</label>` : ''}` : ''}
      <p class="config-note">${fields.mode === 'restore' ? '撤销管理器的 FG 覆盖，恢复原有配置；这不会关闭帧生成。' : !community && fields.mode === 'off' ? '向 NVIDIA 驱动写入关闭设置。游戏菜单中的开关可能不变，实际是否关闭需在游戏中确认。' : community ? '兼容组件用于游戏已有的帧生成。跟随游戏开关不会强制关闭 FG。' : '使用 RTX 50 原生帧生成；请在游戏中开启 DLSS FG。'}</p><p class="config-note">游戏内 FG 开关：暂无法直接读取。</p>`;
  }

  function fgComponentMarkup(data, busy) {
    const info = data.fgComponents;
    const facts = hardwareFacts(data.hardware);
    if (!info) return '<div class="launch-components"><p>正在确认帧生成组件状态；请重新读取后再操作。</p></div>';
    const native = info.route === 'native' || facts.fgBackend === 'nvidia';
    const blockers = Array.isArray(info.blockers) ? info.blockers : [];
    const missing = Array.isArray(info.missing) ? info.missing : [];
    return `<div class="launch-components"><strong>${native ? 'RTX 50 · 原生帧生成' : info.route === 'compatibility' ? 'RTX 40 · 兼容帧生成' : '帧生成路线待确认'}</strong>
      <p>${native ? info.needsCleanup ? '需要先移除原来的兼容组件，再使用原生路线。' : '使用游戏和 NVIDIA 驱动的原生帧生成，无需另装兼容组件。' : info.ready ? '兼容组件已就绪，可以设置倍率。首次使用请在游戏内开启帧生成。' : '先检查系统运行库，再由管理器准备兼容插件与加载器。'}</p>
      ${blockers.length ? `<p class="launch-error">${blockers.map(esc).join('<br>')}</p>` : ''}
      ${missing.length ? `<details><summary>待准备的组件（${missing.length}）</summary><ul>${missing.map(item => `<li>${esc(typeof item === 'string' ? item : item.label || item.name || item.role)}</li>`).join('')}</ul></details>` : ''}
      ${!native && info.runtime && info.runtime.ready !== true ? `<div class="payload-source-actions">${info.runtime.repair !== 'game-files' ? `<button class="button primary" type="button" data-ls-action="runtime-help"${busy ? ' disabled' : ''}>下载 VC++ x64 运行库</button>` : ''}<button class="button" type="button" data-ls-action="runtime-check"${busy ? ' disabled' : ''}>重新检查运行库</button></div><p class="config-note">${info.runtime.repair === 'game-files' ? '请先验证游戏安装文件，再重新检查。管理器不会覆盖游戏自带的运行库。' : '微软官方安装程序。完成安装或修复后返回此处；不会自动修改游戏文件。'}</p>` : ''}
      ${info.needsCleanup ? `<button class="button primary" type="button" data-ls-action="native-route"${busy ? ' disabled' : ''}>切换为原生帧生成</button>` : !info.ready && info.canPrepare ? `<button class="button primary" type="button" data-ls-action="prepare"${busy ? ' disabled' : ''}>准备兼容组件</button>` : ''}
      ${!native && info.managed ? `<button class="button subtle" type="button" data-ls-action="remove-components"${busy ? ' disabled' : ''}>移除兼容组件</button>` : ''}
    </div>`;
  }

  function previewMarkup(preview, fields) {
    if (!preview) return '';
    const plan = preview.plan;
    const blockers = Array.isArray(plan.blockers) ? plan.blockers : [];
    const warnings = Array.isArray(plan.warnings) ? plan.warnings : [];
    const exe = plan.exe || plan.target?.exe;
    const destination = plan.destination || (['native', 'nvidia'].includes(plan.backend) ? '此游戏的驱动配置' : plan.backend === 'optiscaler' ? 'OptiScaler.ini' : '社区 FG 控制配置');
    const operations = Array.isArray(plan.operations) ? plan.operations : [];
    return `<div class="launch-preview" role="region" aria-label="设置应用预览"><strong>应用预览</strong><p>${esc(requestLabel(plan.domain || preview.domain, plan.request || fields))}</p><p>写入位置：${esc(destination)}</p>${exe ? `<p class="launch-target">目标 EXE：${esc(exe)}</p>` : ''}
      ${operations.length ? `<details><summary>查看设置变更</summary><pre>${esc(JSON.stringify(operations, null, 2))}</pre></details>` : plan.driverSettings?.length ? `<p>涉及设置：${plan.driverSettings.map(key => esc(DRIVER_KEYS[key] || key)).join('、')}</p>` : ''}
      ${plan.noOp ? '<p>与当前配置一致，无需改写。</p>' : ''}${warnings.length ? `<details${plan.requiresReview ? ' open' : ''}><summary>后端提示（${warnings.length}）</summary><ul>${warnings.map(w => `<li>${esc(w)}</li>`).join('')}</ul></details>` : ''}
      ${blockers.length ? `<p class="launch-error">${blockers.map(esc).join('<br>')}</p>` : '<p>确认后写入配置。游戏需完全退出；配置读回后仍需游戏内验证。</p>'}
      <div class="launch-actions"><button type="button" class="button" data-ls-action="cancel">取消</button><button type="button" class="button primary" data-ls-action="confirm"${blockers.length ? ' disabled' : ''}>确认应用</button></div></div>`;
  }

  function mount(host, id, options = {}) {
    if (!host || host.launchSettingsController) return host?.launchSettingsController;
    const manager = options.manager || scope.manager;
    let data = null, busy = false, generation = 0;
    const drafts = {}, dirty = {}, messages = {}, previews = {};
    const attached = () => host.isConnected !== false;
    function setMessage(domain, text, error = false) { messages[domain] = { text, error }; }
    function render() {
      if (!attached() || !data) return;
      const facts = hardwareFacts(data.hardware);
      const pending = Array.isArray(data.pending) && data.pending.length > 0;
      host.innerHTML = `<div class="launch-head"><div><h4>下次启动的图像设置</h4><p>保存后从管理器启动时应用，或预览后立即应用。</p></div><span title="${esc(facts.allNames)}">${esc(facts.label)}</span><button class="button subtle" type="button" data-ls-action="reload"${busy ? ' disabled' : ''}>重新读取</button></div>${pending ? `<div class="launch-recovery" role="alert"><span>${esc(data.notice || '上次设置事务未完成，请先恢复。')}</span><button class="button" type="button" data-ls-action="recover">恢复未完成操作</button></div>` : data.notice ? `<p class="launch-message launch-error" role="alert">${esc(data.notice)}</p>` : ''}${data.legacy?.error ? `<p class="launch-message launch-error">旧 SR 模型记录读取失败：${esc(data.legacy.error.message)}。恢复记录已保留。</p>` : ''}
        <div class="launch-grid">${['sr', 'fg'].map(domain => {
          const fields = drafts[domain];
          const legacyOptiScaler = domain === 'sr' && hasLegacyOptiScaler(data);
          const available = domain === 'sr' ? !legacyOptiScaler : facts.fgBackend && fields.backend === facts.fgBackend && data.fgComponents?.ready === true;
          const canRestore = Boolean(data.applied?.[domain] || data.requests?.[domain] || domain === 'sr' && (data.legacy?.baselineCaptured || legacyState(data)));
          const message = messages[domain];
          return `<section class="launch-panel" data-ls-domain="${domain}" aria-label="${domain === 'sr' ? 'SR 超分辨率' : 'FG 帧生成'}"><div class="launch-panel-title"><h4>${domain === 'sr' ? 'SR 超分辨率' : 'FG 帧生成'}</h4><span>${domain === 'sr' ? '输入画质' : '显示帧率'}</span></div>
            ${domain === 'fg' ? fgComponentMarkup(data, busy || pending) : ''}
            <fieldset${busy || pending ? ' disabled' : ''}><legend class="sr-only">${domain.toUpperCase()} 设置</legend>${legacyOptiScaler ? '<p class="launch-unavailable">检测到旧 OptiScaler 设置。请先撤销旧设置，再设置原生 DLSS；恢复记录仍会保留。</p>' : domain === 'sr' ? srFields(fields) : data.fgComponents?.ready ? fgFields(fields, data.hardware) : ''}
            ${statusMarkup(domain, data, dirty[domain])}${message ? `<p class="launch-message${message.error ? ' launch-error' : ''}" role="${message.error ? 'alert' : 'status'}">${esc(message.text)}</p>` : ''}
            <div class="launch-actions">${canRestore || domain === 'sr' ? `<button class="button subtle" type="button" data-ls-action="restore" title="撤销本页覆盖并恢复原有配置，不会关闭游戏的 SR 或 FG"${canRestore ? '' : ' disabled'}>撤销本页设置</button>` : ''}${domain === 'sr' || data.fgComponents?.ready ? `<button class="button" type="button" data-ls-action="save"${available ? '' : ' disabled'}>保存到下次启动</button><button class="button primary" type="button" data-ls-action="preview"${available ? '' : ' disabled'}>预览并应用</button>` : ''}</div>
            ${previewMarkup(previews[domain], fields)}</fieldset></section>`;
        }).join('')}</div>`;
      const recover = host.querySelector('[data-ls-action="recover"]');
      if (recover) recover.disabled = busy;
    }
    async function refresh(resetDrafts = false) {
      const token = ++generation;
      const next = unwrap(await manager.inspectLaunchSettings(id));
      if (token !== generation || !attached()) return;
      data = next;
      for (const domain of ['sr', 'fg']) if (resetDrafts || !drafts[domain]) { drafts[domain] = initialFields(domain, data); dirty[domain] = false; }
      render();
    }
    async function perform(action, domain) {
      if (busy) return;
      busy = true;
      let succeeded = false;
      try {
        if (domain) delete messages[domain];
        if (action === 'runtime-help') {
          render();
          if (unwrap(await manager.openExternal('vcRuntimeUrl')) !== true) throw new Error('无法打开微软下载，请稍后重试。');
          setMessage('fg', '已打开微软运行库下载；安装或修复完成后点击“重新检查运行库”。');
        } else if (action === 'runtime-check') {
          render();
        } else if (action === 'prepare') {
          render();
          let response = await manager.prepareFgComponents(id, { allowAntiCheat: false });
          if (response?.ok === false && response.error?.code === 'ERR_ANTI_CHEAT_CONFIRM') {
            if (typeof scope.confirmAntiCheat !== 'function' || !await scope.confirmAntiCheat(id)) throw new Error('已取消组件安装。');
            response = await manager.prepareFgComponents(id, { allowAntiCheat: true });
          }
          unwrap(response);
          delete previews.fg;
          setMessage('fg', '兼容组件准备完成；请在游戏内开启帧生成。');
        } else if (action === 'native-route' || action === 'remove-components') {
          render();
          unwrap(await manager.restoreFgComponents(id));
          delete previews.fg;
          setMessage('fg', action === 'native-route' ? '已切换为原生帧生成路线。' : '已移除本工具安装的兼容组件。');
        } else if (action === 'recover') {
          render();
          unwrap(await manager.recoverLaunchSettings(id));
          for (const key of ['sr', 'fg']) { delete previews[key]; setMessage(key, '未完成操作已恢复；游戏未验证。'); }
        } else if (action === 'restore') {
          render();
          unwrap(await manager.restoreLaunchSettings(id, domain));
          delete previews[domain];
          setMessage(domain, '已撤销本页设置，并取消下次启动覆盖。恢复原有配置不等于关闭功能。');
        } else if (action === 'confirm') {
          const preview = previews[domain];
          if (!preview) throw new Error('请先重新预览设置。');
          // Never recreate a plan or apply changed fields behind the user's confirmation.
          if (JSON.stringify(createRequest(domain, drafts[domain])) !== preview.signature) throw new Error('设置已改变，请重新预览。');
          const planId = preview.plan.id || preview.plan.planId;
          if (!planId) throw new Error('后端未返回可确认的应用计划。');
          delete previews[domain];
          render();
          const result = unwrap(await manager.applyLaunchSettings(planId, { confirm: true }));
          setMessage(domain, result.noOp ? '当前配置无需改写；游戏未验证。' : '应用完成，配置写后已校验；游戏未验证。');
        } else {
          if (domain === 'sr' && hasLegacyOptiScaler(data)) throw new Error('请先撤销旧 OptiScaler 设置，再设置原生 DLSS。');
          const request = createRequest(domain, drafts[domain]);
          render();
          if (action === 'save') {
            const result = unwrap(await manager.saveLaunchSettings(id, domain, request));
            if (result?.saved !== true) throw new Error('后端未确认保存成功。');
            dirty[domain] = false;
            setMessage(domain, '请求已保存，下次从管理器启动游戏时应用。');
          } else if (action === 'preview') {
            const plan = unwrap(await manager.previewLaunchSettings(id, domain, request));
            if (!plan?.id && !plan?.planId) throw new Error('后端未返回可确认的应用计划。');
            previews[domain] = { plan, domain, signature: JSON.stringify(request) };
          }
        }
        succeeded = true;
      } catch (error) {
        const text = `${error.message}${error.code ? ` [${error.code}]` : ''}`;
        setMessage(domain || 'sr', text, true);
      }
      if (action !== 'preview') {
        try { await refresh(false); }
        catch (error) { setMessage(domain || 'sr', `${succeeded ? '操作已完成；' : ''}状态读取失败：${error.message}。请重新读取。`, true); }
        if (succeeded && action === 'restore' && data) { drafts[domain] = initialFields(domain, data); dirty[domain] = false; }
        if (succeeded && ['native-route', 'remove-components'].includes(action) && data) { drafts.fg = initialFields('fg', data); dirty.fg = false; }
        if (succeeded && action === 'prepare' && data?.fgComponents?.ready !== true) setMessage('fg', '组件检查尚未通过，请处理上方提示后重试。', true);
        if (succeeded && action === 'runtime-check') setMessage('fg', data?.fgComponents?.runtime?.ready === true
          ? '基础运行库文件检查通过；游戏内加载与帧生成仍需验证。' : '运行库检查尚未通过，请处理上方提示。', data?.fgComponents?.runtime?.ready !== true);
      }
      busy = false;
      render();
    }
    host.onchange = event => {
      const field = event.target?.dataset?.lsField;
      const domain = event.target?.closest('[data-ls-domain]')?.dataset.lsDomain;
      if (!field || !domain || busy) return;
      drafts[domain][field] = event.target.type === 'checkbox' ? event.target.checked : event.target.value;
      if (domain === 'sr' && field === 'quality' && Object.hasOwn(PRESET_PERCENT, drafts.sr.quality)) drafts.sr.renderPercent = PRESET_PERCENT[drafts.sr.quality];
      if (domain === 'sr' && field === 'renderPercent') drafts.sr.quality = 'custom';
      dirty[domain] = true;
      delete previews[domain]; delete messages[domain];
      if (event.target.type === 'number') return;
      render();
    };
    host.oninput = event => {
      // Invalidate a preview as soon as a number changes, before focus leaves it.
      const field = event.target?.dataset?.lsField;
      const domain = event.target?.closest('[data-ls-domain]')?.dataset.lsDomain;
      if (!field || !domain || busy || event.target.type !== 'number') return;
      drafts[domain][field] = event.target.value;
      if (domain === 'sr' && field === 'renderPercent') {
        drafts.sr.quality = 'custom';
        event.target.closest('[data-ls-domain]').querySelector('[data-ls-field="quality"]').value = 'custom';
      }
      dirty[domain] = true;
      delete previews[domain];
      const panel = event.target.closest('[data-ls-domain]');
      panel.querySelector('.launch-preview')?.remove();
      const badge = panel.querySelector('.launch-status .badge');
      if (badge) { badge.textContent = '尚未保存'; badge.className = 'badge warn'; }
    };
    host.onclick = event => {
      const button = event.target?.closest('[data-ls-action]');
      if (!button || button.disabled || busy) return;
      event.stopPropagation();
      const action = button.dataset.lsAction;
      const domain = button.closest('[data-ls-domain]')?.dataset.lsDomain;
      if (action === 'cancel') { delete previews[domain]; render(); return; }
      if (action === 'reload') { controller.ready = initialize(); return; }
      void perform(action, domain);
    };
    async function initialize() {
      try {
        if (!manager || typeof manager.inspectLaunchSettings !== 'function') throw new Error('当前程序未提供启动设置接口。');
        await refresh();
      } catch (error) {
        if (attached()) host.innerHTML = `<div class="launch-recovery" role="alert"><span>${esc(error.message)}</span><button class="button" data-ls-action="reload" type="button">重新读取</button></div>`;
      }
    }
    const controller = { refresh, perform, getState: () => ({ data, drafts, dirty, previews, busy }), ready: null };
    host.launchSettingsController = controller;
    controller.ready = initialize();
    return controller;
  }

  const api = { mount, createRequest, hardwareFacts, initialFields, requestLabel, statusMarkup, previewMarkup, fgComponentMarkup };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else scope.launchSettingsUi = api;
})(typeof window === 'object' ? window : globalThis);
