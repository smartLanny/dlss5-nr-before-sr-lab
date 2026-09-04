'use strict';

const MESSAGES = Object.freeze({
  ERR_CONFIG_CHANGED: '配置已被其他入口修改，请重新读取后再保存。',
  ERR_RECOVERY_REQUIRED: '存在未完成事务，请先关闭游戏并恢复上次操作。',
  ERR_CORE_CONFLICT: '检测到其他 NR 核心或旧安装，请先按原方式卸载；不会自动覆盖。',
  ERR_RESHADER_DISABLED: 'ReShade 中已禁用本组件，请在原设置页重新启用。',
  ERR_BAD_REQUEST: '请求参数无效。',
  ERR_UNKNOWN_GAME: '游戏不在当前扫描结果中，请重新扫描。',
  ERR_NO_GAME_EXE: '没有找到可用的游戏主程序。',
  ERR_UNSUPPORTED_API: '当前基础版只支持 已明确识别为 64 位 DirectX 12 的游戏。',
  ERR_UNSUPPORTED_BITNESS: '当前基础版只支持 64 位游戏。',
  ERR_NO_DLSS: '没有检测到游戏自带的普通 DLSS 超分组件。',
  ERR_EMULATOR_UNSUPPORTED: '当前基础版暂不自动安装到模拟器。',
  ERR_ANTI_CHEAT: '检测到反作弊组件。为避免封号风险，基础版不会继续安装。',
  ERR_GAME_RUNNING: '请先关闭游戏，再进行安装、修复或卸载。',
  ERR_PROCESS_CHECK: '无法确认游戏是否已关闭，请稍后重试或以管理员身份运行。',
  ERR_NO_WRITE_ACCESS: '没有游戏目录写入权限，请以管理员身份运行管理器。',
  ERR_PAYLOAD_MISSING: '安装组件不完整，请先准备 release payload。',
  ERR_PAYLOAD_HASH: '安装组件校验失败，文件可能损坏或版本不匹配。',
  ERR_RESHADER_CONFLICT: '游戏目录已有其他 dxgi.dll，基础版不会自动覆盖。',
  ERR_RESHADER_NO_ADDON: '现有 ReShade 不支持 Add-on，无法加载本插件。',
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
  const codeMap = {
    errGameRunning: 'ERR_GAME_RUNNING',
    errProcessCheck: 'ERR_PROCESS_CHECK',
    errJobBusy: 'ERR_JOB_BUSY',
    errUnsafeTarget: 'ERR_BACKUP_INVALID',
    errBackupInvalid: 'ERR_BACKUP_INVALID',
    errBackendRecovery: 'ERR_BACKUP_INVALID'
  };
  const code = codeMap[incoming.code] ||
    (typeof incoming.code === 'string' && MESSAGES[incoming.code] ? incoming.code : 'ERR_INTERNAL');
  return {
    code,
    message: MESSAGES[code] || incoming.message || MESSAGES.ERR_INTERNAL,
    details: incoming.details || incoming.params || {}
  };
}

module.exports = { MESSAGES, appError, normalizeError };
