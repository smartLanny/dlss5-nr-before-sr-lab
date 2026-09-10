'use strict';

(function (scope) {
  const esc = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]);
  const unwrap = value => { if (value?.ok !== true) throw new Error(value?.error?.message || '本次操作未完成。'); return value.value; };
  const waiting = phase => ['waiting-helper', 'waiting-launcher', 'waiting-game', 'running'].includes(phase);
  const PHASES = {
    binding: ['绑定启动器', '选择这个客户端对应的官方启动器。'], api: ['确认游戏 API', '按游戏实际使用的图形 API 选择一次。'],
    install: ['准备独立画面增强', '先预览 ReShade、Core 和 NR 配套，再应用安装。'], ready: ['已准备，可以启动', '助手就绪后会打开已绑定的官方启动器。'],
    'waiting-helper': ['正在准备加载助手', '请稍候，助手就绪后继续。'], 'waiting-launcher': ['在官方启动器中启动游戏', '已绑定当前客户端，正在等待它产生游戏进程。'],
    'waiting-game': ['等待游戏启动', '请在已打开的官方启动器中点击启动游戏。'], running: ['游戏已启动', '加载与 NR 状态分别核对；请在游戏中查看实际画面。'],
    failed: ['本次操作未完成', '查看当前原因后重新检查。'], recovery: ['先恢复未完成操作', '恢复记录已保留，完成恢复后再继续。']
  };
  const verificationLabel = row => row?.state === 'passed' ? '已确认' : ['failed', 'blocked'].includes(row?.state) ? '未通过' : '待确认';
  const channelValue = row => typeof row === 'string' ? row : row.channel;
  const channelLabel = row => typeof row === 'string' ? row : row.channelLabel || row.label || row.channel;

  function mount(host, api, options = {}) {
    let games = [], launchers = [], warnings = [], selectedId = null, flow = null, form = {}, busy = false, error = '', plan = null,
      discovering = false, disposed = false, generation = 0, timer = null, active = false, editorOpen = false, editingApi = false, currentWork = '', previewAction = 'install';
    const editors = new Map();
    const button = (action, label, primary = false, disabled = false) => `<button type="button" class="button ${primary ? 'primary' : 'subtle'}" data-hoyo-action="${action}"${disabled ? ' disabled' : ''}>${label}</button>`;
    const select = (field, label, rows, selected, placeholder) => `<label class="gp-field"><span>${label}</span><select data-hoyo-field="${field}"${busy ? ' disabled' : ''}>${placeholder ? `<option value="">${placeholder}</option>` : ''}${rows.map(row => `<option value="${esc(row.value)}"${String(selected ?? '') === String(row.value) ? ' selected' : ''}>${esc(row.label)}</option>`).join('')}</select></label>`;
    function stopPoll() { if (timer) clearTimeout(timer); timer = null; }
    function schedule() {
      stopPoll();
      if (active && !disposed && !busy && !editorOpen && !editingApi && flow && waiting(flow.phase) && !error)
        timer = setTimeout(() => { void inspect(true); }, 2000);
    }
    function accept(value) {
      if (!value || !value.id || selectedId && value.id !== selectedId) throw new Error('返回的客户端与当前选择不一致。');
      flow = value; selectedId = value.id; const at = games.findIndex(row => row.id === value.id);
      if (at < 0) games.push(value); else games[at] = value;
      form = {}; options.onChanged?.(value); schedule();
    }
    function currentAction() {
      if (busy) return button('working', currentWork, true, true);
      if (!flow) return '';
      const disabled = busy || flow.busy;
      if (editingApi) return button('confirm-api', '确认图形 API', true, disabled || !['dx11', 'dx12'].includes(form.api) || form.api === flow.api?.api) + button('cancel-api', '取消修改', false, disabled);
      if (flow.nextAction === 'recover' || flow.phase === 'recovery') return button('recover', '恢复未完成操作', true, disabled);
      if (error || flow.error || flow.installation?.error || flow.phase === 'failed') return button('inspect', '重新检查', true, disabled);
      if (flow.nextAction === 'bind') return button('bind', '确认绑定', true, disabled || !((form.launcherId || flow.binding?.launcher?.id) && (!(flow.binding?.channels?.length > 1) || form.channel || flow.channel)));
      if (flow.nextAction === 'select-api') return button('bind', '确认图形 API', true, disabled || !['dx11', 'dx12'].includes(form.api));
      if (flow.nextAction === 'preview-install') return button('preview-install', flow.installation?.installed ? '预览更新配套' : '预览安装', true, disabled);
      if (flow.nextAction === 'start') return button('start', '启动游戏', true, disabled || !flow.installation?.ready);
      if (flow.nextAction === 'recover') return button('recover', '恢复未完成操作', true, disabled);
      if (flow.nextAction === 'wait' || waiting(flow.phase)) return button('cancel', '取消等待', false, disabled);
      return button('inspect', '重新检查', true, disabled);
    }
    function bindingFields() {
      if (!flow || !editingApi && !['binding', 'api'].includes(flow.phase)) return '';
      const bindingMode = !editingApi && flow.phase === 'binding';
      const binding = flow.binding || {}, channels = binding.channels || [], available = binding.launchers?.length ? binding.launchers : launchers;
      const rows = available.map(row => ({ value: row.id, label: `${row.kind === 'starward' ? 'Starward' : 'HoYoPlay'} · ${row.path}` }));
      return `<div class="hoyo-binding gp-controls">${bindingMode ? select('launcherId', '对应启动器', rows, form.launcherId || binding.launcher?.id, '选择启动器') +
        (channels.length > 1 ? select('channel', '游戏客户端', channels.map(row => ({ value: channelValue(row), label: channelLabel(row) })), form.channel || flow.channel, '选择客户端') : '') :
        select('api', '游戏图形 API', [{ value: 'dx11', label: 'DirectX 11' }, { value: 'dx12', label: 'DirectX 12' }], form.api, '按游戏实际设置选择')}
        ${bindingMode ? button('pick-launcher', '选择启动器文件', false, busy) : ''}</div>`;
    }
    function evidence() {
      if (!flow || !flow.installation?.installed && !flow.session) return '';
      return `<section class="hoyo-evidence" aria-label="独立运行核验"><h4>${flow.session && !flow.session.historical ? '本次运行' : '最近运行记录'}</h4><div>${[['reshade', 'ReShade'], ['core', 'Core'], ['nr', 'NR 处理']].map(([key, label]) => {
        const row = flow.verification?.[key], text = verificationLabel(row);
        return `<article><span>${label}</span><strong class="${row?.state === 'passed' ? 'confirmed' : ''}">${text}</strong>${row?.detail ? `<small>${esc(row.detail)}</small>` : ''}</article>`;
      }).join('')}</div><p>这些状态独立确认；画面效果请在游戏内对照。</p></section>`;
    }
    function render() {
      if (disposed) return;
      const phase = editingApi ? 'api' : flow?.phase === 'recovery' ? 'recovery' : error || flow?.error || flow?.installation?.error ? 'failed' : flow?.phase, phaseText = PHASES[phase] || ['选择一个客户端', '选择后只显示当前需要完成的一步。'];
      host.innerHTML = `<div class="hoyo-workspace"><aside class="hoyo-clients"><div class="hoyo-list-head"><h3>本机客户端</h3>${button('discover', discovering ? '正在发现…' : '重新发现', false, busy)}</div>
        <div role="list" aria-label="米哈游客户端">${games.map(row => `<button type="button" class="hoyo-client${row.id === selectedId ? ' selected' : ''}" data-hoyo-client="${esc(row.id)}"${busy ? ' disabled' : ''} aria-pressed="${row.id === selectedId}"><strong>${esc(row.name)}</strong><small>${esc(row.channelLabel || '客户端待确认')}${row.gameVersion ? ` · ${esc(row.gameVersion)}` : ''}</small><span>${esc(busy && row.id === selectedId ? currentWork : PHASES[row.error ? 'failed' : row.phase]?.[0] || '待检查')}</span></button>`).join('') || '<p class="hoyo-empty">尚未发现正式客户端。可以选择游戏程序添加。</p>'}</div>
        ${button('pick-game', '选择游戏程序', !games.length && !busy, busy)}${warnings.length ? `<details class="hoyo-discovery-notes"><summary>发现提示 ${warnings.length}</summary>${warnings.map(row => `<p>${esc(row.message || row)}</p>`).join('')}</details>` : ''}</aside>
        <section class="hoyo-current" aria-label="当前客户端操作">${flow ? `<div class="hoyo-client-heading"><span class="hoyo-eyebrow">当前客户端</span><h3>${esc(flow.name)}</h3><p>${esc(flow.channelLabel || '')}${flow.gameVersion ? ` · ${esc(flow.gameVersion)}` : ''}</p></div>` : ''}
          <div class="hoyo-next-step"><span class="hoyo-eyebrow">现在只需这一步</span><h3>${esc(busy ? currentWork : discovering && !flow ? '正在发现本机客户端' : phaseText[0])}</h3><p>${esc(busy ? '本次操作正在执行，完成后会显示结果。' : error || flow?.error?.message || flow?.installation?.error?.message || (editingApi ? '此处仅选择配套路线，不会修改游戏中的图形设置。确认后先预览应用，再启动。' : phaseText[1]))}</p>${bindingFields()}<div class="hoyo-primary-actions">${currentAction()}</div></div>
          ${evidence()}${flow ? `<details class="hoyo-details"><summary>启动器、配置与维护</summary><dl><dt>游戏程序</dt><dd>${esc(flow.exePath)}</dd><dt>启动器</dt><dd>${esc(flow.binding?.launcher?.path || '尚未绑定')}</dd><dt>图形 API</dt><dd>${esc(flow.api?.api || '待确认')}</dd></dl>
            <p>此客户端使用独立 ReShade、Core 和 NR 配置。新安装默认按 Home 打开菜单，可自定义。</p><div class="hoyo-secondary-actions">${button('inspect', '重新检查', false, busy || editingApi)}${flow.installation?.installed ? button('settings', '画面与快捷键设置', false, busy || editingApi || waiting(flow.phase)) + button('preview-repair', '预览修复', false, busy || editingApi || waiting(flow.phase)) + button('preview-restore', '预览卸载与恢复', false, busy || editingApi || waiting(flow.phase)) : ''}${button('edit-api', '修改图形 API', false, busy || editingApi || waiting(flow.phase) || !['install', 'ready'].includes(flow.phase))}${button('pick-launcher', '重新选择启动器', false, busy || editingApi || waiting(flow.phase))}</div></details>` : ''}</section></div>`;
      host.setAttribute('aria-busy', String(busy));
      if (plan) renderPlan();
      if (editorOpen) { const editor = editors.get(flow?.gameId); if (editor) host.append(editor.element); }
    }
    function closeSettings() {
      const editor = editors.get(flow?.gameId); if (editor?.controller.getState().busy) return;
      editorOpen = false; editor?.element.remove(); schedule();
      host.querySelector('[data-hoyo-action="settings"]')?.focus();
    }
    function openSettings() {
      if (!flow?.gameId || !flow.installation?.installed || waiting(flow.phase)) return;
      stopPoll(); let editor = editors.get(flow.gameId);
      if (!editor) {
        const element = document.createElement('div'); element.className = 'gp-modal hoyo-settings-modal';
        element.setAttribute('role', 'dialog'); element.setAttribute('aria-modal', 'true'); element.setAttribute('aria-label', `${flow.name}画面与快捷键`);
        element.innerHTML = `<div class="gp-modal-card hoyo-settings-card"><h3>${esc(flow.name)} · 画面与快捷键</h3><div class="gp-inline hoyo-settings-host"></div></div>`;
        const settings = element.querySelector('.hoyo-settings-host'); host.append(element);
        const controller = scope.GamePageUi.mount(settings, api, { hoyoSettingsOnly: true, onBack: closeSettings,
          onChanged: async () => { try { const value = unwrap(await api.hoyoInspect(selectedId)); accept(value); } catch (failure) { error = failure.message; } } });
        editor = { element, controller }; editors.set(flow.gameId, editor);
        void controller.open(flow.gameId, 'overview', { game: { id: flow.gameId, name: flow.name, installed: true, chosen: { path: flow.exePath, apiResolution: { api: flow.api?.api } } } });
      } else { host.append(editor.element); void editor.controller.refresh(true); }
      editorOpen = true; editor.element.querySelector('button')?.focus();
    }
    function renderPlan() {
      host.querySelector('.gp-modal')?.remove();
      const consent = plan.requiresAntiCheat === true || plan.deployment?.requiresAntiCheat === true;
      host.insertAdjacentHTML('beforeend', `<div class="gp-modal" role="dialog" aria-modal="true" aria-label="米哈游操作预览"><div class="gp-modal-card"><h3>确认本次变更</h3><p>${esc(flow?.name)} · 核对后一次应用。</p><div class="gp-change-list">${(plan.changes || []).map(row => `<div><strong>${esc(row.name || row.key || row.role || row.action)}</strong><span>${esc(({ create: '新增', replace: '替换', remove: '移除', keep: '保留' })[row.action] || row.description || row.action)}</span>${row.path ? `<small>${esc(row.path)}</small>` : ''}</div>`).join('')}</div>${(plan.blockers || []).map(row => `<p class="gp-message error">${esc(row.message || row)}</p>`).join('')}${plan.requiresElevation ? '<p>应用时会显示 Windows 权限确认，完成后管理器继续以普通权限运行。</p>' : ''}${consent ? '<label class="check-line gp-check"><input type="checkbox" data-hoyo-consent>我已了解反作弊可能阻止加载及账号风险，并决定应用。</label>' : ''}<p class="hoyo-plan-message gp-message" role="status" hidden></p><div class="gp-modal-actions">${button('close-plan', '取消', false, busy)}${button('apply', '应用本次变更', true, busy || Boolean(plan.blockers?.length))}</div></div></div>`);
      host.querySelector('[data-hoyo-action="close-plan"]')?.focus();
    }
    async function run(work, applyValue = true, label = '正在检查…') {
      if (busy || disposed) return; busy = true; currentWork = label; error = ''; stopPoll(); render(); const token = ++generation;
      try { const result = unwrap(await work()); if (disposed || generation !== token) return; if (applyValue) accept(result); return result; }
      catch (failure) { if (!disposed && generation === token) error = failure.message; }
      finally { if (!disposed && generation === token) { busy = false; currentWork = ''; render(); schedule(); } }
    }
    async function discover() {
      discovering = true;
      await run(async () => { const result = await api.hoyoDiscover(); if (result?.ok === true) {
        const value = result.value; games = value.games || []; launchers = value.launchers || []; warnings = value.warnings || [];
        selectedId = games.some(row => row.id === selectedId) ? selectedId : games[0]?.id || null; flow = games.find(row => row.id === selectedId) || null; form = {};
        for (const game of games) options.onChanged?.(game);
      } return result; }, false, '正在发现本机客户端…'); discovering = false; render(); schedule();
    }
    async function inspect(passive = false) {
      if (!flow || busy) return;
      if (!passive) return run(() => api.hoyoInspect(selectedId, { retry: true }));
      const token = generation, requestedId = selectedId;
      try { const value = unwrap(await api.hoyoInspect(requestedId)); if (!disposed && active && generation === token && selectedId === requestedId) { accept(value); render(); } }
      catch (failure) { if (!disposed && generation === token && selectedId === requestedId) { error = failure.message; render(); } }
      finally { schedule(); }
    }
    host.addEventListener('change', event => { const field = event.target.dataset.hoyoField; if (!field || busy) return; form[field] = event.target.value; const region = host.querySelector('.hoyo-primary-actions'); if (region) region.innerHTML = currentAction(); });
    host.addEventListener('click', async event => {
      const client = event.target.closest('[data-hoyo-client]');
      if (client && !busy) { generation++; stopPoll(); selectedId = client.dataset.hoyoClient; flow = games.find(row => row.id === selectedId); form = {}; editingApi = false; error = ''; plan = null; render(); await inspect(); return; }
      const item = event.target.closest('[data-hoyo-action]'); if (!item || item.disabled || busy) return;
      const action = item.dataset.hoyoAction;
      if (action === 'edit-api') { editingApi = true; form = { api: flow.api?.api || '' }; error = ''; stopPoll(); render(); host.querySelector('[data-hoyo-field="api"]')?.focus(); return; }
      if (action === 'cancel-api') { editingApi = false; form = {}; error = ''; render(); schedule(); return; }
      if (action === 'confirm-api') {
        const selected = form.api; if (!editingApi || !['dx11', 'dx12'].includes(selected)) return;
        const result = await run(() => api.hoyoBind(selectedId, { api: selected }), true, '正在保存图形 API…');
        if (result) { editingApi = false; form = {}; render(); schedule(); } return;
      }
      if (editingApi && ['start', 'bind', 'preview-install', 'preview-repair', 'preview-restore', 'settings'].includes(action)) return;
      if (action === 'discover') return discover();
      if (action === 'inspect') return inspect();
      if (action === 'pick-game') { const result = await run(() => api.hoyoPickGame(), false, '正在选择游戏程序…'); if (result && !result.cancelled) { if (result.discovery?.id) { selectedId = result.discovery.id; accept(result.discovery); } await discover(); } return; }
      if (action === 'pick-launcher') return run(() => api.hoyoPickLauncher(selectedId), true, '正在选择启动器…');
      if (action === 'bind') return run(() => api.hoyoBind(selectedId, { ...form }), true, '正在保存客户端绑定…');
      if (action.startsWith('preview-')) { const requested = action.slice(8), result = await run(() => api.hoyoPreview(selectedId, requested), false, '正在准备操作预览…'); if (result) { plan = result; previewAction = requested; renderPlan(); } return; }
      if (action === 'close-plan') { plan = null; host.querySelector('.gp-modal')?.remove(); return; }
      if (action === 'apply') {
        const saved = plan; if (!saved) return; const allowAntiCheat = host.querySelector('[data-hoyo-consent]')?.checked === true;
        if ((saved.requiresAntiCheat === true || saved.deployment?.requiresAntiCheat === true) && !allowAntiCheat) {
          const message = host.querySelector('.hoyo-plan-message'); message.textContent = '请先确认已了解本次游戏的反作弊提示。'; message.hidden = false; return;
        }
        const label = previewAction === 'restore' ? '正在卸载并恢复…' : previewAction === 'repair' ? '正在修复配套…' : '正在应用安装…';
        plan = null; return run(() => api.hoyoApply(selectedId, saved.planId, { confirm: true, fingerprint: saved.fingerprint, allowAntiCheat }), true, label);
      }
      if (action === 'start') return run(() => api.hoyoStart(selectedId), true, '正在准备启动…');
      if (action === 'cancel') return run(() => api.hoyoCancel(selectedId), true, '正在取消等待…');
      if (action === 'recover') return run(() => api.hoyoRecover(selectedId), true, '正在恢复未完成操作…');
      if (action === 'settings') openSettings();
    });
    host.addEventListener('keydown', event => {
      if (editingApi && event.key === 'Escape' && !busy) { editingApi = false; form = {}; error = ''; render(); schedule(); return; }
      if (!plan && editorOpen) {
        const editor = editors.get(flow?.gameId), nested = event.target.closest('.gp-modal');
        if (nested !== editor?.element) return;
        if (event.key === 'Escape') closeSettings();
        if (event.key === 'Tab') {
          const fields = [...editor.element.querySelectorAll('button:not([disabled]),input:not([disabled]),select:not([disabled]),summary')].filter(row => row.getClientRects().length), first = fields[0], last = fields.at(-1);
          if (event.shiftKey && event.target === first) { event.preventDefault(); last?.focus(); } else if (!event.shiftKey && event.target === last) { event.preventDefault(); first?.focus(); }
        }
        return;
      }
      if (!plan) return;
      if (event.key === 'Escape' && !busy) { plan = null; host.querySelector('.gp-modal')?.remove(); }
      if (event.key === 'Tab') {
        const buttons = [...host.querySelectorAll('.gp-modal button:not([disabled]),.gp-modal input:not([disabled])')], first = buttons[0], last = buttons.at(-1);
        if (event.shiftKey && event.target === first) { event.preventDefault(); last?.focus(); } else if (!event.shiftKey && event.target === last) { event.preventDefault(); first?.focus(); }
      }
    });
    render();
    return { activate() { active = true; if (!games.length) return discover(); render(); schedule(); }, deactivate() { active = false; stopPoll(); },
      refresh: discover, getState: () => ({ selectedId, flow, games, busy, error, plan, editingApi, currentWork }), dispose() { disposed = true; generation++; stopPoll(); for (const editor of editors.values()) editor.controller.dispose(); } };
  }
  const api = { mount, verificationLabel };
  if (typeof module === 'object' && module.exports) module.exports = api; else scope.HoYoPageUi = api;
})(typeof window === 'object' ? window : globalThis);
