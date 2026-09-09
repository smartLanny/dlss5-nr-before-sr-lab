'use strict';

(function (root) {
  function attach(document, manager) {
    const panel = document.getElementById('startupSettings');
    if (!panel) return null;
    const mode = document.getElementById('startupMode');
    const privilege = document.getElementById('startupPrivilege');
    const note = document.getElementById('startupModeNote');
    const message = document.getElementById('startupMessage');
    const refreshButton = document.getElementById('startupRefreshBtn');
    const restartButton = document.getElementById('startupRestartAdminBtn');
    let context = null, busy = false;

    function showMessage(text, error = false) {
      message.textContent = text;
      message.classList.toggle('error', error);
      message.setAttribute('role', error ? 'alert' : 'status');
    }
    function render() {
      const compatible = context?.mode === 'compatibility' || context?.sandbox === false;
      mode.textContent = context ? compatible ? '临时兼容启动' : '正常启动 · 默认沙箱' : '暂未确认';
      privilege.textContent = { standard: '普通权限', administrator: '管理员权限', unknown: '暂未确认' }[context?.privilege] || '暂未确认';
      note.hidden = !compatible;
      note.textContent = compatible ? '本次通过兼容入口关闭了 Chromium 沙箱，仅用于启动排障。直接打开原 EXE 可恢复正常启动。' : '';
      restartButton.textContent = busy ? '正在等待新窗口…' : context?.privilege === 'administrator' ? '已具有管理员权限' : '以管理员身份重启';
      restartButton.disabled = busy || !context?.canRestartElevated || context?.privilege === 'administrator' || context?.restarting === true;
      refreshButton.disabled = busy;
      panel.setAttribute('aria-busy', String(busy));
    }
    function unwrap(result) {
      if (result?.ok !== true) throw new Error(result?.error?.message || '启动状态暂不可用，请运行独立的“启动诊断.cmd”。');
      return result.value;
    }
    async function refresh({ preserveMessage = false } = {}) {
      try {
        if (typeof manager?.getStartupContext !== 'function') throw new Error('当前版本未提供启动状态接口，请运行独立的“启动诊断.cmd”。');
        context = unwrap(await manager.getStartupContext());
        if (!context || !['standard', 'administrator', 'unknown'].includes(context.privilege)) throw new Error('启动状态返回不完整，请导出启动诊断。');
        if (!preserveMessage) showMessage(context.restarting
          ? '新实例尚未释放启动锁，当前窗口暂不接受修改。请关闭新实例后刷新状态。'
          : context.privilege === 'unknown' ? '暂时无法确认权限，浏览界面不受影响。需要写入受保护目录时可使用管理员入口。' : '');
      } catch (error) {
        if (!preserveMessage) showMessage(error.message, true);
      }
      render();
    }
    async function restart() {
      if (busy || restartButton.disabled) return;
      busy = true; render();
      showMessage('正在等待新窗口完成启动，当前窗口会保持打开。');
      try {
        if (typeof manager?.restartElevated !== 'function') throw new Error('当前版本未提供管理员重启接口。');
        // The clearly labelled user click is the application-level consent.
        // Windows UAC remains responsible for granting the requested privilege.
        const value = unwrap(await manager.restartElevated({ confirm: true }));
        if (value?.alreadyAdministrator) showMessage('当前窗口已具有管理员权限，无需重复重启。');
        else showMessage('新管理员窗口已完成启动，正在切换。');
      } catch (error) {
        showMessage(/当前窗口.*保留/.test(error.message) ? error.message : `重启未完成，当前窗口仍保留。${error.message}`, true);
      } finally {
        busy = false;
        await refresh({ preserveMessage: true });
      }
    }
    refreshButton.addEventListener('click', () => refresh());
    restartButton.addEventListener('click', restart);
    const ready = refresh();
    return { ready, refresh, restart };
  }

  if (typeof module === 'object' && module.exports) module.exports = { attach };
  if (root?.document) {
    const initialize = () => attach(root.document, root.manager);
    if (root.document.readyState === 'loading') root.document.addEventListener('DOMContentLoaded', initialize, { once: true });
    else initialize();
  }
})(typeof window === 'object' ? window : null);
