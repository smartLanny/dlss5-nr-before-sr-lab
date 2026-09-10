'use strict';
// Compose the existing GamePageUi for normal games, HoYo settings and maintenance.
// No replacement router, independent installer, periodic scan or remote request.
(function (scope) {
  const original = scope.GamePageUi;
  if (!original?.mount || !scope.ManagerCompatibilityUX?.mount) return;
  const unwrap = result => { if (result?.ok !== true) throw Object.assign(new Error(result?.error?.message || '反馈操作未完成。'), result?.error); return result.value; };
  function description(state) {
    const data = state.data || {}, native = data.launch?.session, verification = data.verification;
    const historical = native?.historical === true;
    if (data.operation?.pending || data.deployment?.needsRecovery) return '先恢复未完成的操作；仍可保存反馈供排查。';
    if (historical) return '上次运行记录已保留，本次是否生效仍待确认。';
    if (verification?.nr?.status === 'passed') return '已观察到 NR 处理；画面效果和流畅度请你确认。';
    if (verification?.nr?.status === 'bypassed') return '插件已加载，但这次没有正常处理。可在反馈中附带原因。';
    if (verification?.core?.status === 'passed') return '插件已加载，是否处理画面仍待确认。';
    return data.game?.installed ? '文件已安装；是否在游戏里生效，需要运行证据和你的体验。' : '打不开游戏或没有效果，也可以直接反馈。';
  }
  // Reuse existing controls, attributes and event delegation; never rewrite values or backend requests.
  function simplifyControls(host) {
    const rename = (selector, label, help) => {
      for (const control of host.querySelectorAll(selector)) {
        const field = control.closest('label.gp-field');
        const caption = field?.querySelector(':scope > span');
        if (caption && caption.textContent !== label) caption.textContent = label;
        if (field && help && field.title !== help) field.title = help;
      }
    };
    rename('[data-gp-group="route"][data-gp-field="api"]', '图形接口', '默认自动识别。只有识别不确定或需要测试其他接口时才手动选择。');
    rename('[data-gp-group="route"][data-gp-field="version"]', '增强版本', '保留当前版本；切换前仍会预览需要修改的文件。');
    rename('[data-gp-group="nr"][data-gp-field="Intensity"]', 'AI 效果强度', '调整传给模型的强度；不改变超分档位或补帧倍率。');
    rename('[data-gp-group="nr"][data-gp-field="WorkMode"]', 'AI 处理分辨率', '只控制增强处理尺寸，不等于降低游戏本身的渲染分辨率。');
    rename('[data-gp-group="nr"][data-gp-field="CustomWorkScale"]', '自定义 AI 比例', '仅在自定义模式下生效；保留当前接口支持的范围。');
    rename('[data-gp-group="nr"][data-gp-field="TransferStrength"]', '超分前效果强度', '仅用于前置处理；不是模型强度。');
    rename('[data-gp-group="nr"][data-gp-field="PostTransferStrength"]', '超分后效果强度', '仅用于后置处理；不是模型强度。');
    rename('[data-gp-group="nr"][data-gp-field="SkinStructureStrength"]', '人脸细节强度', '沿用当前 Core 的人脸参数；不承诺完整保留原脸。');
    const primary = host.querySelector('.gp-nr-primary');
    const details = host.querySelector('.gp-nr-details[data-gp-detail="nr"]');
    const extra = details?.querySelector('.gp-controls');
    if (!primary || !extra || primary.dataset.cxSimplified) return;
    primary.dataset.cxSimplified = 'true';
    const focused = host.contains(document.activeElement) ? document.activeElement : null;
    // Style and strength are everyday controls. Detailed contrast, reconstruction and performance remain reachable.
    for (const key of ['LocalToneStrength', 'LocalStructureStrength']) {
      const field = primary.querySelector(`[data-gp-field="${key}"]`)?.closest('label.gp-field');
      if (field) extra.append(field);
    }
    const style = extra.querySelector('[data-gp-field="Style"]')?.closest('label.gp-field');
    if (style) primary.prepend(style);
    const summary = details.querySelector(':scope > summary');
    if (summary) summary.textContent = '细节、性能与更多设置';
    if (focused?.isConnected) { if (details.contains(focused)) details.open = true; focused.focus({ preventScroll: true }); }
  }
  function mount(host, manager, options = {}) {
    if (!manager.openCompatibilityFeedback) return original.mount(host, manager, options);
    // Preserve the original DOM depth and CSS selectors. The dialog lives outside the rerendered game card.
    const footer = document.createElement('div'); footer.className = 'cx-feedback-footer';
    const controller = original.mount(host, manager, options);
    let disposed = false, token = null, widget = null, lastKey = null, lastView = '', generation = 0, opening = null;
    const closeToken = () => { const old = token; token = null; if (old) Promise.resolve(manager.closeCompatibilityFeedback(old)).catch(() => {}); };
    async function context() {
      const state = controller.getState();
      if (!state.id) throw new Error('请先选择一个游戏。');
      if (state.busy || state.launching) throw new Error('请先等待当前操作完成。');
      if (token) return token;
      if (opening) return opening;
      const mine = generation, id = state.id;
      opening = Promise.resolve(manager.openCompatibilityFeedback(id)).then(unwrap).then(value => {
        if (disposed || mine !== generation || controller.getState().id !== id) {
          void manager.closeCompatibilityFeedback(value.token); throw new Error('已切换游戏，请重新打开反馈。');
        }
        token = value.token; return token;
      }).finally(() => { opening = null; });
      return opening;
    }
    function update() {
      if (disposed) return;
      const state = controller.getState(), data = state.data;
      simplifyControls(host);
      footer.hidden = !state.id || !data;
      if (footer.hidden) { if (lastKey) { generation++; closeToken(); lastKey = null; widget?.closeFeedback(); } return; }
      if (footer.parentNode !== host) host.append(footer);
      const key = `${state.id}:${data.layout?.bindingId || ''}:${data.layout?.version || data.game?.addonVersion || ''}`;
      if (key !== lastKey) { generation++; closeToken(); lastKey = key; }
      const view = { contextKey: key, gameName: data.game?.name || '当前游戏', title: '这次体验怎么样？', subtitle: '兼容反馈', status: description(state), reasons: [], warnings: [],
        verification: '', layers: [], alternatives: [], feedbackDisabled: state.busy || state.launching };
      const value = JSON.stringify(view);
      if (value === lastView) return;
      lastView = value;
      if (widget) widget.update(view);
      else widget = scope.ManagerCompatibilityUX.mount(footer, { decision: view, compact: true, dialogHost: document.body, bridge: {
        preview: async request => {
          // Every new preview gets a fresh main-process context; a failed/expired preview never pins the user to an old token.
          closeToken(); const active = await context();
          return unwrap(await manager.previewCompatibilityFeedback(active, request));
        },
        save: async request => {
          if (!token) throw new Error('反馈预览已失效，请返回修改后重新预览。');
          return unwrap(await manager.saveCompatibilityFeedback(token, request));
        },
        discard: async previewId => { if (token) await manager.discardCompatibilityFeedback(token, previewId); }
      } });
    }
    // Ignore our own footer mutations and reattachment. No polling or background scan.
    const observer = new MutationObserver(records => {
      if (records.some(row => !footer.contains(row.target) && !(row.target === host &&
          [...row.addedNodes, ...row.removedNodes].every(node => node === footer)))) update();
    });
    observer.observe(host, { childList: true, subtree: true });
    // A library/client switch removes the old card/editor from the stable
    // workspace while keeping its controller cached for reuse. Close the
    // feedback context at that boundary, but keep the existing GamePageUi
    // controller alive so renderer.js can reattach it later.
    let detached = false;
    const lifecycleRoot = host.closest('#gameList,#hoyoWorkspace,#repairMaintenance') || document.body;
    const lifecycleObserver = new MutationObserver(() => {
      if (!host.isConnected) {
        if (detached) return;
        detached = true; generation++; closeToken(); widget?.closeFeedback();
      } else if (detached) {
        detached = false; update();
      }
    });
    lifecycleObserver.observe(lifecycleRoot, { childList: true, subtree: true });
    const wrapped = { ...controller };
    for (const name of ['open','resume','refresh','selectTab']) if (typeof controller[name] === 'function') {
      wrapped[name] = (...args) => {
        const result = controller[name](...args); update();
        if (result?.then) void result.then(update, update);
        return result;
      };
    }
    wrapped.dispose = () => { disposed = true; generation++; observer.disconnect(); lifecycleObserver.disconnect(); closeToken(); widget?.destroy(); controller.dispose(); footer.remove(); };
    return wrapped;
  }
  scope.GamePageUi = { ...original, mount };
  scope.CompatibilityGamePage = Object.freeze({ description, simplifyControls });
})(typeof window === 'object' ? window : globalThis);
