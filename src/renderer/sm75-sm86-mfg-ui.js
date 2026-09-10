'use strict';
// Component-only UI; the existing Manager owns IPC, anti-cheat confirmation,
// refresh, error display and the file-journal recovery action.
(function (scope) {
  const BACKEND = 'dlssg-native-sm75-sm86';
  const esc = value => String(value ?? '').replace(/[&<>"']/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
  function components(info, busy = false) {
    if (info?.backend !== BACKEND) return '';
    const blockers = Array.isArray(info.blockers) ? info.blockers : [];
    const cap = info.settings?.maxGeneratedFrames ?? 3;
    const canPrepare = info.canPrepare === true || (info.ready === true && blockers.length === 0);
    return `<div class="launch-components" data-sm-mfg>
      <strong>RTX 20 / 30 · 外部 MFG（实验）</strong>
      <p>${info.ready ? '组件和配置已准备；游戏内效果尚未验证。' : info.managed ? '已有恢复记录；请处理下方提示或移除本工具组件。' : '可选组件，默认不安装。仅适用于已有 DLSS FG 的 D3D12 游戏。'}</p>
      <p class="config-note">${esc(info.router || '路由待确认')} · ${esc(info.proxy || '代理待确认')}。不改变 NR / SR，也不向任意游戏新增帧生成。</p>
      ${blockers.length ? `<p class="launch-error" role="alert">${blockers.map(esc).join('<br>')}</p><p class="config-note">请提交管理器反馈与 dlssg_sm86/logs 日志；NR 问题另附 F8。</p>` : ''}
      <label class="launch-field"><span>生成帧上限（不是当前倍率）</span><select data-sm-mfg-cap aria-label="RTX20/30 MFG 生成帧上限"${busy || !canPrepare ? ' disabled' : ''}>${[1, 2, 3].map(n => `<option value="${n}"${cap === n ? ' selected' : ''}>最高 ${n + 1}× · 1 帧渲染 + 最多 ${n} 帧生成</option>`).join('')}</select></label>
      <p class="config-note">实际倍率由游戏请求。先退出游戏，点击应用，再重启游戏并开启 DLSS 帧生成；不会实时切换倍率。物理 RTX20 和本游戏组合仍需验证。</p>
      ${canPrepare ? `<button class="button primary" type="button" data-ls-action="prepare"${busy ? ' disabled' : ''}>${info.ready ? '应用上限（重启生效）' : '准备外部组件'}</button>` : ''}
      ${info.canRestore === true ? `<button class="button subtle" type="button" data-ls-action="remove-components"${busy ? ' disabled' : ''}>移除并恢复本工具组件</button>` : ''}
    </div>`;
  }
  function request(host, info) {
    if (info?.backend !== BACKEND) return {};
    const input = host?.querySelector('[data-sm-mfg-cap]');
    if (!input || input.disabled) throw new Error('MFG 组件状态尚不可应用，请重新检查。');
    const maxGeneratedFrames = Number(input.value);
    if (!Number.isInteger(maxGeneratedFrames) || maxGeneratedFrames < 1 || maxGeneratedFrames > 3)
      throw new Error('生成帧上限必须为1、2或3。');
    // Capture before the host's busy render replaces the controls. The service
    // re-plans and revalidates under its own mutation lock even without a token.
    return { settings: { ...info.settings, maxGeneratedFrames }, proxy: info.proxy,
      ...(maxGeneratedFrames === info.settings?.maxGeneratedFrames && info.previewToken
        ? { previewToken: info.previewToken } : {}) };
  }
  const api = { BACKEND, components, request };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else scope.Sm75Sm86MfgUI = api;
})(typeof window === 'object' ? window : globalThis);
