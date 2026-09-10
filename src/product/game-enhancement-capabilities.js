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
function assessEnhancementState({ domain, request = {}, game = {}, hardware = {}, driver = {} } = {}) {
  const blockers = [], warnings = [];
  const add = (code, message) => blockers.push({ code, message });
  const support = game.support || { status: 'unknown', source: null, evidence: [] };
  const setting = game.gameSetting || { state: 'unknown', source: null };
  const series = [...new Set(Array.isArray(hardware.series) ? hardware.series : [])];
  const singleGpu = hardware.source !== 'unavailable' && hardware.family !== 'mixed' && series.length === 1;
  const backend = request.backend || (domain === 'sr' ? 'native' : singleGpu && series[0] === 'RTX40' ? 'mfgunlock' : 'nvidia');
  const isDriver = backend === 'native' || backend === 'nvidia';
  const feature = domain === 'sr' ? 'DLSS 超分' : 'DLSS 帧生成';
  if (!['sr', 'fg'].includes(domain)) add('SETTINGS_DOMAIN', '图像功能域无效。');
  if (!singleGpu || domain === 'sr' && !/^RTX(?:20|30|40|50)$/.test(series[0] || '') ||
      domain === 'fg' && (backend === 'nvidia' ? series[0] !== 'RTX50' : !['mfgunlock', 'rtx40'].includes(backend) || series[0] !== 'RTX40'))
    add('SETTINGS_GPU_UNKNOWN', '当前显卡未确认满足此功能要求。');
  if (support.status !== 'supported' || !['catalog', 'native-integration', 'trusted-mod', 'runtime'].includes(support.source))
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
  if (backend === 'mfgunlock' && capabilities.mfgUnlock?.available !== true)
    add('SETTINGS_MFG_RUNTIME_UNCONFIRMED', capabilities.mfgUnlock?.api && capabilities.mfgUnlock.api !== 'dx12'
      ? '当前 MFG Unlock 配套仅对已确认的 DX12 路线开放。'
      : '尚未确认已有 x64 DLSS-G 310.x 或更新运行库，不能准备 MFG Unlock。');
  // This addon only raises game requests; it does not implement Dynamic.
  // The native integration alone proves no >2x NVIDIA override capacity.
  const declaredMultipliers = backend === 'mfgunlock' ? (capabilities.mfgUnlock?.available === true && Array.isArray(capabilities.mfgUnlock.multipliers) ? capabilities.mfgUnlock.multipliers : [])
    : Array.isArray(capabilities.multipliers) ? capabilities.multipliers : [2];
  const multipliers = declaredMultipliers.filter(value => Number.isInteger(value) && value >= 2 && value <= 6);
  const modes = backend === 'mfgunlock' ? (capabilities.mfgUnlock?.available === true ? ['follow', ...(multipliers.length ? ['fixed'] : [])] : []) : backend === 'nvidia'
    ? ['off', 'fixed', ...(capabilities.dynamic === true ? ['dynamic'] : [])] : [];
  if (domain === 'fg' && request.mode && !['restore', ...modes, ...(backend === 'rtx40' ? ['follow', 'fixed', 'dynamic'] : [])].includes(request.mode))
    add('SETTINGS_MODE_UNSUPPORTED', '当前后端及游戏证据未确认支持此补帧模式。');
  if (domain === 'fg' && request.mode === 'fixed' && !multipliers.includes(request.multiplier))
    add('SETTINGS_MULTIPLIER_UNCONFIRMED', '此游戏及当前运行库尚未确认支持请求的倍率。');
  // An override can be configured while the feature is off. The game switch is
  // activation evidence, not permission to invent support. Old user statements
  // deliberately have no role in this decision, including the write boundary.
  const activation = { state: ['on', 'off', 'missing'].includes(setting.state) ? setting.state : 'unknown',
    source: setting.source || null, runtimeVerified: false,
    message: setting.state === 'on' ? `游戏配置显示${feature}已开启；实际采用仍待本次运行确认。`
      : setting.state === 'off' ? `游戏配置显示${feature}已关闭；请在游戏中开启后使用本次设置。`
      : `建议先正常运行游戏，在游戏设置中开启${feature}并退出；管理器不会代替游戏打开此功能。` };
  if (activation.state !== 'on') warnings.push({ code: activation.state === 'off' ? 'SETTINGS_GAME_FEATURE_OFF'
    : activation.state === 'missing' ? 'SETTINGS_FIRST_RUN_RECOMMENDED' : 'SETTINGS_GAME_ACTIVATION_UNKNOWN', message: activation.message });
  if (setting.state === 'missing' && setting.requiredForSupport === true)
    add('SETTINGS_FIRST_RUN_REQUIRED', '尚未生成用于验证此游戏集成的必需配置，请正常运行一次后重新检测。');
  const evidence = { support, static: game.staticEvidence || null, driver: { ...driver, perGameSupport: false },
    gameSetting: setting, confirmation: null };
  const state = blockers.some(row => row.code === 'SETTINGS_FIRST_RUN_REQUIRED') ? 'waiting-first-run' : blockers.length ? 'unavailable' : 'configurable';
  return { domain, backend, eligible: blockers.length === 0, blockers, warnings, evidence, activation,
    state, canConfirm: false,
    availableModes: modes, availableMultipliers: multipliers, runtimeVerified: false,
    actual: { state: 'unknown', source: null }, officialOverrideCertified: support.source === 'catalog' && support.official === true };
}
module.exports = { inspectNativeEnhancementCapabilities, assessEnhancementState };
