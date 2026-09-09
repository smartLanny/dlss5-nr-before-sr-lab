'use strict';
const fs = require('node:fs');
const path = require('node:path');

// Static DLL evidence is not a runtime capability or a generated-frame result.
// Manager-owned external NR assets can never upgrade a game's SR/FG eligibility.
function x64Header(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(fd), dos = Buffer.alloc(64);
    if (fs.readSync(fd, dos, 0, 64, 0) !== 64 || dos.readUInt16LE(0) !== 0x5a4d) return false;
    const offset = dos.readUInt32LE(0x3c), header = Buffer.alloc(26);
    return offset >= 64 && offset <= 1024 * 1024 && offset + 26 <= stat.size &&
      fs.readSync(fd, header, 0, 26, offset) === 26 && header.readUInt32LE(0) === 0x4550 &&
      header.readUInt16LE(4) === 0x8664 && header.readUInt16LE(24) === 0x20b;
  } finally { fs.closeSync(fd); }
}
function inspectNativeEnhancementCapabilities(scan, options = {}) {
  function contains(rows, expected) {
    return Array.isArray(rows) && rows.some(row => {
      const file = row?.path || row?.file;
      if (typeof file !== 'string' || !path.isAbsolute(file) || path.basename(file).toLowerCase() !== expected ||
          /(?:^|[\\/])(?:_DLSS5_Feeder|_DLSS5_Backup|feeder-runtime|vulkan-runtime)(?:[\\/]|$)/i.test(file)) return false;
      try {
        const stat = fs.lstatSync(file);
        return stat.isFile() && !stat.isSymbolicLink() && stat.nlink === 1 &&
          (options.pe ? options.pe.getBitness(file) === 64 : x64Header(file));
      } catch { return false; }
    });
  }
  const nativeDlssAvailable = contains(scan?.dlssFiles, 'nvngx_dlss.dll') || contains([scan?.primaryDlss], 'nvngx_dlss.dll');
  const nativeFgAvailable = contains(scan?.dlssFiles, 'nvngx_dlssg.dll') && contains(scan?.streamlineFiles, 'sl.dlss_g.dll') &&
    (contains(scan?.streamlineFiles, 'sl.interposer.dll') || contains(scan?.streamlineFiles, 'sl.common.dll'));
  return { nativeDlssAvailable, nativeFgAvailable, staticOnly: true };
}
// Driver setting enumeration describes the driver, not the selected game's
// implementation. Keep those facts separate all the way to the write boundary.
function assessEnhancementState({ domain, request = {}, game = {}, hardware = {}, driver = {}, confirmation = null } = {}) {
  const blockers = [];
  const add = (code, message) => blockers.push({ code, message });
  const support = game.support || { status: 'unknown', source: null, evidence: [] };
  const setting = game.gameSetting || { state: 'unknown', source: null };
  const series = [...new Set(Array.isArray(hardware.series) ? hardware.series : [])];
  const singleGpu = hardware.source !== 'unavailable' && hardware.family !== 'mixed' && series.length === 1;
  const backend = request.backend || (domain === 'sr' ? 'native' : singleGpu && series[0] === 'RTX40' ? 'mfgunlock' : 'nvidia');
  const isDriver = backend === 'native' || backend === 'nvidia';
  const feature = domain === 'sr' ? 'DLSS 超分' : 'DLSS 帧生成';
  const confirmed = setting.state !== 'off' && confirmation?.enabled === true && confirmation.domain === domain &&
    typeof game.exeIdentity === 'string' && confirmation.exeIdentity === game.exeIdentity &&
    typeof game.exe === 'string' && typeof confirmation.exe === 'string' &&
    path.resolve(confirmation.exe).toLowerCase() === path.resolve(game.exe).toLowerCase();
  if (!['sr', 'fg'].includes(domain)) add('SETTINGS_DOMAIN', '图像功能域无效。');
  if (!singleGpu || domain === 'sr' && !/^RTX(?:20|30|40|50)$/.test(series[0] || '') ||
      domain === 'fg' && (backend === 'nvidia' ? series[0] !== 'RTX50' : !['mfgunlock', 'rtx40'].includes(backend) || series[0] !== 'RTX40'))
    add('SETTINGS_GPU_UNKNOWN', '当前显卡未确认满足此功能要求。');
  if (support.status !== 'supported' || !['catalog', 'native-integration', 'runtime'].includes(support.source))
    add(support.status === 'unsupported' ? 'SETTINGS_GAME_UNSUPPORTED' : 'SETTINGS_GAME_SUPPORT_UNKNOWN',
      support.status === 'unsupported' ? `当前游戏明确不支持此${feature}路线。` : `尚无可核对的游戏${feature}支持证据。`);
  if (isDriver) {
    if (driver.available !== true) add('SETTINGS_DRIVER_UNAVAILABLE', '无法确认 NVIDIA 驱动设置接口可用。');
    const required = request.requiredSettingIds || [];
    if (!Array.isArray(driver.settingIds) || required.some(id => !driver.settingIds.includes(id)))
      add('SETTINGS_DRIVER_SETTINGS_UNKNOWN', '当前驱动尚未确认提供所需设置；配置值缺失不代表游戏不支持。');
    if (request.minimumDriverVersion && (!Number.isInteger(driver.version) || driver.version < request.minimumDriverVersion))
      add('SETTINGS_DRIVER_VERSION_UNCONFIRMED', '尚未确认当前驱动达到此选项所需版本。');
  }
  const capabilities = support.capabilities || {};
  // This addon only raises game requests; it does not implement Dynamic.
  // The native integration alone proves no >2x NVIDIA override capacity.
  const modes = backend === 'mfgunlock' ? ['follow', 'fixed'] : backend === 'nvidia'
    ? ['off', 'fixed', ...(capabilities.dynamic === true ? ['dynamic'] : [])] : [];
  const declaredMultipliers = Array.isArray(capabilities.multipliers) ? capabilities.multipliers :
    backend === 'mfgunlock' ? [2, 3, 4] : [2];
  const multipliers = declaredMultipliers.filter(value => Number.isInteger(value) && value >= 2 && value <= 6);
  if (domain === 'fg' && request.mode && !['restore', ...modes, ...(backend === 'rtx40' ? ['follow', 'fixed', 'dynamic'] : [])].includes(request.mode))
    add('SETTINGS_MODE_UNSUPPORTED', '当前后端及游戏证据未确认支持此补帧模式。');
  if (domain === 'fg' && request.mode === 'fixed' && !multipliers.includes(request.multiplier))
    add('SETTINGS_MULTIPLIER_UNCONFIRMED', '此游戏及当前运行库尚未确认支持请求的倍率。');
  if (setting.state === 'off') add('SETTINGS_GAME_FEATURE_OFF', `游戏配置表明${feature}已关闭；请先在游戏中开启。`);
  else if (setting.state === 'missing' && setting.requiresFirstRun === true)
    add('SETTINGS_FIRST_RUN_REQUIRED', '游戏尚未生成必需的配置，请正常运行一次后重新检测。');
  else if (setting.state !== 'on' && !confirmed)
    add('SETTINGS_GAME_CONFIRMATION_REQUIRED', `无法读取游戏内${feature}开关，请确认已在游戏中开启。`);
  const evidence = { support, static: game.staticEvidence || null, driver: { ...driver, perGameSupport: false },
    gameSetting: setting, confirmation: confirmed ? { ...confirmation, source: 'user-confirmation', runtimeVerified: false } : null };
  const baseBlockers = blockers.filter(row => row.code !== 'SETTINGS_GAME_CONFIRMATION_REQUIRED');
  const state = blockers.some(row => row.code === 'SETTINGS_FIRST_RUN_REQUIRED') ? 'waiting-first-run' :
    blockers.some(row => row.code === 'SETTINGS_GAME_FEATURE_OFF') ? 'waiting-game-setting' :
    baseBlockers.length ? 'unavailable' : blockers.length ? 'waiting-confirmation' : 'configurable';
  return { domain, backend, eligible: blockers.length === 0, blockers, evidence,
    state, canConfirm: state === 'waiting-confirmation' && !confirmed,
    availableModes: modes, availableMultipliers: multipliers, runtimeVerified: false,
    actual: { state: 'unknown', source: null }, officialOverrideCertified: support.source === 'catalog' && support.official === true };
}
module.exports = { inspectNativeEnhancementCapabilities, assessEnhancementState };
