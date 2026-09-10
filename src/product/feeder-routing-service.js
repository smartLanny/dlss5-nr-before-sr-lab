'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { classifyApi } = require('./game-support');
const { createFeederService } = require('./feeder-service');
const { createLegacyService, RECEIPT: MODERN_RECEIPT, PENDING: MODERN_PENDING } = require('./legacy-service');
const { RECEIPT: HISTORICAL_RECEIPT, regularJson } = require('./feeder-runtime');
const { createLegacyRuntime } = require('./legacy-runtime');
const fail = message => { throw Object.assign(new Error(message), { code: 'FEEDER_OWNER_CONFLICT' }); };

// Receipt ownership selects recovery and repair. A new default never claims or
// upgrades a historical deployment; its original runtime remains available.
function createFeederRoutingService(options) {
  const historical = options.historical || createFeederService(options), modern = options.modern || createLegacyService(options);
  const runtime = options.runtime || createLegacyRuntime(options);
  function requireHardware() {
    if (Array.isArray(options.hardware?.series) && (!options.hardware.series.length || options.hardware.series.some(row => !['RTX40', 'RTX50'].includes(row))))
      throw Object.assign(new Error('本轮 Feeder 配套面向已确认的 RTX 40／50 系列。'), { code: 'LEGACY_HARDWARE_UNSUPPORTED' });
  }
  function owner(game, request = {}) {
    const journal = regularJson(path.join(game.dir, '_DLSS5_Backup/pending-switch.json'), 2 * 1024 * 1024);
    const old = fs.existsSync(path.join(game.dir, HISTORICAL_RECEIPT)) || journal?.files?.some(row => typeof row.rel === 'string' && row.rel.replaceAll('\\', '/').toLowerCase() === HISTORICAL_RECEIPT.toLowerCase());
    const current = fs.existsSync(path.join(game.dir, MODERN_RECEIPT)) || fs.existsSync(path.join(game.dir, MODERN_PENDING));
    if (old && current) fail('同一游戏存在两代 Feeder 记录，请先恢复未完成的组件操作。');
    if (old) {
      if (request.loadingBackend === 'hoyoshade' || String(request.version || '').startsWith('feeder-0151-')) fail('请先恢复旧 Feeder 配套，再安装新输入路线。');
      return historical;
    }
    return modern;
  }
  function selection(game, request = {}) {
    return { api: request.api && request.api !== 'auto' ? request.api : classifyApi(game.scan?.chosen),
      architecture: Number(game.scan?.chosen?.bitness) === 32 ? 'x86' : Number(game.scan?.chosen?.bitness) === 64 ? 'x64' : null, hardwareFamily: options.hardware?.family,
      loadingBackend: request.loadingBackend || 'local', ...(request.proxyEntry ? { proxyEntry: request.proxyEntry } : {}) };
  }
  function summary(game, request = {}) {
    try {
      const chosen = owner(game, request), state = chosen.summary(game);
      if (chosen === historical) return { ...state, generation: 'historical-0131' };
      if (state.installed || state.needsRecovery) return { ...state, generation: 'feeder-0151' };
      requireHardware();
      const pkg = runtime.load(selection(game, request));
      return { ...state, available: true, packageId: pkg.recipe.id, coreVersion: pkg.recipe.coreVersion, api: pkg.recipe.gameApi,
        architecture: pkg.recipe.architecture, loadingBackend: pkg.recipe.loadingBackend, hostRequired: pkg.recipe.hostRequired,
        generation: 'feeder-0151', runtimeVerified: false };
    } catch (error) { return { installed: false, available: false, ready: false, needsRecovery: /OWNER|RECORD|RECEIPT|RECOVERY/.test(error.code || ''),
      reason: error.message, code: error.code, runtimeVerified: false }; }
  }
  const dispatch = method => (game, request, ...args) => { const selected = owner(game, request);
    if (selected === modern && ['previewInstall', 'install'].includes(method)) requireHardware();
    return selected[method](game, request, ...args); };
  return {
    summary, selections: game => Object.fromEntries(['dx9', 'dx10', 'dx11', 'dx12'].map(api => [api, summary(game, { api })])),
    previewInstall: dispatch('previewInstall'), install: dispatch('install'), inspect: dispatch('inspect'), diagnose: dispatch('inspect'),
    previewRestore: dispatch('previewRestore'), restore: dispatch('restore'), launch: dispatch('launch'),
    recover: (game, request) => { const chosen = owner(game, request); return chosen.recover ? chosen.recover(game, request) : chosen.restore(game, request); },
    profile: game => { const chosen = owner(game); return chosen.profile ? chosen.profile(game) : null; },
    ownedModuleManifest: game => { const chosen = owner(game); return chosen.ownedModuleManifest ? chosen.ownedModuleManifest(game) : []; },
    configDir: (game, kind) => owner(game).configDir(game, kind),
    feedbackLogDirectory: game => owner(game).feedbackLogDirectory(game),
    prepareLaunch: async (game, session) => { const chosen = owner(game); if (chosen.prepareLaunch) return chosen.prepareLaunch(game, session);
      const state = await chosen.inspect(game); if (!state.ready) throw Object.assign(new Error(state.reason || 'Feeder 配套未就绪。'), { code: 'FEEDER_NOT_READY' }); },
    recordLaunch: (game, session) => owner(game).recordLaunch?.(game, session),
    runtimeOwner: game => owner(game),
    generation: game => owner(game) === modern ? 'feeder-0151' : 'historical-0131'
  };
}
module.exports = { createFeederRoutingService };
