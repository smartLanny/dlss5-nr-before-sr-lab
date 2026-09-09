'use strict';

const fs = require('node:fs');
const path = require('node:path');

const ADMINISTRATOR_PROBE = '$id=[Security.Principal.WindowsIdentity]::GetCurrent(); $p=New-Object Security.Principal.WindowsPrincipal($id); $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)';
const PERMISSION_CODES = new Set(['EACCES', 'EPERM', 'ERR_NO_WRITE_ACCESS', 'NVAPI_ACCESS_DENIED', 'NVAPI_INSUFFICIENT_PRIVILEGE']);
const PERMISSION_GUIDANCE = '若因目录或驱动权限不足，可在“设置 → 以管理员身份重启”后核对恢复状态，再手动重试；不会自动重复本次操作。';

function startupError(code, message) { return Object.assign(new Error(message), { code }); }
function quotePowerShell(value) { return `'${String(value).replace(/'/g, "''")}'`; }
function quoteArgument(value) { return `"${String(value).replace(/(\\*)"/g, '$1$1\\"').replace(/(\\+)$/g, '$1$1')}"`; }

// Keep the normal window independent of PowerShell/UAC. Token inspection runs
// only when the visible startup context or an explicit admin action asks for it.
function createStartupElevation({ app, runPowerShell, processInfo = process, log = () => {},
  releaseLock = () => false, reacquireLock = () => true, handoff, restoreTimeoutMs = 2500 }) {
  let privilege = 'unknown', inspection = null, restarting = false, recoveryTicket = null;
  async function inspectPrivilege() {
    if (processInfo.platform !== 'win32') return 'standard';
    if (!inspection) inspection = (async () => {
      try {
        const value = await runPowerShell(ADMINISTRATOR_PROBE, 3000);
        if (!/^(?:true|false)$/i.test(value)) throw new Error('权限检查没有返回明确结果。');
        privilege = /^true$/i.test(value) ? 'administrator' : 'standard';
        log('startup-privilege', { privilege });
      } catch (error) {
        privilege = 'unknown';
        log('administrator-check-failed', { message: error.message });
      }
      return privilege;
    })();
    return inspection;
  }
  async function context() {
    if (recoveryTicket && reacquireLock()) {
      handoff.finish(recoveryTicket); recoveryTicket = null; restarting = false;
      log('elevation-original-lock-restored');
    }
    const noSandbox = app.commandLine.hasSwitch('no-sandbox');
    return { mode: noSandbox ? 'compatibility' : 'normal', sandbox: !noSandbox,
      privilege: await inspectPrivilege(), canRestartElevated: processInfo.platform === 'win32', restarting };
  }
  async function relaunchAsAdministrator() {
    if (processInfo.platform !== 'win32') throw startupError('STARTUP_ADMIN_UNAVAILABLE', '管理员重启仅支持 Windows。');
    if (restarting) throw startupError('ERR_JOB_BUSY', '管理员重启正在进行，请等待。');
    const currentPrivilege = await inspectPrivilege();
    if (currentPrivilege === 'administrator') { log('administrator-confirmed'); return true; }
    if (currentPrivilege === 'unknown') throw startupError('STARTUP_ADMIN_CHECK_FAILED', '无法检查当前权限，未发起重复提权。可关闭管理器后，从 EXE 的右键菜单选择“以管理员身份运行”；也可导出启动诊断。');
    if (processInfo.argv.includes('--elevation-attempted')) throw startupError('STARTUP_ADMIN_NOT_GRANTED', '提权后的进程仍未取得管理员权限，已停止重启循环。');
    if (!handoff) throw startupError('STARTUP_HANDOFF_UNAVAILABLE', '管理员启动握手不可用，当前窗口已保留。');
    const ticket = handoff.begin();
    const args = [...processInfo.argv.slice(1).filter(value => typeof value === 'string' && value !== '--as-admin' && !value.startsWith('--elevation-handoff=')),
      '--as-admin', '--elevation-attempted', `--elevation-handoff=${ticket.nonce}`];
    const portable = processInfo.env.PORTABLE_EXECUTABLE_FILE;
    const target = portable && path.isAbsolute(portable) && /[.]exe$/i.test(portable) && fs.existsSync(portable) ? portable : processInfo.execPath;
    const command = `$ErrorActionPreference='Stop'; $p=Start-Process -FilePath ${quotePowerShell(target)} -WorkingDirectory ${quotePowerShell(path.dirname(target))} -ArgumentList ${quotePowerShell(args.map(quoteArgument).join(' '))} -Verb RunAs -PassThru; $p.Id`;
    restarting = true;
    let released = false;
    try {
      // Without releasing the normal instance lock, the elevated child would
      // merely focus this window and exit. Restore ownership if UAC is cancelled.
      released = releaseLock();
      log('elevation-requested');
      const childPid = await runPowerShell(command, 60000);
      if (!/^\d+$/.test(childPid)) throw new Error('没有收到管理员进程编号。');
      log('elevation-child-started', { pid: Number(childPid) });
      await handoff.wait(ticket);
      handoff.finish(ticket);
      app.quit();
      return false;
    } catch (error) {
      handoff.cancel(ticket);
      let lockRestored = !released;
      const deadline = Date.now() + restoreTimeoutMs;
      while (!lockRestored) {
        lockRestored = reacquireLock();
        if (lockRestored || Date.now() >= deadline) break;
        await new Promise(resolve => setTimeout(resolve, 100));
      }
      restarting = !lockRestored;
      // Keep the cancellation marker for a late-starting child to observe.
      if (!lockRestored) recoveryTicket = ticket;
      log('elevation-failed', { message: error.message, lockRestored });
      throw startupError(/^STARTUP_/.test(error.code || '') ? error.code : 'STARTUP_ADMIN_RESTART_FAILED', `未能启动管理员进程并完成界面初始化：${error.message || '请查看启动诊断。'} 当前窗口已保留，请导出启动诊断。${lockRestored ? '' : ' 新管理员实例仍占用启动锁，当前窗口暂不接受修改；请先关闭新实例，再回到设置刷新状态。'}`);
    }
  }
  return { context, relaunchAsAdministrator, get restarting() { return restarting; }, get privilege() { return privilege; } };
}

function permissionFailure(error, depth = 0) {
  if (!error || typeof error !== 'object' || depth > 4) return false;
  if (PERMISSION_CODES.has(error.code)) return true;
  return permissionFailure(error.cause, depth + 1) || permissionFailure(error.details?.cause, depth + 1);
}

// Enrich the existing transaction result only after its normal error handling.
// Never retry a write, bypass a guard, or replace the original recovery details.
function withPermissionRecovery(result) {
  if (result?.ok !== false || !permissionFailure(result.error)) return result;
  return { ...result, error: { ...result.error,
    message: `${result.error.message || '权限不足，操作未完成。'} ${PERMISSION_GUIDANCE}`,
    details: { ...result.error.details, recoveryAction: 'restart-elevated' } } };
}

module.exports = { createStartupElevation, withPermissionRecovery, ADMINISTRATOR_PROBE };
