'use strict';

const MESSAGES = Object.freeze({
  ERR_BAD_REQUEST: '请求参数无效。',
  ERR_UNKNOWN_GAME: '游戏不在当前扫描结果中，请重新扫描。',
  ERR_NO_GAME_EXE: '没有找到可用的游戏主程序。',
  ERR_UNSUPPORTED_API: '所选组件不支持这条图形路线。DirectX 11 需要包含配套 carrier 的兼容包；其他图形 API 暂不支持。',
  ERR_API_SELECTION_REQUIRED: '图形 API 证据不足或存在多条路线，请展开游戏卡片，按游戏的实际启动设置选择 DirectX 11 或 DirectX 12。',
  ERR_API_ROUTE_PENDING: '游戏 API 选择与已部署组件不一致，请先点击修复或应用版本，再启动游戏。',
  ERR_CARRIER_NOT_SELECTED: '桥接已关闭，该 NR 路线不可用。若要安装或修复 DX11 路线，请勾选兼容桥接并使用完整配套组件。',
  ERR_UNSUPPORTED_BITNESS: '当前基础版只支持 64 位游戏。',
  ERR_NO_DLSS: '没有检测到游戏自带的普通 DLSS 超分组件。',
  ERR_EMULATOR_UNSUPPORTED: '当前基础版暂不自动安装到模拟器。',
  ERR_ANTI_CHEAT_CONFIRM: '检测到可能的反作弊组件。继续安装可能导致游戏无法启动、触发安全策略或封禁风险；管理器不会修改或绕过反作弊文件。请确认后继续。',
  ERR_GAME_RUNNING: '请先关闭游戏，再进行安装、修复或卸载。',
  ERR_PROCESS_CHECK: '无法确认游戏是否已关闭，请稍后重试或以管理员身份运行。',
  ERR_NO_WRITE_ACCESS: '没有游戏目录写入权限，请以管理员身份运行管理器。',
  ERR_PAYLOAD_MISSING: '安装组件不完整，请先准备 release payload。',
  ERR_PAYLOAD_HASH: '安装组件校验失败，文件可能损坏或版本不匹配。',
  ERR_ADDON_INVALID: 'Addon 文件无效；请选择 64 位 .addon64 文件。',
  ERR_ADDON_NOT_FOUND: '找不到这个导入的 Addon 版本。',
  ERR_D3D12_CONFLICT: '游戏目录已有其他 d3d12.dll，未自动覆盖。请先确认它不是游戏自己的组件。',
  ERR_GPU_UNSUPPORTED: '未能确认 RTX 20 / 30 / 40 / 50 系列显卡，已停止安装以避免误用组件。',
  ERR_RESHADER_CONFLICT: '游戏目录已有其他 dxgi.dll，基础版不会自动覆盖。',
  ERR_RESHADER_NO_ADDON: '现有 ReShade 不支持 Add-on，无法加载本插件。',
  ERR_HOTKEY_UNSUPPORTED: '当前 Addon 版本的 NR 效果切换键由插件固定为 F6，暂不支持改键。',
  ERR_NOT_INSTALLED: '该游戏尚未由本管理器安装。',
  ERR_BACKUP_INVALID: '备份记录损坏或缺失，已停止写入以保护游戏文件。',
  ERR_FILE_CHANGED: '部分文件已被其他工具修改，未自动删除。',
  ERR_JOB_BUSY: '当前游戏已有操作正在进行。',
  ERR_INTERNAL: '发生未预期错误，请复制诊断信息后反馈。'
});

function appError(code, details = {}) {
  const error = new Error(MESSAGES[code] || MESSAGES.ERR_INTERNAL);
  error.code = code;
  error.details = details;
  return error;
}

function normalizeError(error) {
  const incoming = error && typeof error === 'object' ? error : {};
  const launchError = /^(SETTINGS_|NVAPI_)[A-Z_]+$/.test(incoming.code || '') ||
    ['EXE_REQUIRED', 'RECOVERY_FIRST', 'PLAN_EXPIRED', 'CONFIRM_REQUIRED', 'GAME_RUNNING', 'PROCESS_UNKNOWN',
      'FILE_CHANGED', 'WRITE_VERIFY_FAILED', 'BACKUP_INVALID', 'BACKUP_DAMAGED', 'STAGE_CHANGED', 'JOURNAL_INVALID',
      'AMBIGUOUS_INI', 'CONFLICTING_REQUEST', 'DUPLICATE_JSON_KEY', 'INVALID_INPUT', 'INVALID_JSON',
      'INVALID_JSON_VALUE', 'INVALID_RANGE', 'INVALID_TEXT', 'PROTOCOL_MISMATCH', 'UNKNOWN_FIELD',
      'UNKNOWN_MODE', 'UNKNOWN_PRESET', 'UNKNOWN_QUALITY', 'INVALID_EXE', 'INVALID_SNAPSHOT'].includes(incoming.code);
  const codeMap = {
    errGameRunning: 'ERR_GAME_RUNNING',
    errProcessCheck: 'ERR_PROCESS_CHECK',
    errJobBusy: 'ERR_JOB_BUSY',
    errUnsafeTarget: 'ERR_BACKUP_INVALID',
    errBackupInvalid: 'ERR_BACKUP_INVALID',
    errBackendRecovery: 'ERR_BACKUP_INVALID'
  };
  const code = codeMap[incoming.code] ||
    (typeof incoming.code === 'string' && (MESSAGES[incoming.code] || launchError) ? incoming.code : 'ERR_INTERNAL');
  const details = incoming.details || incoming.params || {};
  let message = MESSAGES[code] || (typeof incoming.message === 'string' ? incoming.message.slice(0, 1500) : '') || MESSAGES.ERR_INTERNAL;
  if (code === 'ERR_GAME_RUNNING' && Array.isArray(details.processes)) {
    const names = [...new Set(details.processes.map(row => row && row.name)
      .filter(name => typeof name === 'string' && name.trim())
      .map(name => name.replace(/[\r\n\t]/g, ' ').slice(0, 100)))];
    if (names.length) message += ` 检测到：${names.slice(0, 3).join('、')}${names.length > 3 ? '等进程' : ''}。`;
  }
  return {
    code,
    message,
    details
  };
}

module.exports = { MESSAGES, appError, normalizeError };
