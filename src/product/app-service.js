'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('node:crypto');
const { PRODUCT, DX11_COMPAT_VERSION, INSTALLED_NAMES } = require('./constants');
const { isDx11Only, classifyApi, assess } = require('./game-support');
const { createStore } = require('./state-store');
const { createLibraryWorkerClient } = require('./library-worker-client');
const { createPayloadInspectionCache } = require('./payload-inspection-cache');
const { createInstaller } = require('./installer');
const { payloadRoot, readBundle, requirePayload, sha256 } = require('./payload');
const { readConfig, writeConfig, defaultPatch } = require('./nr-config');
const { readReShadeHotkey, writeReShadeHotkey } = require('./hotkeys');
const { readManifest, assertManifestExecutable, manifestPath } = require('./manifest');
const { appError, normalizeError, MESSAGES } = require('./errors');
const { detectGpu: readGpu } = require('./gpu');
const { createArtService } = require('./art-service');
const { readOtaPackage } = require('./ota');
const { createFeedbackCollector } = require('./feedback');
const { createExternalRuntime, RECEIPT: EXTERNAL_RECEIPT, PENDING: EXTERNAL_PENDING } = require('./external-runtime');
const { resolveVersion } = require('./version-selection');
const { resolvePayloadDirectory, inspectSource } = require('./payload-source');
const { createVulkanService } = require('./vulkan-service');
const { createFeederService } = require('./feeder-service');
const { inspectNativeEnhancementCapabilities } = require('./game-enhancement-capabilities');
const { createGameLaunchBroker } = require('./game-launch-broker');
const { createReframeworkPreparation } = require('./reframework-preparation');
const { REFRAMEWORK_ADAPTERS } = require('./reframework-compatibility');
const { createRdr2ApiSettings } = require('./rdr2-api-settings');
const { createGameApiSettings } = require('./game-api-settings');

const QUIET_READ_ACTIONS = new Set(['boot', 'games-refresh', 'games-list', 'game-diagnose', 'game-hotkeys-read',
  'nr-read', 'sr-model-read', 'launch-settings-inspect', 'addon-list', 'game-art', 'game-icon', 'game-feeder-inspect',
  'game-selection-icon', 'feedback-build', 'payload-source-read', 'game-reframework-inspect']);

function createAppService({ userData, resourcesPath, appDir, documentsDir, version = '0.0.0', overrides = {} }) {
  const store = createStore(path.join(userData, 'settings.json'));
  const library = overrides.library || createLibraryWorkerClient({ documentsDir });
  const installer = overrides.installer || createInstaller();
  const externalDeployment = overrides.externalDeployment || createExternalRuntime({ userData,
    guards: { ...require('../core/install-guards'), ...(overrides.assertGameClosed ? { assertGameClosed: overrides.assertGameClosed } : {}) },
    ...overrides.externalDeploymentOptions });
  const deploymentPlans = new Map();
  const specialDeploymentPlans = new Map();
  const apiSettingsReader = overrides.apiSettingsReader || createRdr2ApiSettings({ documentsDir });
  const gameApiSettings = overrides.gameApiSettings || createGameApiSettings({ userData, settings: apiSettingsReader,
    assertGameClosed: overrides.assertGameClosed || require('../core/install-guards').assertGameClosed });
  const detectGpu = overrides.detectGpu || readGpu;
  const artService = createArtService({ userData });
  const feedback = createFeedbackCollector({ userData, productVersion: version,
    resolveFeederLogDirectory: game => feeder.feedbackLogDirectory(game) });
  const payloadInspection = createPayloadInspectionCache();
  const bundledPayloadDir = payloadRoot(fs.existsSync(path.join(resourcesPath || '', 'payload')) ? resourcesPath : appDir);
  let payloadDir = store.read().payloadSourcePath || bundledPayloadDir;
  const addonVersionsDir = path.join(userData, 'addon-versions');
  let games = [];
  let latestScanWarnings = [];
  let collectionFresh = false;
  let activeRefresh = null, refreshGeneration = 0, scanEpoch = 0;
  let hardware = detectGpu();
  const vulkan = overrides.vulkan || createVulkanService({ userData, resourcesPath, appDir, hardware });
  const feeder = overrides.feeder || createFeederService({ userData, resourcesPath, appDir, hardware });
  let nativeLaunchBroker = overrides.nativeLaunchBroker;
  const refResources = resourcesPath && fs.existsSync(path.join(resourcesPath, 'reframework-01417', 'component.json'))
    ? path.join(resourcesPath, 'reframework-01417') : path.join(appDir, 'resources', 'reframework-01417');
  const reframework = overrides.reframework || createReframeworkPreparation({ componentRoot: refResources });
  function reframeworkInput(game) {
    const exe = game?.scan?.chosen?.path;
    if (!exe || path.resolve(path.dirname(exe)).toLowerCase() !== path.resolve(game.dir).toLowerCase() || game.scan.chosen.bitness !== 64) return null;
    const adapter = REFRAMEWORK_ADAPTERS.find(row => row.executable.toLowerCase() === path.basename(exe).toLowerCase());
    // New scans distinguish engine evidence from deployment policy. Existing
    // receipts remain accessible for recovery if game assets were removed.
    if (adapter && game.engine !== undefined && game.engine?.profile?.adapterId !== adapter.id &&
        !fs.existsSync(path.join(game.dir, '_DLSS5_Backup/reframework-preparation.json'))) return null;
    return adapter ? { gameDir: game.dir, exe, engine: adapter.engine } : null;
  }
  async function prepareDetectedReframework(game, result) {
    const input = classifyApi(game?.scan?.chosen) === 'dx12' && reframeworkInput(game);
    if (!input) return result;
    try {
      const prepared = await reframework.prepare(input);
      return { ...result, reframework: { ...prepared, automatic: true, ready: true } };
    } catch (cause) {
      // The native transaction has committed. Keep that fact visible while the
      // independent compatibility receipt remains available for repair/recovery.
      const error = normalizeError(cause);
      await feedback.record({ action: 'game-reframework-auto', gameId: game.id, ok: false,
        errorCode: error.code, errorMessage: error.message }).catch(() => {});
      return { ...result, reframework: { automatic: true, ready: false, error } };
    }
  }
  async function prepareExistingReframework(game, options = {}) {
    const input = classifyApi(game?.scan?.chosen) === 'dx12' && reframeworkInput(game);
    if (!input || !fs.existsSync(manifestPath(game.dir))) return;
    const guards = require('../core/install-guards');
    if (guards.antiCheatPresent(game.dir) && options.allowAntiCheat !== true) throw appError('ERR_ANTI_CHEAT_CONFIRM');
    // Existing official loaders may already have a matching Core cache. Record
    // component ownership first so the original installer can confirm that cache.
    await reframework.prepare(input);
  }
  const vulkanRoute = game => classifyApi(game?.scan?.chosen) === 'vulkan';
  const vulkanOwned = game => vulkan.summary(game).installed === true;
  const feederOwned = game => feeder.summary(game).installed === true;
  const hasExternalRecord = game => fs.existsSync(path.join(game.dir, EXTERNAL_RECEIPT)) || fs.existsSync(path.join(game.dir, EXTERNAL_PENDING));
  const externalOwned = game => hasExternalRecord(game) && externalDeployment.owned(game);
  function gameLayout(game) {
    if (hasExternalRecord(game)) return externalDeployment.getLayout(game);
    if (feederOwned(game) || vulkanOwned(game)) {
      const isFeeder = feederOwned(game), config = isFeeder ? feeder.configDir(game, 'reshade') : vulkan.configDir(game);
      const addon = isFeeder ? feeder.configDir(game, 'nr') : path.join(config, 'addons');
      return { mode: 'local', source: isFeeder ? 'feeder' : 'vulkan', exe: game.scan?.chosen?.path,
        gameRoot: game.dir, loaderDir: path.dirname(game.scan.chosen.path), runtimeDir: config, addonDir: addon,
        addonDirectory: addon, activeConfigPath: path.join(config, 'ReShade.ini'), reshadeConfigDir: config,
        nrConfigDir: addon, logDirs: [config, addon], verified: true, blockers: [], needsRecovery: false };
    }
    return externalDeployment.getLayout(game);
  }
  function feederRouteSelection(game, api) {
    const chosen = game.scan?.chosen;
    if (!chosen) throw appError('ERR_NO_GAME_EXE');
    let detected = chosen.detectedApiResolution || { api: chosen.detectedApi || 'unknown' };
    if (api === 'auto' && chosen.apiSettings?.kind === 'rdr2-system-xml') {
      const current = apiSettingsReader.read({ ...chosen.apiSettings, exe: chosen.path });
      detected = { api: current.api || 'unknown', source: 'game-settings', evidence: [] };
    }
    const nextApi = api === 'auto' ? detected.api : api;
    if (nextApi !== 'dx12') throw appError(['unknown', 'mixed'].includes(nextApi) ? 'ERR_API_SELECTION_REQUIRED' : 'ERR_UNSUPPORTED_API');
    if (Array.isArray(chosen.supportedApis) && !chosen.supportedApis.includes(nextApi)) throw appError('ERR_UNSUPPORTED_API');
    const selected = { ...chosen, apiResolution: { api: nextApi,
      source: api === 'auto' ? detected.source : 'override', evidence: detected.evidence || [] } };
    return { ...game, apiOverride: api, chosen: selected,
      scan: { ...game.scan, chosen: selected, componentSelection: { ...game.scan.componentSelection, dx11Carrier: false } } };
  }
  function feederSelectionSummary(game, current, vk) {
    try {
      if (vk.installed || vk.needsRecovery) routeRestoreFirst();
      if (readManifest(game.dir)) throw Object.assign(new Error('先恢复原生 DLSS 配套，再准备 Feeder。'), { code: 'FEEDER_ROUTE_CONFLICT' });
      const selected = feeder.summary(feederRouteSelection(game, 'dx12'));
      return { ...current, selectionAvailable: selected.available === true, selectionReason: selected.reason || null };
    } catch (error) {
      return { ...current, selectionAvailable: false, selectionReason: error.message };
    }
  }
  function requireNoFeeder(game) {
    const state = feeder.summary(game);
    if (state.installed || state.needsRecovery) throw Object.assign(new Error('请先恢复无 DLSS 的 Feeder 配套，再切换原生或 Vulkan 路线。'), { code: 'FEEDER_RESTORE_FIRST' });
  }
  function requireKnownVulkanOwnership(game) {
    const state = vulkan.summary(game);
    if (state.needsRecovery && !state.installed) throw Object.assign(new Error(state.reason ||
      'Vulkan 恢复记录无法读取，暂时不能移除游戏条目或改变加载路线。请保留记录并保存反馈。'), { code: 'VULKAN_RECOVERY_FIRST' });
  }
  function routeRestoreFirst() {
    throw Object.assign(new Error('Vulkan 与原有 DX11 / DX12 加载方式不同。请先卸载当前插件并恢复备份，再选择新的 API 安装。'), { code: 'VULKAN_RESTORE_FIRST' });
  }
  async function settingDirectory(game, kind = 'nr') {
    if (externalOwned(game)) {
      const layout = gameLayout(game);
      await require('./launch-safety').noLinks(kind === 'nr' ? path.join(layout.nrConfigDir, 'nr_before_sr.ini') : layout.activeConfigPath);
      return kind === 'nr' ? layout.nrConfigDir : layout.reshadeConfigDir;
    }
    if (feederOwned(game)) return feeder.configDir(game, kind);
    if (vulkanOwned(game)) {
      const basePath = vulkan.configDir(game);
      return kind === 'nr' ? path.join(basePath, 'addons') : basePath;
    }
    const input = kind === 'nr' && reframeworkInput(game);
    if (input) {
      const state = await reframework.inspect(input);
      if (state.loader?.exists && state.canPrepare && state.config?.effective && state.config.existingStoragePreferred)
        return path.dirname(state.config.effective);
      if (state.loader?.exists && state.blockers?.length) throw Object.assign(new Error(state.blockers[0].message), { code: state.blockers[0].code });
    }
    return path.dirname(installedExecutable(game));
  }

  function assertSourceIdentity() {
    const selected = store.read();
    if (!selected.payloadSourcePath) return;
    const file = path.join(payloadDir, 'bundle.json');
    if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_SOURCE_UNAVAILABLE', { path: payloadDir, file: 'bundle.json' });
    readBundle(payloadDir); // Bound metadata size and reject links before hashing.
    if (!selected.payloadSourceIdentity || sha256(file) !== selected.payloadSourceIdentity)
      throw appError('ERR_PAYLOAD_SOURCE_CHANGED', { path: payloadDir, file: 'bundle.json' });
  }

  function inspectCurrentPayload(options = {}) {
    const external = Boolean(store.read().payloadSourcePath);
    const bundledAvailable = fs.existsSync(path.join(bundledPayloadDir, 'bundle.json'));
    const mode = external ? 'external' : bundledAvailable ? 'bundled' : 'unconfigured';
    try {
      assertSourceIdentity();
      const inspected = payloadInspection.inspect(payloadDir, options);
      const sourceError = !inspected.ready && mode !== 'unconfigured'
        ? normalizeError(appError(inspected.missing.length ? 'ERR_PAYLOAD_MISSING' : 'ERR_PAYLOAD_HASH',
          { path: payloadDir, files: inspected.missing.length ? inspected.missing : inspected.invalid })) : null;
      return { ...inspected, source: { mode, bundledAvailable, path: mode === 'unconfigured' ? '' : payloadDir,
        ready: Boolean(inspected.bundle && inspected.ready), error: sourceError } };
    } catch (error) {
      return { dir: payloadDir, bundle: null, versions: {}, selectedVersion: null, files: [], ready: false,
        missing: [], invalid: [], source: { mode, bundledAvailable, path: mode === 'unconfigured' ? '' : payloadDir, ready: false, error: normalizeError(error) } };
    }
  }

  function payloadState() {
    return { settings: store.read(), payload: inspectCurrentPayload({ allowMissingBundle: true, hardwareFamily: hardware.family, version: selectedVersion() }), addons: listAddonVersions() };
  }

  async function selectPayloadSource(selectedPath) {
    const directory = selectedPath === null ? bundledPayloadDir : resolvePayloadDirectory(selectedPath);
    readBundle(directory);
    const identity = sha256(path.join(directory, 'bundle.json'));
    const inspected = inspectSource(directory, { hardwareFamily: hardware.family });
    readBundle(directory);
    if (sha256(path.join(directory, 'bundle.json')) !== identity) throw appError('ERR_PAYLOAD_SOURCE_CHANGED', { path: directory });
    const current = store.read();
    await store.write({ payloadSourcePath: selectedPath === null ? null : directory,
      payloadSourceIdentity: selectedPath === null ? null : identity,
      addonVersion: inspected.versions && current.addonVersion && !inspected.versions[current.addonVersion] ? null : current.addonVersion });
    payloadDir = directory;
    payloadInspection.invalidate();
    return payloadState();
  }

  // Keep internal IDs stable while presenting public 0.4 releases as Beta0.4.
  const displayVersionLabel = value => typeof value === 'string'
    ? value.replace(/\bbeta(?=0\.4(?:\.|\D|$))/ig, 'Beta')
    : value;

  const pathKey = value => path.resolve(value).toLowerCase();
  const isInside = (file, root) => {
    const candidate = pathKey(file);
    const parent = pathKey(root);
    return candidate === parent || candidate.startsWith(`${parent}${path.sep}`);
  };

  const canBeRestoredBy = (row, root, executable = null) => {
    if (!row || typeof row !== 'object') return false;
    const same = value => typeof value === 'string' && pathKey(value) === pathKey(root);
    const nested = value => typeof value === 'string' && isInside(value, root);
    return same(row.dir) || same(row.executable) || nested(row.executable) || (executable && same(row.executable));
  };

  const removeExcludedFor = (rows, root, executable = null) =>
    (Array.isArray(rows) ? rows : []).filter(row => !canBeRestoredBy(row, root, executable));

  function executableAliases(state, root, executable) {
    const roots = new Map([[pathKey(root), root]]);
    const sameExe = value => typeof value === 'string' && executable && pathKey(value) === pathKey(executable);
    const add = dir => {
      if (typeof dir === 'string' && path.isAbsolute(dir) && executable && isInside(executable, dir)) roots.set(pathKey(dir), path.resolve(dir));
    };
    for (const game of games) if (sameExe(game.scan?.chosen?.path || game.chosen?.path)) {
      add(game.dir); for (const alias of game.rootAliases || []) add(alias);
    }
    for (const row of state.manualExecutables) if (sameExe(row.file)) add(row.root);
    for (const [dir, row] of Object.entries(state.gameOverrides)) if (sameExe(row.apiExecutable)) add(dir);
    for (const row of state.excludedGames) if (sameExe(row.executable)) add(row.dir);
    const otherExecutables = [
      ...state.manualExecutables.map(row => row.file), ...Object.values(state.gameOverrides).map(row => row.apiExecutable),
      ...games.map(game => game.scan?.chosen?.path || game.chosen?.path)
    ].filter(file => typeof file === 'string' && !sameExe(file));
    const protectedRoots = new Set([...roots.keys()].filter(key => key !== pathKey(root) && fs.existsSync(manifestPath(roots.get(key)))));
    const sharedRoots = new Set([...roots.keys()].filter(key => otherExecutables.some(file => isInside(file, roots.get(key)))));
    const ownsRoot = dir => roots.has(pathKey(dir)) && !protectedRoots.has(pathKey(dir));
    const ownsMetadata = (dir, row) => ownsRoot(dir) && (!row?.apiExecutable || sameExe(row.apiExecutable)) &&
      (!sharedRoots.has(pathKey(dir)) || sameExe(row?.apiExecutable));
    return { roots, sameExe, protectedRoots, sharedRoots, ownsRoot, ownsMetadata };
  }

  function selectedVersion() {
    return store.read().addonVersion || undefined;
  }

  function recommendedCompatVersion() {
    try {
      const bundle = readBundle(payloadDir);
      if (bundle.versions && bundle.versions[bundle.defaultVersion]?.compatibility === 'dx11') return bundle.defaultVersion;
    } catch {}
    return DX11_COMPAT_VERSION;
  }

  function versionChoice(game, requestedVersion = null) {
    const selected = resolveVersion({
      game,
      requestedVersion,
      globalVersion: selectedVersion(),
      dx11Version: recommendedCompatVersion(),
      dx11Only: Boolean(game && game.scan && isDx11Only(game.scan.chosen))
    });
    if (!selected || /^imported-/i.test(selected)) return { version: selected, replacement: null };
    assertSourceIdentity();
    const bundle = readBundle(payloadDir), map = bundle.supersededVersions;
    if (store.read().payloadSourcePath && bundle.versions && !bundle.versions[selected] && !map?.[selected])
      throw appError('ERR_ADDON_NOT_FOUND', { reason: 'version-not-in-selected-source', requestedVersion: selected });
    if (map === undefined) return { version: selected, replacement: null };
    if (!map || typeof map !== 'object' || Array.isArray(map) || Object.keys(map).length > 64 ||
        Object.entries(map).some(([from, to]) => !from || typeof to !== 'string' || !to || !bundle.versions?.[to])) throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' });
    let current = selected; const seen = new Set();
    while (Object.hasOwn(map, current)) {
      if (seen.has(current) || seen.size >= 16) throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' });
      seen.add(current); current = map[current];
    }
    if (current === selected) return { version: selected, replacement: null };
    const replacement = { from: selected, to: current, reason: 'superseded' };
    if (requestedVersion) throw appError('ERR_ADDON_NOT_FOUND', { reason: 'superseded', requestedVersion: selected, replacementVersion: current });
    return { version: current, replacement };
  }

  function selectedVersionForGame(game, requestedVersion = null) {
    // API chooses the deployment route; it does not override an explicit core
    // selection. Superseded implicit selections move to the reviewed replacement;
    // an explicit request is rejected above instead of silently changing versions.
    return versionChoice(game, requestedVersion).version;
  }

  function selectedPayload(game, requestedVersion = null) {
    assertSourceIdentity();
    const choice = versionChoice(game, requestedVersion), version = choice.version;
    if (/^imported-/i.test(version || '')) throw appError('ERR_ADDON_NOT_FOUND', { reason: 'Install a bundled base before applying an imported update.' });
    const result = requirePayload(payloadDir, (hardware = detectGpu()).family, version);
    if (requestedVersion && result.version !== requestedVersion) throw appError('ERR_ADDON_NOT_FOUND');
    result.replacement = choice.replacement;
    return result;
  }

  function importedAddon(version) {
    if (!version || typeof version !== 'string') return null;
    const dir = path.join(addonVersionsDir, version);
    if (!/^imported-[a-f0-9]{12}$/i.test(version) || !fs.existsSync(dir)) return null;
    const metaFile = path.join(dir, 'meta.json');
    const addonFile = path.join(dir, 'nr-before-sr.zh-CN.addon64');
    try {
      const meta = JSON.parse(fs.readFileSync(metaFile, 'utf8'));
      if (!fs.existsSync(addonFile) || !meta || meta.id !== version) return null;
      return {
        id: version,
        label: displayVersionLabel(meta.label || version),
        notes: meta.kind === 'ota'
          ? `OTA 更新包；替换配套核心与 nrchain，DX11 路线使用包内配套 carrier，DX12 路线不部署 carrier。保留 INI、ReShade、Runtime 和游戏自带 DLSS。`
          : '仅替换 Addon；保留当前已安装的 ReShade、Bridge、Runtime 和配置。',
        source: 'imported',
        addonOnly: true,
        ota: meta.kind === 'ota',
        compatibility: meta.compatibility || null,
        otaManifest: meta.otaManifest || null,
        ready: true,
        file: addonFile,
        bridgeFile: fs.existsSync(path.join(dir, 'nrchain_nvngx.dll'))
          ? path.join(dir, 'nrchain_nvngx.dll')
          : null,
        carrierFile: meta.carrierName && path.basename(meta.carrierName) === meta.carrierName && fs.existsSync(path.join(dir, meta.carrierName))
          ? path.join(dir, meta.carrierName) : null,
        addonSha256: meta.addonSha256 || (meta.kind !== 'ota' ? meta.sha256 : null),
        bridgeSha256: meta.bridgeSha256 || (meta.otaManifest && meta.otaManifest.bridgeSha256) || null,
        carrierSha256: meta.carrierSha256 || null,
        importedAt: meta.importedAt || null,
        sourceName: meta.sourceName || path.basename(addonFile)
      };
    } catch {
      return null;
    }
  }

  function importedAddons() {
    let names = [];
    try { names = fs.readdirSync(addonVersionsDir); } catch { return []; }
    return names.map(name => importedAddon(name)).filter(Boolean);
  }

  function listAddonVersions() {
    let inspected;
    try {
      inspected = inspectCurrentPayload({
        allowMissingBundle: true,
        hardwareFamily: hardware.family,
        version: selectedVersion()
      });
    } catch {
      inspected = null;
    }
    const bundled = inspected && inspected.versions
      ? Object.entries(inspected.versions).map(([id, item]) => ({
          id,
          label: displayVersionLabel(item.label || id),
          notes: item.notes || '',
          source: store.read().payloadSourcePath ? 'external' : 'bundled',
          compatibility: item.compatibility || null,
          ota: Boolean(item.ota),
          addonOnly: false,
          ready: Boolean(item.variants && ['RTX40', 'RTX50'].some(family => item.variants[family] && item.variants[family].ready)),
          deletable: false
        }))
      : [];
    return [...bundled, ...importedAddons().map(item => ({ ...item, deletable: true }))];
  }

  function inspectGamePayload(game, requestedVersion = null) {
    const family = hardware.family;
    const choice = versionChoice(game, requestedVersion), version = choice.version;
    const imported = importedAddon(version);
    if (/^imported-/i.test(version || '') && !imported) throw appError('ERR_ADDON_NOT_FOUND');
    if (!imported) {
      const payload = inspectCurrentPayload({
        allowMissingBundle: true,
        hardwareFamily: family,
        version
      });
      return {
        payload,
        payloadVersion: payload.selectedVersion,
        payloadVersionReplacement: choice.replacement,
        payloadVersionLabel: payload.versions && payload.selectedVersion
          ? payload.versions[payload.selectedVersion] && payload.versions[payload.selectedVersion].label
          : null
      };
    }

    // An imported OTA owns the addon (and, when present, its paired bridge),
    // while runtime/ReShade/config remain game-install state. Do not compare
    // those preserved files against whichever bundled version is selected.
    const base = inspectCurrentPayload({
      allowMissingBundle: true,
      hardwareFamily: family
    });
    const importedAddonHash = sha256(imported.file);
    const importedBridgeHash = imported.bridgeFile ? sha256(imported.bridgeFile) : null;
    const importedCarrierHash = imported.carrierFile ? sha256(imported.carrierFile) : null;
    const files = (base.files || []).map(row => {
      if (row.kind === 'addon') {
        return { ...row, file: imported.file, actual: importedAddonHash, expected: imported.addonSha256 || importedAddonHash, exists: true, valid: !imported.addonSha256 || importedAddonHash === imported.addonSha256 };
      }
      if (row.kind === 'bridge' && importedBridgeHash) {
        return { ...row, file: imported.bridgeFile, actual: importedBridgeHash, expected: importedBridgeHash, exists: true, valid: true };
      }
      return { ...row, expected: null, valid: row.exists };
    });
    if (importedCarrierHash) files.push({ kind: 'carrier', name: path.basename(imported.carrierFile), file: imported.carrierFile,
      actual: importedCarrierHash, expected: imported.carrierSha256 || importedCarrierHash, exists: true, valid: !imported.carrierSha256 || importedCarrierHash === imported.carrierSha256 });
    return {
      payload: { ...base, files, selectedVersion: imported.id, versionInfo: { compatibility: imported.compatibility } },
      payloadVersion: imported.id,
      payloadVersionLabel: imported.label
    };
  }

  async function diagnoseGame(game) {
    if (externalOwned(game) || fs.existsSync(path.join(game.dir, EXTERNAL_PENDING))) {
      const state = await externalDeployment.inspect(game);
      return { payload: { selectedVersion: state.version, ready: state.ready }, diagnostic: {
        installed: true, complete: state.ready, payloadVersion: state.version, version: state.version,
        deployment: state, runtimeVerified: false, routeMismatch: state.api !== classifyApi(game.scan?.chosen),
        components: [
          { key: 'deployment', label: '运行目录', ok: state.verified && !state.pending,
            detail: state.pending ? '外置部署未完成，请先恢复。' : state.runtimeDir || state.blockers?.join('；') },
          ...state.files.map(row => ({ key: row.kind, label: row.name, file: path.join(state.runtimeDir || '', row.name),
            ok: row.valid, detail: row.valid ? row.mutable ? '当前个人配置' : '摘要与部署记录一致' : row.reason || '文件缺失或摘要改变' }))
        ] } };
    }
    if (feederOwned(game)) {
      const raw = await feeder.inspect(game);
      return { payload: { selectedVersion: raw.coreVersion, ready: raw.available }, diagnostic: {
        complete: raw.ready === true, installed: raw.installed, pending: raw.needsRecovery, deploymentApi: 'dx12',
        enhancementRoute: 'feeder-dx12', runtimeVerified: false, blockers: raw.blockers || (raw.reason ? [raw.reason] : []),
        components: [{ key: 'feeder-files', label: '无 DLSS 固定配套', ok: raw.ready === true, detail: raw.reason || '组件校验通过；成品帧后处理，Synthetic 导引' },
          ...(raw.antiCheatDetected ? [{ key: 'feeder-protection', label: '反作弊启动兼容性', ok: null, detail: raw.launchWarning }] : []),
          ...(raw.retainedFiles?.length ? [{ key: 'feeder-retained', label: '安装前已有组件', ok: true, detail: `${raw.retainedFiles.join('、')} 为复用文件，卸载本次 Feeder 时会保留。` }] : []),
          { key: 'feeder-loaded', label: '本次加载', ok: raw.loaded === true ? true : null, detail: '加载状态与文件准备分别确认' },
          { key: 'feeder-processed', label: '本次 NR 完成', ok: raw.processed === true ? true : null, detail: raw.detail || '需启动后核对本次完成记录；不代表画质验收' }]
      } };
    }
    if (vulkanOwned(game) || vulkanRoute(game) && !readManifest(game.dir)) {
      const raw = await vulkan.diagnose(game), checks = raw.components || {};
      const components = [
        { key: 'vulkan-profile', label: '外部运行组件', ok: checks.profile?.ready === true, detail: checks.profile?.ready ? '配套核心与文件校验通过' : checks.profile?.error?.message || '运行组件需要准备或修复' },
        { key: 'vulkan-layer', label: 'Vulkan 加载层', ok: checks.deployment?.ready === true, detail: checks.deployment?.ready ? '注册与共享引用完整' : checks.deployment?.blockers?.join('；') || '加载层尚未就绪' },
        { key: 'vulkan-activation', label: '按游戏激活', ok: checks.activation?.active === true, detail: checks.activation?.active ? '绑定当前 EXE 的外部运行目录' : '尚未完成激活' },
        { key: 'vulkan-loaded', label: '本次加载', ok: raw.loaded === true ? true : null, detail: raw.loaded === true ? '本次运行日志已确认' : '待启动游戏验证' },
        { key: 'vulkan-processed', label: 'NR 处理', ok: raw.processed === true ? true : null, detail: raw.detail || (raw.processed === true ? '本次日志已确认 NR 完成；画面效果请在游戏内核对' : '尚未确认，文件检查不代表增强已生效') }
      ];
      return { payload: { selectedVersion: vulkan.summary(game).coreVersion, ready: vulkan.summary(game).available },
        diagnostic: { complete: raw.ready === true, components, deploymentApi: 'vulkan', pending: raw.pending,
          installed: raw.installed, runtimeVerified: raw.runtimeVerified, blockers: raw.blockers || [] } };
    }
    let inspected;
    try { inspected = inspectGamePayload(game); }
    catch (error) {
      const manifest = readManifest(game.dir);
      if (!manifest || !/^(?:ERR_ADDON_NOT_FOUND|ERR_PAYLOAD_[A-Z_]+)$/.test(error.code || '')) throw error;
      const executableDir = path.dirname(assertManifestExecutable(game.dir, manifest));
      const currentFiles = manifest.files.filter(row => row && typeof row.kind === 'string' && typeof row.rel === 'string' &&
        !path.isAbsolute(row.rel) && /^[a-f0-9]{64}$/i.test(row.installedSha256 || '') && Object.hasOwn(INSTALLED_NAMES, row.kind) &&
        pathKey(path.resolve(game.dir, row.rel)) === pathKey(path.join(executableDir, INSTALLED_NAMES[row.kind])));
      const expected = Object.fromEntries(currentFiles.map(row => [row.kind, { actual: row.installedSha256 }]));
      expected.versionInfo = { compatibility: manifest.deploymentApi === 'dx11' || currentFiles.some(row => row.kind === 'carrier') ? 'dx11' : null };
      const result = await installer.diagnose({ gameDir: game.dir, payload: expected, payloadVersion: manifest.payloadVersion,
        payloadVersionLabel: /^imported-/.test(manifest.payloadVersion || '') ? '已装导入核心（原包需重新导入）' : manifest.payloadVersion, scan: game.scan });
      return {
        payload: { selectedVersion: manifest.payloadVersion, ready: false, missing: ['原安装源不可用'], invalid: [] },
        diagnostic: { ...result, complete: false, sourceMissing: true, components: [...result.components, { key: 'payload-source', label: '修复组件来源', ok: false,
          detail: '已按安装记录检查现有文件。原安装源不可用；可重新导入原包，或选择可用核心并点击“应用版本”。卸载恢复不依赖原包。' }] }
      };
    }
    const payload = inspected.payload;
    const mapped = Object.fromEntries(payload.files.map(row => [row.kind, row]));
    mapped.versionInfo = payload.versionInfo || (payload.versions && payload.versions[payload.selectedVersion]);
    const result = await installer.diagnose({ gameDir: game.dir, payload: mapped, payloadVersion: inspected.payloadVersion,
      payloadVersionLabel: inspected.payloadVersionLabel, scan: game.scan });
    return { payload, diagnostic: inspected.payloadVersionReplacement ? { ...result, payloadReplacement: inspected.payloadVersionReplacement } : result };
  }

  async function repairGame(game, options = {}) {
      if (externalOwned(game)) {
        const payload = await externalPayload(game, options.version);
        const plan = await externalDeployment.preview(game, { mode: 'external', payload, version: payload.version });
        return externalDeployment.apply(plan.planId, options);
      }
      if (feederOwned(game)) return feeder.install(game, options);
      requireNoFeeder(game);
      if (vulkanOwned(game) || vulkanRoute(game)) return vulkan.install(game, options);
      const requestedVersion = options && typeof options.version === 'string' ? options.version : null;
      const version = selectedVersionForGame(game, requestedVersion);
      const imported = importedAddon(version);
      if (/^imported-/i.test(version || '') && !imported) throw appError('ERR_ADDON_NOT_FOUND');
      if (imported) {
        await prepareExistingReframework(game, options);
        const result = await installer.upgradeAddon({ gameDir: game.dir, addon: imported, version: imported.id, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
        return prepareDetectedReframework(game, result);
      }
      const payload = selectedPayload(game, requestedVersion);
      await prepareExistingReframework(game, options);
      const result = await installer.repair({ gameDir: game.dir, payload, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return prepareDetectedReframework(game, payload.replacement ? { ...result, payloadReplacement: payload.replacement } : result);
  }

  function publicGame(game) {
    if (!game) return null;
    const { scan, ...safe } = game;
    const vk = vulkan.summary(game);
    const feed = feederSelectionSummary(game, feeder.summary(game), vk);
    const enhancements = inspectNativeEnhancementCapabilities(scan);
    const routed = vulkanRoute(game);
    const refInput = reframeworkInput(game);
    const refAdapter = refInput && REFRAMEWORK_ADAPTERS.find(row => row.executable.toLowerCase() === path.basename(refInput.exe).toLowerCase());
    let deployment = null;
    if (hasExternalRecord(game)) try { deployment = externalDeployment.getLayout(game); }
    catch (error) { deployment = { mode: 'external', verified: false, needsRecovery: true, blockers: [error.message] }; }
    return { ...safe, vulkan: vk, feeder: feed, deployment,
      ...(deployment?.mode === 'external' ? { installed: true, addonVersion: deployment.version || safe.addonVersion } : {}),
      enhancementRoute: feed.installed ? 'feeder-dx12' : vk.installed ? 'vulkan' : safe.installed ? 'native-dlss' : null,
      hasNativeDlss: enhancements.nativeDlssAvailable,
      nativeFgAvailable: enhancements.nativeFgAvailable,
      enhancementCapabilities: enhancements,
      ...(safe.chosen?.apiSettings ? { chosen: { ...safe.chosen, apiSettings: {
        kind: safe.chosen.apiSettings.kind, api: safe.chosen.apiSettings.api,
        canSync: safe.chosen.apiSettings.kind === 'rdr2-system-xml' && Boolean(safe.chosen.apiSettings.sha256)
      } } } : {}),
      ...(refAdapter ? { reframework: { matched: true, label: '卡普空 RE 引擎兼容', profile: refAdapter.id } } : {}),
      ...(routed ? { supported: vk.available === true, supportCode: vk.available ? null : 'VULKAN_UNAVAILABLE', supportText: vk.reason || 'Vulkan 桥接 · 试验版' } : {}),
      ...(vk.installed ? { installed: true, addonVersion: vk.coreVersion } : {}),
      ...(feed.installed ? { installed: true, addonVersion: feed.coreVersion } : {}),
      nativeDlssAvailable: enhancements.nativeDlssAvailable,
      recommendedAddonVersion: safe.recommendedAddonVersion ? recommendedCompatVersion() : null };
  }

  function findGame(id) {
    const game = games.find(row => row.id === id);
    if (!game) throw appError('ERR_UNKNOWN_GAME');
    return game;
  }

  async function rollbackApiChoice(error, state, key, settingChange) {
    const current = store.read(), previous = state.gameOverrides[key] || {};
    try { await store.write({ gameOverrides: { ...current.gameOverrides,
      [key]: { ...current.gameOverrides[key], api: previous.api || 'auto', apiExecutable: previous.apiExecutable || null } } }); }
    catch { error.details = { ...error.details, preferenceRollbackFailed: true }; }
    if (settingChange?.changed) try {
      const restored = await settingChange.rollback();
      if (!restored.rolledBack) error.details = { ...error.details, gameApiRollbackFailed: true, gameApiExternalChangeRetained: true };
    } catch (recoveryError) { error.details = { ...error.details, gameApiRollbackFailed: true }; error.recoveryError = recoveryError; }
  }

  async function applyGameRoute(id, options = {}, internal = {}) {
    if (!options || typeof options !== 'object' || Array.isArray(options) ||
        Object.keys(options).some(key => !['api', 'version', 'allowAntiCheat'].includes(key)) ||
        !['auto', 'dx11', 'dx12', 'vulkan'].includes(options.api) ||
        typeof options.version !== 'string' || !options.version || options.version.length > 100) throw appError('ERR_BAD_REQUEST');
    const game = findGame(id), chosen = game.scan?.chosen;
    if (!chosen) throw appError('ERR_NO_GAME_EXE');
    requireNoFeeder(game);
    requireKnownVulkanOwnership(game);
    let detected = chosen.detectedApiResolution || { api: chosen.detectedApi || 'unknown' };
    if (options.api === 'auto' && chosen.apiSettings?.kind === 'rdr2-system-xml') {
      const current = apiSettingsReader.read({ ...chosen.apiSettings, exe: chosen.path });
      detected = { api: current.api || 'unknown', source: 'game-settings', evidence: [] };
    }
    const nextApi = options.api === 'auto' ? detected.api : options.api;
    if (!['dx11', 'dx12', 'vulkan'].includes(nextApi)) throw appError('ERR_API_SELECTION_REQUIRED');
    if (Array.isArray(chosen.supportedApis) && !chosen.supportedApis.includes(nextApi)) throw appError('ERR_UNSUPPORTED_API');
    const manifest = readManifest(game.dir), ownedVulkan = vulkanOwned(game);
    if (ownedVulkan && nextApi !== 'vulkan' || manifest && nextApi === 'vulkan') routeRestoreFirst();
    const resolution = { api: nextApi, source: options.api === 'auto' ? detected.source : 'override', evidence: detected.evidence || [] };
    const routed = { ...game, apiOverride: options.api,
      scan: { ...game.scan, chosen: { ...chosen, apiResolution: resolution }, componentSelection: { dx11Carrier: nextApi === 'dx11' } } };
    let payload = null;
    const imported = /^imported-/.test(options.version) ? importedAddon(options.version) : null;
    if (nextApi === 'vulkan') {
      const info = vulkan.summary(routed);
      if (options.version !== info.packageId) throw Object.assign(new Error('所选 Core 不属于当前 Vulkan 配套，请核对 API 和配套版本后一起应用。'), { code: 'VULKAN_PACKAGE_MISMATCH' });
    } else {
      if (/^imported-/.test(options.version) && (!imported || !manifest)) throw appError('ERR_ADDON_NOT_FOUND');
      if (!imported) payload = selectedPayload(routed, options.version);
      const support = assess(routed.scan, { allowDx11: (imported?.compatibility || payload?.versionInfo?.compatibility) === 'dx11' });
      if (!support.supported) throw appError(support.code);
    }
    const state = store.read(), key = pathKey(game.dir);
    let settingChange, preferenceWritten = false, result;
    try {
      settingChange = await gameApiSettings.apply(game, options.api);
      await store.write({ gameOverrides: { ...state.gameOverrides,
        [key]: { ...state.gameOverrides[key], api: options.api, apiExecutable: chosen.path } } });
      preferenceWritten = true;
      if (nextApi === 'vulkan') result = await vulkan.install(routed, options);
      else if (imported || externalOwned(game)) result = await repairGame(routed, options);
      else {
        await prepareExistingReframework(routed, options);
        const operation = manifest ? installer.repair : installer.install;
        result = await operation({ gameDir: game.dir, payload, scan: routed.scan, allowAntiCheat: options.allowAntiCheat === true });
        result = await prepareDetectedReframework(routed, result);
      }
    } catch (error) {
      if (preferenceWritten || settingChange?.changed) await rollbackApiChoice(error, state, key, settingChange);
      throw error;
    }
    internal.onAppliedState?.({ state, key, settingChange });
    return refreshAfterMutation({ ...result, appliedRoute: { api: nextApi, version: nextApi === 'vulkan' ? options.version : payload?.version || imported?.id,
      gameSettingsSynced: settingChange?.applied === true } });
  }

  function installedExecutable(game) {
    const manifest = readManifest(game.dir);
    if (!manifest) throw appError('ERR_NOT_INSTALLED');
    return assertManifestExecutable(game.dir, manifest, game.scan?.chosen?.path);
  }

  function refresh() {
    const state = store.read(), epoch = scanEpoch, key = JSON.stringify([state, epoch]);
    if (activeRefresh?.key === key) return activeRefresh.promise;
    const generation = ++refreshGeneration;
    const promise = Promise.resolve().then(() => library.scanAll(state, epoch)).then(rows => {
      if (generation !== refreshGeneration) return activeRefresh ? activeRefresh.promise : games.map(publicGame);
      latestScanWarnings = Array.isArray(rows.discoveryWarnings) ? rows.discoveryWarnings : [];
      games = rows; collectionFresh = true;
      return games.map(publicGame);
    }, error => {
      if (generation !== refreshGeneration) return activeRefresh ? activeRefresh.promise : games.map(publicGame);
      collectionFresh = false; throw error;
    }).finally(() => { if (activeRefresh?.generation === generation) activeRefresh = null; });
    activeRefresh = { key, generation, promise };
    return promise;
  }

  async function refreshCollection(fallback = games) {
    collectionFresh = false; scanEpoch++;
    try { return await refresh(); } catch { return fallback; }
  }

  async function refreshAfterMutation(value) {
    // The journal transaction has already committed. A transient scan failure
    // must not turn that successful mutation into an IPC error; the renderer
    // retries the cache refresh separately.
    collectionFresh = false; scanEpoch++;
    try { await refresh(); } catch {}
    return value;
  }

  async function withError(work, context = {}) {
    try {
      const value = await work();
      if (!QUIET_READ_ACTIONS.has(context.action)) await feedback.record({ ...context, ok: true });
      return { ok: true, value };
    } catch (error) {
      const normalized = normalizeError(error);
      const needsConfirmation = normalized.code === 'ERR_ANTI_CHEAT_CONFIRM';
      await feedback.record({
        ...context,
        ok: false,
        ...(needsConfirmation ? { outcome: '需确认' } : {}),
        errorCode: normalized.code,
        errorMessage: normalized.message,
        details: normalized.details
      });
      return { ok: false, error: normalized };
    }
  }

  async function addManualSelection(selection) {
    if (!selection || typeof selection !== 'object' || typeof selection.root !== 'string' || typeof selection.executable !== 'string') {
      throw appError('ERR_BAD_REQUEST');
    }
    const root = path.resolve(selection.root);
    const executable = path.resolve(selection.executable);
    if (!path.isAbsolute(root) || !path.isAbsolute(executable) || !isInside(executable, root) ||
        !/\.exe$/i.test(executable) || !fs.existsSync(executable)) {
      throw appError('ERR_BAD_REQUEST');
    }
    const state = store.read();
    const aliases = executableAliases(state, root, executable);
    const manualExecutables = state.manualExecutables.filter(row => pathKey(row.root) !== pathKey(root) &&
      !(aliases.ownsRoot(row.root) && aliases.sameExe(row.file)));
    manualExecutables.push({ root, file: executable });
    const remembered = [...aliases.roots.keys()].filter(key => aliases.ownsMetadata(key, state.gameOverrides[key]))
      .map(key => state.gameOverrides[key]).filter(Boolean);
    // Prefer the canonical root, then fill missing display metadata from an
    // exact-EXE alias. Never carry a route remembered for another executable.
    const canonical = state.gameOverrides[pathKey(root)];
    const metadata = [canonical, ...remembered].filter(row => row && (!row.apiExecutable || aliases.sameExe(row.apiExecutable)));
    const boundApi = metadata.find(row => aliases.sameExe(row.apiExecutable));
    const name = typeof selection.name === 'string' ? selection.name.trim().slice(0, 160) : '';
    const icon = typeof selection.icon === 'string' && selection.icon.length <= 1024 * 1024 ? selection.icon : null;
    const gameOverrides = Object.fromEntries(Object.entries(state.gameOverrides).filter(([dir, row]) => !aliases.ownsMetadata(dir, row)));
    gameOverrides[pathKey(root)] = {
      name: name || metadata.find(row => row.name)?.name || '',
      icon: icon || metadata.find(row => row.icon)?.icon || null,
      api: boundApi?.api || 'auto', apiExecutable: boundApi ? executable : null
    };
      await store.write({
        manualGames: [...state.manualGames.filter(dir => !aliases.ownsRoot(dir) || aliases.sharedRoots.has(pathKey(dir))), root],
        manualExecutables,
        gameOverrides,
        excludedRoots: state.excludedRoots.filter(dir => !aliases.ownsRoot(dir) || aliases.sharedRoots.has(pathKey(dir))),
        excludedGames: state.excludedGames.filter(row => !aliases.sameExe(row.executable) &&
          !(row.dir && !row.executable && aliases.ownsRoot(row.dir) && !aliases.sharedRoots.has(pathKey(row.dir))))
      });
    return refreshCollection(games);
  }

  async function validateLaunch(id) {
    const game = findGame(id);
    if (!game.scan || !game.scan.chosen || !game.scan.chosen.path) throw appError('ERR_NO_GAME_EXE');
    if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
    if (externalOwned(game)) {
      const state = await externalDeployment.inspect(game);
      if (!state.ready) throw Object.assign(new Error(state.blockers?.join('；') || '外置组件未就绪，请先检查或修复。'), { code: 'DEPLOYMENT_NOT_READY' });
      if (state.api !== classifyApi(game.scan.chosen)) throw appError('ERR_API_ROUTE_PENDING');
    } else if (feederOwned(game)) {
      // The Feeder service checks its fixed package and ordinary-privilege
      // launch immediately before process creation, independently of SR/FG.
    } else if (vulkanOwned(game)) {
      if (!vulkanRoute(game)) routeRestoreFirst();
      // The Vulkan launch operation verifies the immutable profile and layer
      // once, immediately before creating its ordinary-privilege process.
    } else if (game.installed) {
      if (vulkanRoute(game)) routeRestoreFirst();
      const diagnostic = await installer.diagnose({ gameDir: game.dir, scan: game.scan });
      if (['mixed', 'unknown'].includes(classifyApi(game.scan.chosen))) throw appError('ERR_API_SELECTION_REQUIRED');
      if (diagnostic.routeMismatch) throw appError('ERR_API_ROUTE_PENDING');
    }
    const exe = game.scan.chosen.path;
    if (typeof exe !== 'string' || !path.isAbsolute(exe) || path.extname(exe).toLowerCase() !== '.exe' || !fs.existsSync(exe)) throw appError('ERR_NO_GAME_EXE');
    return exe;
  }

  function uninstallRequest(request) {
    const value = typeof request === 'boolean' ? { removeSettings: request, mode: 'restore' } : request || {};
    if (typeof value !== 'object' || Array.isArray(value) ||
        Object.keys(value).some(key => !['mode', 'removeSettings'].includes(key)) ||
        value.mode !== undefined && !['restore', 'clean'].includes(value.mode) ||
        value.removeSettings !== undefined && typeof value.removeSettings !== 'boolean') throw appError('ERR_BAD_REQUEST');
    return { mode: value.mode || 'restore', removeSettings: value.removeSettings === true };
  }
  async function externalPayload(game, requested) {
    const state = await externalDeployment.inspect(game), version = requested || state.version;
    const imported = importedAddon(version);
    if (!imported) return selectedPayload(game, version);
    if (state.api === 'dx11' && (imported.compatibility !== 'dx11' || !imported.carrierFile || !imported.bridgeFile))
      throw appError('ERR_PAYLOAD_MISSING', { file: 'DX11 配套核心与桥接器' });
    const payload = { version: imported.id, versionInfo: { compatibility: imported.compatibility } };
    for (const kind of ['addon', 'bridge', 'runtime', 'carrier']) {
      const current = state.files.find(row => row.kind === kind); if (!current) continue;
      const provided = kind === 'addon' ? imported.file : kind === 'bridge' ? imported.bridgeFile : kind === 'carrier' ? imported.carrierFile : null;
      const expected = kind === 'addon' ? imported.addonSha256 : kind === 'bridge' ? imported.bridgeSha256 : kind === 'carrier' ? imported.carrierSha256 : null;
      payload[kind] = { file: provided || path.join(state.runtimeDir, current.name), actual: provided ? expected : current.sha256 };
    }
    return payload;
  }
  function deploymentApi(game, requested) {
    const chosen = game.scan?.chosen;
    if (!chosen) throw appError('ERR_NO_GAME_EXE');
    let detected = chosen.detectedApiResolution || { api: chosen.detectedApi || classifyApi(chosen) };
    if (requested === 'auto' && chosen.apiSettings?.kind === 'rdr2-system-xml') {
      const current = apiSettingsReader.read({ ...chosen.apiSettings, exe: chosen.path });
      detected = { api: current.api || 'unknown', source: 'game-settings', evidence: [] };
    }
    const api = requested === 'auto' ? detected.api : requested || classifyApi(chosen);
    if (!['dx11', 'dx12'].includes(api)) throw appError('ERR_API_SELECTION_REQUIRED');
    if (chosen.supportedApis && !chosen.supportedApis.includes(api)) throw appError('ERR_UNSUPPORTED_API');
    return { api, routed: { ...game, scan: { ...game.scan, chosen: { ...chosen, apiResolution: { api, source: requested === 'auto' ? detected.source : 'override',
      evidence: detected.evidence || [] } }, componentSelection: { dx11Carrier: api === 'dx11' } } } };
  }
  async function previewDeployment(id, request = {}, internal = {}) {
    if (!request || typeof request !== 'object' || Array.isArray(request) ||
        Object.keys(request).some(key => !['mode', 'version', 'api', 'loadingMode'].includes(key)) ||
        !['local', 'external'].includes(request.mode) || request.api !== undefined && !['auto', 'dx11', 'dx12', 'vulkan'].includes(request.api) ||
        request.loadingMode !== undefined && !['proxy', 'helper'].includes(request.loadingMode))
      throw appError('ERR_BAD_REQUEST');
    const game = findGame(id);
    if (feederOwned(game) || vulkanOwned(game) || request.api === 'vulkan' || vulkanRoute(game) && request.api === undefined) {
      if (request.loadingMode === 'helper') throw Object.assign(new Error('Vulkan 和 Feeder 固定配套使用各自的加载方式。'), { code: 'SPECIAL_LOADING_MODE_LOCKED' });
      return previewSpecialDeployment(id, { route: feederOwned(game) ? 'feeder' : 'vulkan', api: request.api, version: request.version });
    }
    requireNoFeeder(game); requireKnownVulkanOwnership(game);
    if (vulkanOwned(game)) routeRestoreFirst();
    await externalDeployment.assertReady(game);
    const state = store.read(), preference = request.api || game.apiOverride || classifyApi(game.scan?.chosen);
    const { api, routed } = deploymentApi(game, preference);
    const current = await externalDeployment.inspect(game), manifest = readManifest(game.dir);
    const targetVersion = request.version || (current.mode === 'external' ? current.version : selectedVersionForGame(routed));
    const fromApi = current.mode === 'external' ? current.api : manifest?.deploymentApi || classifyApi(game.scan?.chosen);
    const sameApi = fromApi === api;
    let migration = null, migrationRequest = null, phases = [], changes = [], payload = null;
    if (current.mode === 'external' && sameApi) {
      payload = targetVersion !== current.version || request.version ? await externalPayload(routed, targetVersion) : null;
      migrationRequest = { mode: request.mode, loadingMode: request.loadingMode, api, version: targetVersion, payload };
      migration = await externalDeployment.preview(routed, migrationRequest, internal);
      phases = [request.mode === 'external' ? 'external-update' : 'restore-local'];
      changes = migration.changes;
    } else if (manifest && current.mode !== 'external' && request.mode === 'external' && sameApi) {
      payload = targetVersion !== manifest.payloadVersion || request.version ? selectedPayload(routed, targetVersion) : null;
      migrationRequest = { mode: 'external', loadingMode: request.loadingMode, api, version: targetVersion, payload };
      migration = await externalDeployment.preview(routed, migrationRequest, internal);
      phases = ['external-migration']; changes = migration.changes;
    } else {
      if (current.mode === 'external') {
        migrationRequest = { mode: 'local' };
        migration = await externalDeployment.preview(game, migrationRequest, internal);
        phases.push('restore-local'); changes.push(...migration.changes.map(row => ({ ...row, phase: 'restore-local' })));
      }
      payload = selectedPayload(routed, targetVersion);
      const support = assess(routed.scan, { allowDx11: payload.versionInfo?.compatibility === 'dx11' });
      if (!support.supported) throw appError(support.code);
      phases.push(manifest ? 'local-update' : 'local-install');
      const directory = path.dirname(game.scan.chosen.path);
      for (const kind of ['addon', 'bridge', 'runtime', ...(!manifest ? ['reshade', 'config'] : []), ...(api === 'dx11' ? ['carrier'] : [])]) {
        const row = payload[kind]; if (!row) continue;
        const file = path.join(directory, require('./constants').INSTALLED_NAMES[kind]);
        const before = fs.existsSync(file) && fs.statSync(file).isFile() ? sha256(file) : null;
        changes.push({ path: file, name: path.basename(file), role: kind, phase: 'local-install',
          beforeSha256: before, afterSha256: row.actual, action: before === row.actual ? 'keep' : before === null ? 'create' : 'replace' });
      }
      if (request.mode === 'external') {
        phases.push('external-migration');
        const location = externalDeployment.location(routed);
        for (const kind of ['addon', 'bridge', 'runtime', 'config', ...(api === 'dx11' ? ['carrier'] : [])]) {
          const row = payload[kind]; if (!row) continue;
          const file = path.join(location.runtimeDir, require('./constants').INSTALLED_NAMES[kind]);
          changes.push({ path: file, name: path.basename(file), role: kind, phase: 'external-migration',
            beforeSha256: null, afterSha256: row.actual, action: 'create' });
        }
        if (request.loadingMode === 'helper') {
          const existingName = ['dxgi.dll', 'd3d12.dll'].find(name => fs.existsSync(path.join(directory, name)));
          const loaderName = existingName || INSTALLED_NAMES.reshade;
          const loaderHash = existingName ? sha256(path.join(directory, existingName)) : payload.reshade.actual;
          changes.push({ path: path.join(location.runtimeDir, 'ReShade64.dll'), name: 'ReShade64.dll',
            role: 'profile-loader', phase: 'external-migration', beforeSha256: null, afterSha256: loaderHash, action: 'create' });
          changes.push({ path: path.join(directory, loaderName), name: loaderName, role: 'game-proxy', phase: 'external-migration',
            beforeSha256: loaderHash, afterSha256: null, action: 'remove' });
        }
        changes.push({ path: path.join(directory, 'ReShade.ini'), name: 'ReShade.ini', role: 'reshade-config',
          phase: 'external-migration', action: 'update-base-path', destination: location.runtimeDir });
      }
    }
    const planId = crypto.randomUUID();
    deploymentPlans.set(planId, { id, request: { ...request, api: preference, version: targetVersion }, api, routed,
      fromMode: current.mode, fromApi, fromVersion: current.version, hadManifest: Boolean(manifest),
      migration, migrationRequest, phases, expires: Date.now() + 5 * 60 * 1000,
      exe: game.scan.chosen.path, exeHash: await require('./external-runtime').digest(game.scan.chosen.path), preferenceBefore: state });
    const proposedLayout = request.mode === 'external' ? externalDeployment.location(routed) : gameLayout(game);
    const loadingMode = request.mode === 'external' ? request.loadingMode || current.loadingMode || 'proxy' : 'proxy';
    return { planId, gameId: id, fromMode: current.mode, toMode: request.mode, mode: request.mode,
      version: targetVersion, api, loadingMode, phases, changes, layout: migration?.layout || { ...proposedLayout, loadingMode,
        loaderPath: loadingMode === 'helper' ? path.join(proposedLayout.runtimeDir, 'ReShade64.dll') : null },
      retainedAddons: migration?.retainedAddons || [], requiresFgRestore: migration?.requiresFgRestore === true,
      requiresAntiCheat: Boolean(require('../core/install-guards').antiCheatPresent(game.dir)), requiresConfirmation: true, runtimeVerified: false };
  }
  async function applyDeployment(planId, consent = {}) {
    if (specialDeploymentPlans.has(planId)) return applySpecialDeployment(planId, consent);
    const plan = deploymentPlans.get(planId); deploymentPlans.delete(planId);
    if (!plan || plan.expires < Date.now()) throw Object.assign(new Error('部署预览已过期，请重新检查。'), { code: 'DEPLOYMENT_PLAN_EXPIRED' });
    let game = findGame(plan.id), completed = [], result, settingChange, preferenceWritten = false;
    if (game.scan?.chosen?.path !== plan.exe || await require('./external-runtime').digest(plan.exe) !== plan.exeHash)
      throw Object.assign(new Error('预览后游戏 EXE 已变化。'), { code: 'DEPLOYMENT_PLAN_CHANGED' });
    await externalDeployment.assertReady(game);
    const preferenceKey = pathKey(game.dir);
    const preferenceBefore = store.read();
    for (const key of ['api', 'apiExecutable']) if ((preferenceBefore.gameOverrides[preferenceKey]?.[key] || null) !==
        (plan.preferenceBefore.gameOverrides[preferenceKey]?.[key] || null))
      throw Object.assign(new Error('预览后的游戏 API 选择已改变，请重新检查。'), { code: 'DEPLOYMENT_PLAN_CHANGED' });
    if (plan.migration?.requiresFgRestore) {
      plan.migration = await externalDeployment.preview(plan.fromMode === 'external' && plan.fromApi !== plan.api ? game : plan.routed,
        plan.migrationRequest);
    }
    try {
      if (plan.phases.every(phase => ['external-update', 'external-migration', 'restore-local'].includes(phase))) {
        settingChange = await gameApiSettings.apply(game, plan.request.api);
        await store.write({ gameOverrides: { ...preferenceBefore.gameOverrides, [preferenceKey]: {
          ...preferenceBefore.gameOverrides[preferenceKey], api: plan.request.api, apiExecutable: plan.exe } } });
        preferenceWritten = true;
        result = await externalDeployment.apply(plan.migration.planId, consent);
        completed.push(...plan.phases);
      } else {
        if (plan.fromMode === 'external') {
          result = await externalDeployment.apply(plan.migration.planId, consent); completed.push('restore-local');
        }
        result = await applyGameRoute(plan.id, { api: plan.request.api, version: plan.request.version, allowAntiCheat: consent.allowAntiCheat === true },
          { onAppliedState: applied => { settingChange = applied.settingChange; } });
        preferenceWritten = true; completed.push(plan.hadManifest ? 'local-update' : 'local-install');
        if (plan.request.mode === 'external') {
          game = findGame(plan.id);
          const next = await externalDeployment.preview(game, { mode: 'external', loadingMode: plan.request.loadingMode, api: plan.api });
          result = await externalDeployment.apply(next.planId, consent); completed.push('external-migration');
        }
      }
      return refreshAfterMutation({ ...result, applied: true, completedPhases: completed,
        appliedRoute: { api: plan.api, version: plan.request.version, mode: plan.request.mode }, runtimeVerified: false });
    } catch (error) {
      if (preferenceWritten || settingChange?.changed) await rollbackApiChoice(error, preferenceBefore, preferenceKey, settingChange);
      const pending = fs.existsSync(path.join(game.dir, EXTERNAL_PENDING));
      // A newly created ordinary installation is an independently recoverable
      // stage. Revert it only after the external owner has restored its files.
      if (!plan.hadManifest && completed.includes('local-install') && !pending) {
        try {
          const reverted = await installer.uninstall({ gameDir: game.dir, mode: 'restore', removeSettings: false, scan: game.scan });
          if (!reverted?.removed) throw appError('ERR_BACKUP_INVALID');
          completed.push('local-install-reverted');
        } catch (cause) { error.compensationError = cause; }
      }
      error.details = { ...error.details, completedPhases: completed, needsRecovery: pending || Boolean(error.compensationError) };
      throw error;
    }
  }
  async function previewUninstall(id, request = {}, internal = {}) {
    const game = findGame(id), options = uninstallRequest(request);
    if (feederOwned(game) || vulkanOwned(game)) {
      const route = feederOwned(game) ? 'feeder' : 'vulkan';
      const result = await (route === 'feeder' ? feeder : vulkan).previewRestore(game);
      return { ...result, gameId: id, mode: options.mode, phases: [`${route}-restore`],
        changes: result.changes.map(row => ({ ...row, phase: `${route}-restore` })), requiresFgRestore: false };
    }
    let migration = null, projection = null, currentManifest = null;
    if (externalOwned(game)) {
      migration = await externalDeployment.preview(game, { mode: 'local' }, internal);
      projection = Object.fromEntries(migration.changes.filter(row => row.afterSha256 !== undefined).map(row => [path.resolve(row.path).toLowerCase(), row.afterSha256]));
      currentManifest = migration.projectedManifest;
    }
    const result = await installer.previewUninstall({ gameDir: game.dir, ...options, manifestOverride: currentManifest, projectedFiles: projection });
    return { ...result, gameId: id, requiresFgRestore: migration?.requiresFgRestore === true,
      phases: migration ? ['restore-local', 'uninstall'] : ['uninstall'],
      changes: [...(migration?.changes || []).map(row => ({ ...row, phase: 'restore-local' })),
        ...result.changes.map(row => ({ ...row, phase: 'uninstall' }))] };
  }

  function specialRouteGame(game, request) {
    if (request.route === 'feeder') {
      const vk = vulkan.summary(game);
      if (vk.installed || vk.needsRecovery) routeRestoreFirst();
      if (readManifest(game.dir)) throw Object.assign(new Error('先恢复原生 DLSS 配套，再准备 Feeder。'), { code: 'FEEDER_ROUTE_CONFLICT' });
      if (feederOwned(game) && classifyApi(game.scan.chosen) !== 'dx12') throw Object.assign(new Error('请先恢复已有 Feeder 配套，再更改其 API 绑定。'), { code: 'FEEDER_RESTORE_FIRST' });
      return feederRouteSelection(game, request.api || 'dx12');
    }
    requireNoFeeder(game); requireKnownVulkanOwnership(game);
    if (readManifest(game.dir) || externalOwned(game)) routeRestoreFirst();
    const chosen = game.scan?.chosen; if (!chosen) throw appError('ERR_NO_GAME_EXE');
    const api = request.api === 'auto' ? chosen.detectedApiResolution?.api || chosen.detectedApi : request.api || 'vulkan';
    if (api !== 'vulkan') throw appError('ERR_UNSUPPORTED_API');
    if (Array.isArray(chosen.supportedApis) && !chosen.supportedApis.includes(api)) throw appError('ERR_UNSUPPORTED_API');
    return { ...game, apiOverride: request.api || 'vulkan', scan: { ...game.scan,
      chosen: { ...chosen, apiResolution: { api: 'vulkan', source: 'override', evidence: [] } }, componentSelection: { dx11Carrier: false } } };
  }
  const specialFingerprint = preview => crypto.createHash('sha256').update(JSON.stringify(preview.changes)).digest('hex');
  async function specialPreviewWithSettings(game, routed, request, owner) {
    const preview = await owner.previewInstall(routed, { version: request.version });
    const requestedApi = request.api || preview.api;
    const settings = await gameApiSettings.preview?.(game, requestedApi);
    const before = store.read().gameOverrides[pathKey(game.dir)] || {};
    const selectedBefore = { api: before.api || null, apiExecutable: before.apiExecutable || null };
    const selectedAfter = { api: requestedApi, apiExecutable: game.scan.chosen.path };
    preview.changes.push(...(settings?.changes || []), { path: path.join(userData, 'settings.json'), name: 'settings.json',
      role: 'manager-api-preference', gameId: game.id, before: selectedBefore, after: selectedAfter,
      action: JSON.stringify(selectedBefore) === JSON.stringify(selectedAfter) ? 'keep' : 'update' });
    return preview;
  }
  async function previewSpecialDeployment(id, request = {}) {
    if (!request || typeof request !== 'object' || Array.isArray(request) ||
        Object.keys(request).some(key => !['route', 'api', 'version'].includes(key)) || !['vulkan', 'feeder'].includes(request.route) ||
        request.api !== undefined && !['auto', 'dx12', 'vulkan'].includes(request.api) ||
        request.version !== undefined && (typeof request.version !== 'string' || !request.version || request.version.length > 100)) throw appError('ERR_BAD_REQUEST');
    const game = findGame(id), routed = specialRouteGame(game, request), owner = request.route === 'feeder' ? feeder : vulkan;
    const preview = await specialPreviewWithSettings(game, routed, request, owner);
    const planId = crypto.randomUUID(), normalized = { ...request, api: request.api || preview.api, version: preview.packageId || preview.version };
    specialDeploymentPlans.set(planId, { id, request: normalized, expires: Date.now() + 5 * 60 * 1000,
      exe: game.scan.chosen.path, exeHash: await require('./external-runtime').digest(game.scan.chosen.path),
      preferenceBefore: store.read().gameOverrides[pathKey(game.dir)] || {}, fingerprint: specialFingerprint(preview) });
    return { ...preview, planId, gameId: id, fromMode: gameLayout(game).mode, toMode: preview.mode,
      loadingMode: 'proxy', phases: [`${request.route}-install`], changes: preview.changes.map(row => ({ ...row, phase: `${request.route}-install` })) };
  }
  async function applySpecialDeployment(planId, consent = {}) {
    const plan = specialDeploymentPlans.get(planId); specialDeploymentPlans.delete(planId);
    if (!plan || plan.expires < Date.now()) throw Object.assign(new Error('部署预览已过期，请重新检查。'), { code: 'DEPLOYMENT_PLAN_EXPIRED' });
    const game = findGame(plan.id), state = store.read(), key = pathKey(game.dir), before = state.gameOverrides[key] || {};
    if (game.scan?.chosen?.path !== plan.exe || await require('./external-runtime').digest(plan.exe) !== plan.exeHash ||
        ['api', 'apiExecutable'].some(field => (before[field] || null) !== (plan.preferenceBefore[field] || null)))
      throw Object.assign(new Error('预览后游戏 EXE 或 API 选择已变化。'), { code: 'DEPLOYMENT_PLAN_CHANGED' });
    const routed = specialRouteGame(game, plan.request), owner = plan.request.route === 'feeder' ? feeder : vulkan;
    const fresh = await specialPreviewWithSettings(game, routed, plan.request, owner);
    if (specialFingerprint(fresh) !== plan.fingerprint) throw Object.assign(new Error('预览后配套文件或加载状态已变化，请重新检查。'), { code: 'DEPLOYMENT_PLAN_CHANGED' });
    if (plan.request.route === 'vulkan') return applyGameRoute(plan.id, { api: plan.request.api, version: fresh.packageId, allowAntiCheat: consent.allowAntiCheat === true });
    let settingChange, preferenceWritten = false, result;
    try {
      result = await feeder.install(routed, { version: plan.request.version, allowAntiCheat: consent.allowAntiCheat === true }, async () => {
        settingChange = await gameApiSettings.apply(game, plan.request.api);
        await store.write({ gameOverrides: { ...state.gameOverrides, [key]: { ...before, api: plan.request.api, apiExecutable: plan.exe } } });
        preferenceWritten = true;
      });
    } catch (error) {
      if (preferenceWritten || settingChange?.changed) await rollbackApiChoice(error, state, key, settingChange);
      throw error;
    }
    return refreshAfterMutation({ ...result, applied: true, appliedRoute: { api: 'dx12', version: result.coreVersion, mode: 'local' },
      completedPhases: ['feeder-install'], runtimeVerified: false });
  }
  async function inspectDeployment(id) {
    const game = findGame(id), feed = feeder.summary(game), vk = vulkan.summary(game);
    const feederState = feed.installed || feed.needsRecovery, vulkanState = vk.installed || vk.needsRecovery;
    if (feederState && vulkanState) throw Object.assign(new Error('同一游戏同时存在多个固定配套的恢复记录，请保留现场核对，未接管其他路线。'), { code: 'DEPLOYMENT_OWNER_CONFLICT' });
    if (feederState || vulkanState) {
      const route = feederState ? 'feeder' : 'vulkan', summary = feederState ? feed : vk;
      const state = await (feederState ? feeder.inspect(game) : vulkan.diagnose(game));
      const pending = summary.needsRecovery === true || state.pending === true || state.needsRecovery === true || state.status === 'pending';
      return { ...state, source: route, route, mode: feederState ? 'local' : 'external',
        pending, needsRecovery: pending, runtimeVerified: false };
    }
    return externalDeployment.inspect(game);
  }
  async function recoverDeployment(id) {
    const game = findGame(id);
    const native = hasExternalRecord(game) ? await externalDeployment.recover(game) : { recovered: false };
    const before = await inspectDeployment(id);
    if (['feeder', 'vulkan'].includes(before.source)) {
      if (!before.pending && !before.needsRecovery) return { ...native, source: before.source,
        recovered: native.recovered === true, unchanged: native.recovered !== true, runtimeVerified: false };
      const result = await (before.source === 'feeder' ? feeder.restore(game) : vulkan.restore(game));
      const after = await inspectDeployment(id);
      if (after.pending || after.needsRecovery) throw Object.assign(new Error('原固定配套仍有未完成的恢复记录，统一操作记录已保留。'), { code: 'DEPLOYMENT_RECOVERY_REQUIRED' });
      return refreshAfterMutation({ ...result, source: before.source, recovered: true, runtimeVerified: false,
        notice: before.source === 'vulkan'
          ? '未完成的 Vulkan 部署已由原绑定恢复，运行配置已归档；此前已完成的其他设置仍保留。'
          : 'Feeder 的未完成文件事务已由固定配套恢复；此前已完成的其他设置仍保留。' });
    }
    return refreshAfterMutation(native);
  }
  async function gameModuleManifest(id) {
    try {
      const game = findGame(id), exe = game.scan?.chosen?.path;
      const { noLinks, digestFile, inside } = require('./launch-safety');
      const same = require('./game-processes').same;
      if (!path.isAbsolute(exe || '')) return [];
      await noLinks(exe);
      const readOwned = async (file, max = 512 * 1024) => {
        await noLinks(file); const stat = await fs.promises.stat(file);
        if (!stat.isFile() || stat.size > max) throw new Error('invalid owner record');
        return JSON.parse(await fs.promises.readFile(file, 'utf8'));
      };
      const modules = [], binary = /\.(?:dll|asi|addon(?:32|64)?)$/i, hash = /^[a-f0-9]{64}$/i;
      const add = async (file, role, expected, version, owner, architecture = 64) => {
        if (!path.isAbsolute(file || '') || !binary.test(file) || !hash.test(expected || '') || typeof role !== 'string' || !role) throw new Error('invalid expected module');
        await noLinks(file);
        const old = modules.find(row => same(row.path, file));
        if (old) { if (old.role !== role || old.sha256 !== expected) throw new Error('conflicting module roles'); return; }
        modules.push({ path: file, name: path.basename(file), role, sha256: expected, architecture, version: version || null, owner });
      };
      const nativeRoles = { addon: 'core', reshade: 'reshade', bridge: 'chain', runtime: 'nr-runtime', carrier: 'carrier' };
      const nativeModule = async (manifest, row, owner) => {
        const role = nativeRoles[row.kind]; if (!role) return;
        const file = path.resolve(game.dir, row.rel);
        if (!same(path.dirname(file), path.dirname(exe)) || (row.kind === 'reshade'
          ? !/^(?:dxgi|d3d12)\.dll$/i.test(path.basename(file)) : path.basename(file) !== INSTALLED_NAMES[row.kind])) throw new Error('module target does not match its owner');
        await add(file, role, row.installedSha256, manifest.payloadVersion, owner);
      };
      const feed = feeder.summary(game), vk = vulkan.summary(game);
      if ((feed.installed || feed.needsRecovery) && (vk.installed || vk.needsRecovery)) return [];
      if (feed.installed || feed.needsRecovery) {
        if (!feed.installed || feed.needsRecovery) return [];
        // summary validates this receipt against the code-pinned Feeder recipe.
        const row = await readOwned(path.join(game.dir, require('./feeder-runtime').RECEIPT));
        const cores = row.recipe.files.filter(item => item.role === 'core' && item.mutable === false);
        if (cores.length !== 1 || !same(path.resolve(game.dir, row.game.exe), exe)) return [];
        for (const spec of row.recipe.files.filter(item => item.mutable === false && binary.test(item.target))) {
          const owned = row.files.find(item => item.target === spec.target), file = owned && path.resolve(game.dir, owned.rel);
          if (!owned || owned.sha256 !== spec.sha256 || !inside(game.dir, file)) return [];
          await add(file, spec.role === 'loader' ? 'reshade' : spec.role, spec.sha256, row.recipe.coreVersion, 'feeder');
        }
      } else if (vk.installed || vk.needsRecovery) {
        if (!vk.installed || vk.needsRecovery || fs.existsSync(path.join(userData, 'vulkan-deployment/pending.json'))) return [];
        const basePath = vulkan.configDir(game); if (!path.isAbsolute(basePath || '')) return [];
        const identity = require('./vulkan-runtime-profile').createVulkanRuntimeProfile({ userData });
        const receipt = await readOwned(path.join(basePath, '.xiaofeng-vulkan-runtime.json'), 128 * 1024);
        const checked = identity.identifyReceipt({ receipt, basePath, exe });
        const packaged = resourcesPath && path.join(resourcesPath, 'vulkan-runtime/recipe.json');
        const source = await readOwned(packaged && fs.existsSync(packaged) ? packaged : path.join(appDir, 'resources/vulkan-runtime/recipe.json'));
        const sourceIdentity = identity.location({ exe, recipe: source });
        if (sourceIdentity.fingerprint !== checked.recipe.fingerprint || source.id !== vk.packageId || source.coreVersion !== vk.coreVersion) return [];
        // The runtime receipt retains path/hash identity. The matching fixed
        // recipe supplies the Core role, including the distributed Chinese name.
        const cores = source.files.filter(item => item.mutable === false && (item.role === 'core' ||
          /project Core/i.test(item.license || '') && /^[a-f0-9]{64}$/i.test(item.identity?.sourceManifestSha256 || item.provenance?.sourceManifestSha256 || '')));
        if (cores.length !== 1) return [];
        for (const spec of source.files.filter(item => item.mutable === false && binary.test(item.target))) {
          const owned = checked.recipe.files.find(item => same(path.join(basePath, item.target), path.join(basePath, spec.target)));
          if (!owned || owned.sha256 !== spec.sha256) return [];
          const file = path.resolve(basePath, owned.target); if (!inside(basePath, file)) return [];
          const role = spec === cores[0] ? 'core' : spec.role || ({ 'nrchain_nvngx.dll': 'chain', 'nvngx_dlssnr.dll': 'nr-runtime' })[path.basename(file).toLowerCase()] || 'provider';
          await add(file, role, owned.sha256, checked.recipe.coreVersion, 'vulkan-profile');
        }
        // The implicit ReShade loader belongs to the layer owner, outside the
        // per-game Core profile. Missing or invalid layer authority stays absent.
        try {
          const layerSourceFile = resourcesPath && path.join(resourcesPath, 'vulkan-reshade/recipe.json');
          const layerSource = await readOwned(layerSourceFile && fs.existsSync(layerSourceFile) ? layerSourceFile : path.join(appDir, 'resources/vulkan-reshade/recipe.json'));
          const layerReceipt = await readOwned(path.join(userData, 'vulkan-deployment/receipt.json'), 2 * 1024 * 1024);
          const layer = layerReceipt.layer, refs = layerReceipt.refs?.filter(row => same(row.exe, exe));
          if (layerReceipt.version !== 1 || layerReceipt.product !== 'xiaofeng-vulkan-deployment' || layerSource.version !== 1 || layerSource.architecture !== 64 ||
              !/^[a-z0-9][a-z0-9._-]{0,79}$/i.test(layerSource.id || '') || !/^[a-z0-9][a-z0-9._-]{0,79}$/i.test(layerSource.release || '') ||
              typeof layerSource.layer?.manifest !== 'string' || path.basename(layerSource.layer.manifest) !== layerSource.layer.manifest ||
              typeof layerSource.layer?.library !== 'string' || path.basename(layerSource.layer.library) !== layerSource.layer.library ||
              layerReceipt.registry?.scope !== 'HKCU' || layerReceipt.registry?.view !== '64' || layerReceipt.registry?.key !== 'Software\\Khronos\\Vulkan\\ImplicitLayers' ||
              refs?.length !== 1 || !hash.test(refs[0].exeSha256 || '') || !['owned', 'reused'].includes(layer?.mode) ||
              !path.isAbsolute(layer.library || '') || !path.isAbsolute(layer.manifest || '') ||
              layerReceipt.recipe?.id !== layerSource.id || layerReceipt.recipe.release !== layerSource.release ||
              layerReceipt.recipe.manifestSha256 !== layerSource.layer?.manifestSha256 || layerReceipt.recipe.librarySha256 !== layerSource.layer?.librarySha256)
            throw new Error('unmatched Vulkan layer owner');
          const expected = layer.mode === 'owned' ? layerReceipt.recipe.librarySha256 : layer.librarySha256;
          const expectedManifest = layer.mode === 'owned' ? layerReceipt.recipe.manifestSha256 : layer.manifestSha256;
          if (layer.mode === 'owned') {
            const directory = path.join(userData, 'vulkan-deployment/layers', layerSource.id);
            if (!same(layer.library, path.join(directory, layerSource.layer.library)) || !same(layer.manifest, path.join(directory, layerSource.layer.manifest))) throw new Error('Vulkan layer path changed');
          }
          const manifest = await readOwned(layer.manifest, 64 * 1024);
          if (!hash.test(expectedManifest || '') || await digestFile(layer.manifest) !== expectedManifest || manifest.layer?.name !== layerSource.layer.name ||
              manifest.layer?.type !== 'GLOBAL' || !manifest.layer.disable_environment || typeof manifest.layer.library_path !== 'string' ||
              !same(path.resolve(path.dirname(layer.manifest), manifest.layer.library_path), layer.library)) throw new Error('Vulkan layer manifest changed');
          await add(layer.library, 'reshade', expected, layerSource.release, `vulkan-layer-${layer.mode}`);
        } catch { /* Core authority alone cannot promote the missing loader. */ }
      } else if (externalOwned(game)) {
        const current = externalDeployment.getLayout(game);
        if (!current.verified || current.needsRecovery || current.blockers?.length || !same(current.exe, exe)) return [];
        for (const row of current.moduleManifest) {
          if (!inside(current.runtimeDir, row.path)) return [];
          const role = row.role === 'reshade' && current.loadingMode !== 'helper' ? 'inactive-loader' : row.role;
          await add(row.path, role, row.sha256, current.version, 'external', row.architecture);
        }
        if (!modules.some(row => row.role === 'reshade')) {
          const saved = await readOwned(path.join(game.dir, EXTERNAL_RECEIPT));
          const manifest = saved.currentManifest || saved.localManifest;
          assertManifestExecutable(game.dir, manifest, exe);
          for (const row of manifest.files.filter(item => item.kind === 'reshade')) await nativeModule(manifest, row, 'external-game-proxy');
          if (!modules.some(row => row.role === 'reshade') && saved.proxy)
            await add(path.join(path.dirname(exe), saved.proxy.name), 'reshade', saved.proxy.sha256, current.version, 'external-game-proxy');
        }
      } else {
        if (fs.existsSync(path.join(game.dir, '_DLSS5_Backup/pending-switch.json')) || fs.existsSync(path.join(game.dir, EXTERNAL_PENDING))) return [];
        await noLinks(manifestPath(game.dir)); const manifest = readManifest(game.dir);
        if (!manifest) return [];
        assertManifestExecutable(game.dir, manifest, exe);
        for (const row of manifest.files) await nativeModule(manifest, row, 'native');
      }
      return modules;
    } catch { return []; }
  }
  async function gameCoreIdentity(id) {
    try {
      const modules = await gameModuleManifest(id), cores = modules.filter(row => row.role === 'core');
      if (cores.length !== 1) return null;
      const core = cores[0];
      if (!core.version || await require('./launch-safety').digestFile(core.path) !== core.sha256 || require('../core/pe').getBitness(core.path) !== 64) return null;
      return { path: core.path, sha256: core.sha256, version: core.version, verified: true };
    } catch { return null; }
  }

  return {
    product: { ...PRODUCT, version },
    store,
    get payloadDir() { return payloadDir; },
    payloadState,
    selectPayloadSource,
    recheckPayloadSource: () => selectPayloadSource(store.read().payloadSourcePath),
    reShadeSource: () => {
      const inspected = inspectCurrentPayload({ allowMissingBundle: true, hardwareFamily: 'RTX40', version: selectedVersion() });
      const row = inspected.files?.find(file => file.kind === 'reshade');
      return row?.valid && row.expected ? { file: row.file, sha256: row.expected } : null;
    },
    withError,
    validateLaunch,
    boot: async () => {
      const loadedGames = await refresh();
      return {
      product: { ...PRODUCT, version },
      settings: store.read(),
      hardware,
      payload: inspectCurrentPayload({
        allowMissingBundle: true,
        hardwareFamily: hardware.family,
        version: selectedVersion()
      }),
      addons: listAddonVersions(),
      games: loadedGames,
      discoveryWarnings: latestScanWarnings
    }; },
    refresh,
    refreshAfterMutation,
    listGames: () => collectionFresh ? games.map(publicGame) : refresh(),
    dispose: () => library.dispose?.(),
    gameDirectory: id => findGame(id).dir,
    getLayout: id => gameLayout(findGame(id)),
    gameModuleManifest,
    gameCoreIdentity,
    inspectDeployment,
    previewDeployment,
    applyDeployment,
    previewSpecialDeployment,
    applySpecialDeployment,
    restoreDeployment: async (id, consent = {}) => refreshAfterMutation(await externalDeployment.restore(findGame(id), consent)),
    recoverDeployment,
    assertDeploymentReady: id => externalDeployment.assertReady(findGame(id)),
    previewUninstall,
    gameScan: id => structuredClone(findGame(id).scan),
    gamesInDirectory: dir => games.filter(game => pathKey(game.dir) === pathKey(dir)).map(game => ({
      id: game.id, executable: game.scan?.chosen?.path || null
    })),
    gameExecutable: id => {
      const game = findGame(id);
      return game.scan && game.scan.chosen ? game.scan.chosen.path : null;
    },
    addManualGame: async dir => {
      if (typeof dir !== 'string' || !path.isAbsolute(dir)) throw appError('ERR_BAD_REQUEST');
      const state = store.read();
      await store.write({
        manualGames: [...state.manualGames, dir],
        excludedRoots: state.excludedRoots.filter(root => pathKey(root) !== pathKey(dir)),
        excludedGames: removeExcludedFor(state.excludedGames, dir)
      });
      return refreshCollection(games);
    },
    prepareGameSelection: async (source, preferredExecutable = null) => {
      if (typeof source !== 'string' || !path.isAbsolute(source)) throw appError('ERR_BAD_REQUEST');
      return library.prepareSelection(source, preferredExecutable);
    },
    addManualSelection,
    addManualExecutable: async file => {
      if (typeof file !== 'string' || !path.isAbsolute(file) || !/\.exe$/i.test(file)) throw appError('ERR_BAD_REQUEST');
      const selection = await library.prepareSelection(file, file);
      if (!selection.chosen) throw appError('ERR_NO_GAME_EXE');
      return addManualSelection({
        root: selection.root,
        executable: selection.chosen.path,
        name: selection.name,
        icon: null
      });
    },
    addScanFolder: async dir => {
      if (typeof dir !== 'string' || !path.isAbsolute(dir)) throw appError('ERR_BAD_REQUEST');
      const state = store.read();
      await store.write({ scanFolders: [...state.scanFolders, dir] });
      return refreshCollection(games);
    },
    dismissGame: async (id, options = {}) => {
      const game = findGame(id);
      if (options.libraryOnly === true && (feederOwned(game) || vulkanOwned(game) || hasExternalRecord(game) || readManifest(game.dir)))
        throw Object.assign(new Error('请先选择卸载方式并完成恢复，再移出游戏库。'), { code: 'LIBRARY_RESTORE_FIRST' });
      if (options.libraryOnly === true) {
        requireNoFeeder(game); requireKnownVulkanOwnership(game);
        if (fs.existsSync(path.join(game.dir, '_DLSS5_Backup/pending-switch.json')))
          throw Object.assign(new Error('请先恢复未完成的组件操作，再移出游戏库。'), { code: 'LIBRARY_RESTORE_FIRST' });
      }
      if (options.libraryOnly !== true) {
      if (feederOwned(game)) await feeder.restore(game);
      requireNoFeeder(game);
      requireKnownVulkanOwnership(game);
      if (vulkanOwned(game)) await vulkan.restore(game);
      if (hasExternalRecord(game)) await externalDeployment.recover(game);
      if (externalOwned(game)) await externalDeployment.restore(game, { allowAntiCheat: true });
      // Removing a library entry also removes this tool's NR deployment. Never
      // hide the game while an incomplete restore still needs the user's attention.
      if (readManifest(game.dir)) {
        const result=await installer.uninstall({gameDir:game.dir,removeSettings:false,scan:game.scan});
        if (result?.removed !== true) throw appError('ERR_BACKUP_INVALID',{operation:'dismiss-uninstall',removed:false,warnings:result?.warnings||[]});
      }
      }
      const state = store.read();
      const executable = game.scan?.chosen?.path || null;
      const aliases = executableAliases(state, game.dir, executable);
      const removable = [...aliases.roots].filter(([key]) => !aliases.protectedRoots.has(key) && !aliases.sharedRoots.has(key));
      const hiddenRoots = removable.map(([, dir]) => dir);
      const hidesAnotherReceipt = dir => [...aliases.protectedRoots].some(key => isInside(aliases.roots.get(key), dir));
      // With another receipt, use exact-directory exclusions so its recovery
      // row cannot be hidden by the shared executable or launcher identity.
      const exclusionDirs = [...new Set([game.dir, ...hiddenRoots])];
      await store.write({
        excludedRoots: [...state.excludedRoots, ...hiddenRoots.filter(dir => !hidesAnotherReceipt(dir))],
        excludedGames: [...state.excludedGames, ...exclusionDirs.map(dir => ({
          dir, executable: aliases.protectedRoots.size ? null : executable,
          launcher: aliases.protectedRoots.size ? '' : game.launcher || '',
          id: aliases.protectedRoots.size ? null : game.appid || null,
          appid: aliases.protectedRoots.size ? null : game.appid || null
        }))],
        manualGames: state.manualGames.filter(dir => !aliases.ownsRoot(dir) || aliases.sharedRoots.has(pathKey(dir))),
        manualExecutables: state.manualExecutables.filter(row => !(aliases.ownsRoot(row.root) && (!executable || aliases.sameExe(row.file)))),
        gameOverrides: Object.fromEntries(Object.entries(state.gameOverrides).filter(([dir, row]) => !aliases.ownsMetadata(dir, row)))
      });
      return refreshCollection(games);
    },
    renameGame: async (id, name) => {
      const game = findGame(id);
      if (typeof name !== 'string') throw appError('ERR_BAD_REQUEST');
      const nextName = name.trim().slice(0, 160);
      if (!nextName) throw appError('ERR_BAD_REQUEST');
      const state = store.read();
      const key = pathKey(game.dir);
      await store.write({ gameOverrides: {
        ...state.gameOverrides,
        [key]: { ...state.gameOverrides[key], name: nextName }
      } });
      return refreshCollection(games);
    },
    updateSettings: async patch => {
      const allowed = ['scanDrives', 'addonVersion'];
      if (!patch || Object.keys(patch).some(key => !allowed.includes(key))) throw appError('ERR_BAD_REQUEST');
      if (Object.prototype.hasOwnProperty.call(patch, 'addonVersion') &&
          patch.addonVersion !== null && typeof patch.addonVersion !== 'string') {
        throw appError('ERR_BAD_REQUEST');
      }
      return store.write(patch);
    },
    applyGameRoute,
    setGameApi: async (id, api, options = {}) => {
      if (!['auto', 'dx9', 'dx10', 'dx11', 'dx12', 'vulkan', 'opengl'].includes(api)) throw appError('ERR_BAD_REQUEST');
      const game = findGame(id);
      requireNoFeeder(game);
      if (!game.scan || !game.scan.chosen) throw appError('ERR_NO_GAME_EXE');
      const nextApi = api === 'auto' ? game.scan.chosen.detectedApiResolution?.api || game.scan.chosen.detectedApi || 'unknown' : api;
      if (vulkanOwned(game) && nextApi !== 'vulkan' || readManifest(game.dir) && nextApi === 'vulkan') routeRestoreFirst();
      const state = store.read();
      const key = pathKey(game.dir);
      let settingChange;
      try {
        settingChange = await gameApiSettings.apply(game, api);
        await store.write({ gameOverrides: { ...state.gameOverrides,
          [key]: { ...state.gameOverrides[key], api, apiExecutable: game.scan.chosen.path } } });
        if (readManifest(game.dir)) {
          const effectiveApi = api === 'auto' ? game.scan.chosen.detectedApi || 'unknown' : api;
          const routedScan = { ...game.scan, chosen: { ...game.scan.chosen,
            apiResolution: { api: effectiveApi, source: api === 'auto' ? 'detected' : 'override', evidence: [] } },
            componentSelection: { dx11Carrier: effectiveApi === 'dx11' } };
          if (effectiveApi === 'dx11') {
            await repairGame({ ...game, scan: routedScan }, options);
          } else {
            // Every non-DX11 route retires only the managed carrier through the
            // existing journal. Core, nrchain and their backups remain intact.
            await installer.disableCarrier({ gameDir: game.dir, scan: routedScan, manual: false,
              allowAntiCheat: options && options.allowAntiCheat === true });
          }
        }
      } catch (error) {
        await rollbackApiChoice(error, state, key, settingChange);
        throw error;
      }
      return refreshCollection(games);
    },
    setGameCarrier: async () => { throw appError('ERR_BAD_REQUEST', { reason: 'carrier-follows-api' }); },
    listAddonVersions,
    importAddonFile: async file => {
      if (typeof file !== 'string' || !path.isAbsolute(file) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
        throw appError('ERR_ADDON_INVALID');
      }
      if (/\.zip$/i.test(file)) {
        let ota;
        try { ota = await readOtaPackage(file); }
        catch (error) {
          if (error?.code === 'ERR_OTA_CORE_ONLY') throw appError('ERR_OTA_CORE_ONLY');
          throw appError('ERR_ADDON_INVALID', { reason: error && error.message ? error.message : 'invalid-ota' });
        }
        const digest = sha256(file);
        const id = `imported-${digest.slice(0, 12)}`;
        const dir = path.join(addonVersionsDir, id);
        await fs.promises.mkdir(dir, { recursive: true });
        await fs.promises.writeFile(path.join(dir, 'nr-before-sr.zh-CN.addon64'), ota.addon);
        if (ota.bridge) await fs.promises.writeFile(path.join(dir, 'nrchain_nvngx.dll'), ota.bridge);
        if (ota.carrier) await fs.promises.writeFile(path.join(dir, ota.carrierName), ota.carrier);
        await fs.promises.copyFile(file, path.join(dir, 'ota-package.zip'));
        if (ota.instructions) await fs.promises.writeFile(path.join(dir, 'Instructions.txt'), ota.instructions, 'utf8');
        await fs.promises.writeFile(path.join(dir, 'meta.json'), JSON.stringify({
          id,
          kind: 'ota',
          label: ota.manifest.display_version || ota.manifest.version || 'OTA',
          sourceName: path.basename(file),
          importedAt: new Date().toISOString(),
          sha256: digest,
          compatibility: ota.compatibility || 'dx12',
          addonSha256: ota.addonSha256,
          bridgeSha256: ota.bridgeSha256,
          carrierName: ota.carrierName || null,
          carrierSha256: ota.carrierSha256 || null,
          otaManifest: {
            schema: ota.manifest.schema || 'nr-compact-compatibility',
            version: ota.manifest.version || null,
            sourceCommit: ota.manifest.sourceCommit || ota.manifest.source_commit || null,
            api: ota.manifest.api || (ota.compatibility === 'dx11' ? 'DX11/D3D12-x64' : null),
            includesDx11: ota.compatibility === 'dx11',
            bridgeSha256: ota.bridgeSha256
          },
          instructions: Boolean(ota.instructions)
        }, null, 2), 'utf8');
        return listAddonVersions();
      }
      if (!/\.addon64$/i.test(file)) throw appError('ERR_ADDON_INVALID');
      const digest = sha256(file);
      const id = `imported-${digest.slice(0, 12)}`;
      const dir = path.join(addonVersionsDir, id);
      await fs.promises.mkdir(dir, { recursive: true });
      await fs.promises.copyFile(file, path.join(dir, 'nr-before-sr.zh-CN.addon64'));
      await fs.promises.writeFile(path.join(dir, 'meta.json'), JSON.stringify({
        id,
        label: path.basename(file).replace(/\.addon64$/i, ''),
        sourceName: path.basename(file),
        importedAt: new Date().toISOString(),
        sha256: digest
      }, null, 2), 'utf8');
      return listAddonVersions();
    },
    removeAddonVersion: async id => {
      const item = importedAddon(id);
      if (!item) throw appError('ERR_ADDON_NOT_FOUND');
      await fs.promises.rm(path.join(addonVersionsDir, id), { recursive: true, force: true });
      return listAddonVersions();
    },
    fetchGameArt: async id => {
      const game = findGame(id);
      const poster = await artService.fetchGameArt({
        id: game.appid || null,
        launcher: game.launcher,
        name: game.name,
        dir: game.dir
      });
      return poster;
    },
    install: async (id, options = {}) => {
      const game = findGame(id);
      if (externalOwned(game)) return refreshAfterMutation(await repairGame(game, options));
      if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
      if (feederOwned(game)) return refreshAfterMutation(await feeder.install(game, options));
      requireNoFeeder(game);
      requireKnownVulkanOwnership(game);
      if (vulkanRoute(game) || vulkanOwned(game)) return refreshAfterMutation(await vulkan.install(game, options));
      const payload = selectedPayload(game, options && typeof options.version === 'string' ? options.version : null);
      await prepareExistingReframework(game, options);
      const result = await installer.install({ gameDir: game.dir, payload, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return refreshAfterMutation(await prepareDetectedReframework(game, payload.replacement ? { ...result, payloadReplacement: payload.replacement } : result));
    },
    repair: async (id, options = {}) => refreshAfterMutation(await repairGame(findGame(id), options)),
    upgradeAddon: async (id, version, options = {}) => {
      const game = findGame(id);
      if (externalOwned(game)) return refreshAfterMutation(await repairGame(game, { ...options, version }));
      requireNoFeeder(game);
      if (vulkanOwned(game) || vulkanRoute(game)) return refreshAfterMutation(await vulkan.install(game, { ...options, version }));
      const imported = importedAddon(version);
      if (!imported) throw appError('ERR_ADDON_NOT_FOUND');
      await prepareExistingReframework(game, options);
      const result = await installer.upgradeAddon({ gameDir: game.dir, addon: imported, version: imported.id, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return refreshAfterMutation(await prepareDetectedReframework(game, result));
    },
    toggleD3D12: async (id, enabled, options = {}) => {
      const game = findGame(id);
      if (externalOwned(game)) throw Object.assign(new Error('请先恢复普通运行目录，再调整加载器槽位。'), { code: 'DEPLOYMENT_RESTORE_FIRST' });
      requireNoFeeder(game);
      if (vulkanOwned(game) || vulkanRoute(game)) routeRestoreFirst();
      const result = await installer.toggleD3D12({ gameDir: game.dir, enabled: enabled === true, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return refreshAfterMutation(result);
    },
    launch: async id => {
      const exe = await validateLaunch(id);
      const game = findGame(id);
      if (feederOwned(game)) { await feeder.launch(game); return true; }
      if (vulkanOwned(game)) { await vulkan.launch(game); return true; }
      // An explicitly elevated Manager must not silently elevate its games.
      // The same broker preserves the selected EXE/cwd/arguments and proves a
      // same-user ordinary token, just as the external routes do.
      nativeLaunchBroker ||= createGameLaunchBroker({ resourcesPath });
      try { await nativeLaunchBroker.launch({ exe, args: [], cwd: path.dirname(exe) }); }
      catch (error) {
        if (error?.code === 'GAME_LAUNCH_REQUIRES_ELEVATION') throw Object.assign(new Error('该游戏明确要求管理员权限，本次普通权限启动未执行；管理器不会静默提权游戏。'), { code: error.code });
        throw error;
      }
      return true;
    },
    uninstall: async (id, request = false) => {
      const game = findGame(id);
      const { mode, removeSettings } = uninstallRequest(request);
      if (hasExternalRecord(game)) await externalDeployment.recover(game);
      if (externalOwned(game)) await externalDeployment.restore(game, { allowAntiCheat: true });
      if (feederOwned(game)) return refreshAfterMutation(await feeder.restore(game));
      requireNoFeeder(game);
      if (vulkanOwned(game)) return refreshAfterMutation(await vulkan.restore(game));
      const result = await installer.uninstall({ gameDir: game.dir, mode, removeSettings, scan: game.scan });
      if (result?.removed !== true) {
        const warnings = Array.isArray(result?.warnings) ? result.warnings : [];
        const warning = warnings.find(row => row && typeof row.code === 'string' && Object.hasOwn(MESSAGES, row.code));
        throw appError(warning?.code || 'ERR_BACKUP_INVALID', { ...warning, operation: 'uninstall', removed: false, warnings });
      }
      return refreshAfterMutation(result);
    },
    restoreManagedForCleanup: async id => {
      const game = findGame(id);
      if (hasExternalRecord(game)) await externalDeployment.recover(game);
      if (externalOwned(game)) await externalDeployment.restore(game, { allowAntiCheat: true });
      let result = { restored: false, unchanged: true };
      if (feederOwned(game)) result = await feeder.restore(game);
      else if (vulkanOwned(game)) result = await vulkan.restore(game);
      else if (readManifest(game.dir)) {
        result = await installer.uninstall({ gameDir: game.dir, removeSettings: false, scan: game.scan });
        if (result?.removed !== true) throw appError('ERR_BACKUP_INVALID', { operation: 'environment-cleanup-restore', removed: false });
      }
      const input = reframeworkInput(game);
      if (input) await reframework.restore(input);
      return refreshAfterMutation(result);
    },
    diagnose: async id => (await diagnoseGame(findGame(id))).diagnostic,
    inspectFeeder: async id => feeder.inspect(findGame(id)),
    installFeeder: async (id, options = {}) => {
      if (!options || typeof options !== 'object' || Array.isArray(options) ||
          Object.keys(options).some(key => !['api', 'allowAntiCheat'].includes(key)) ||
          options.api !== undefined && !['auto', 'dx12'].includes(options.api) ||
          options.allowAntiCheat !== undefined && typeof options.allowAntiCheat !== 'boolean') throw appError('ERR_BAD_REQUEST');
      const game = findGame(id), vk = vulkan.summary(game);
      if (vk.installed || vk.needsRecovery) routeRestoreFirst();
      if (readManifest(game.dir)) throw Object.assign(new Error('先恢复原生 DLSS 配套，再准备 Feeder。'), { code: 'FEEDER_ROUTE_CONFLICT' });
      const installOptions = { allowAntiCheat: options.allowAntiCheat === true };
      if (options.api === undefined) return refreshAfterMutation(await feeder.install(game, installOptions));
      const routed = feederRouteSelection(game, options.api), current = feeder.summary(game);
      if (current.installed || current.needsRecovery) {
        // Repairs keep the original EXE/API binding. A same-API request may
        // repair files, but cannot silently turn an existing route into another.
        if (classifyApi(game.scan.chosen) !== 'dx12')
          throw Object.assign(new Error('请先恢复已有 Feeder 配套，再更改其 API 绑定。'), { code: 'FEEDER_RESTORE_FIRST' });
        return refreshAfterMutation(await feeder.install(game, installOptions));
      }
      const state = store.read(), key = pathKey(game.dir);
      let settingChange, preferenceWritten = false, result;
      try {
        result = await feeder.install(routed, installOptions, async () => {
          // Feeder invokes this only after package, process, anti-cheat and
          // launch preflight, immediately before its existing file transaction.
          settingChange = await gameApiSettings.apply(game, options.api);
          await store.write({ gameOverrides: { ...state.gameOverrides,
            [key]: { ...state.gameOverrides[key], api: options.api, apiExecutable: game.scan.chosen.path } } });
          preferenceWritten = true;
        });
      } catch (error) {
        if (preferenceWritten || settingChange?.changed) await rollbackApiChoice(error, state, key, settingChange);
        throw error;
      }
      return refreshAfterMutation({ ...result, appliedRoute: { api: 'dx12', version: result.coreVersion,
        gameSettingsSynced: settingChange?.applied === true } });
    },
    restoreFeeder: async id => refreshAfterMutation(await feeder.restore(findGame(id))),
    readReframework: async id => {
      const input = reframeworkInput(findGame(id));
      if (!input) return { matched: false, ready: false, canPrepare: false, blockers: [] };
      const pendingFile = path.join(input.gameDir, '_DLSS5_Backup', 'pending-switch.json');
      if (fs.existsSync(pendingFile)) {
        await require('./launch-safety').noLinks(pendingFile);
        if (fs.statSync(pendingFile).size > 2 * 1024 * 1024) throw appError('ERR_BACKUP_INVALID');
        const pending = JSON.parse(fs.readFileSync(pendingFile, 'utf8'));
        if (pending.files?.some(row => String(row.rel || '').replace(/\\/g, '/').toLowerCase() === '_dlss5_backup/reframework-preparation.json'))
          return { matched: true, ready: false, canPrepare: false, needsRecovery: true, blockers: [{ code: 'REF_RECOVERY_REQUIRED', message: '兼容组件有未完成操作，请先恢复。' }], warnings: [] };
      }
      return reframework.inspect(input);
    },
    prepareReframework: async (id, options = {}) => {
      const game = findGame(id), input = reframeworkInput(game);
      if (!input) throw Object.assign(new Error('当前游戏没有匹配的 REFramework 兼容配套。'), { code: 'REF_UNSUPPORTED_TARGET' });
      const guards = require('../core/install-guards');
      if (guards.antiCheatPresent(game.dir) && options.allowAntiCheat !== true) throw appError('ERR_ANTI_CHEAT_CONFIRM');
      return reframework.prepare(input);
    },
    restoreReframework: async id => {
      const input = reframeworkInput(findGame(id));
      if (!input) throw Object.assign(new Error('当前游戏没有匹配的 REFramework 兼容配套。'), { code: 'REF_UNSUPPORTED_TARGET' });
      return reframework.restore(input);
    },
    recoverReframework: async id => {
      const input = reframeworkInput(findGame(id));
      if (!input) throw Object.assign(new Error('当前游戏没有匹配的 REFramework 兼容配套。'), { code: 'REF_UNSUPPORTED_TARGET' });
      return reframework.recover(input);
    },
    collectFeedback: async (id, options = {}) => {
      const game = findGame(id);
      const includePaths = options && options.includePaths === true;
      let payload = null;
      let diagnostic = null;
      try {
        ({ payload, diagnostic } = await diagnoseGame(game));
      } catch (error) {
        const normalized = normalizeError(error);
        diagnostic = {
          complete: false,
          components: [{ label: '诊断生成', ok: false, detail: normalized.message }]
        };
      }
      let settings = null;
      let managedLogDirs = [];
      try {
        if (game.scan && game.scan.chosen && game.scan.chosen.path) {
          const file = path.join(await settingDirectory(game), 'nr_before_sr.ini');
          if (fs.existsSync(file)) settings = readConfig(file);
        }
      } catch {}
      try {
        if (externalOwned(game)) managedLogDirs = gameLayout(game).logDirs;
        else if (vulkanOwned(game)) managedLogDirs = [await settingDirectory(game, 'reshade'), await settingDirectory(game)];
        else if (reframeworkInput(game)) managedLogDirs = [await settingDirectory(game)];
      }
      catch { /* A broken binding must still allow exporting its diagnosis. */ }
      return feedback.buildReport({
        game,
        diagnostic,
        hardware: hardware || detectGpu(),
        payload,
        settings,
        gameId: id,
        includePaths,
        managedLogDirs
      });
    },
    readNrSettings: async id => {
      const game = findGame(id);
      const file = path.join(await settingDirectory(game), 'nr_before_sr.ini');
      return readConfig(file);
    },
    writeNrSettings: async (id, patch) => {
      const game = findGame(id);
      if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
      const file = path.join(await settingDirectory(game), 'nr_before_sr.ini');
      return writeConfig(file, patch);
    },
    readGameHotkeys: async id => {
      const game = findGame(id);
      const executableDir = await settingDirectory(game, 'reshade');
      return {
        nr: { key: 117, label: 'F6', fixed: true, supported: false },
        reshade: readReShadeHotkey(path.join(executableDir, 'ReShade.ini'))
      };
    },
    writeGameHotkey: async (id, target, binding) => {
      const game = findGame(id);
      if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
      const executableDir = await settingDirectory(game, 'reshade');
      if (target !== 'reshade') throw appError('ERR_HOTKEY_UNSUPPORTED');
      return writeReShadeHotkey(path.join(executableDir, 'ReShade.ini'), binding);
    },
    applyDefault: async id => {
      const game = findGame(id);
      if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
      const file = path.join(await settingDirectory(game), 'nr_before_sr.ini');
      const patch = defaultPatch(vulkanOwned(game) ? '' : readManifest(game.dir)?.payloadVersion);
      let current = '';
      try { current = fs.readFileSync(file, 'utf8'); } catch {}
      if (!/^[ \t]*WorkMode\s*=/mi.test(current)) {
        delete patch.WorkMode;
        delete patch.CustomWorkScale;
        patch.ColorStrength = 0;
      }
      return writeConfig(file, patch);
    },
    applyRecommended: async id => {
      const game = findGame(id);
      if (hasExternalRecord(game)) await externalDeployment.assertReady(game);
      const file = path.join(await settingDirectory(game), 'nr_before_sr.ini');
      const patch = defaultPatch(vulkanOwned(game) ? '' : readManifest(game.dir)?.payloadVersion);
      let current = '';
      try { current = fs.readFileSync(file, 'utf8'); } catch {}
      if (!/^[ \t]*WorkMode\s*=/mi.test(current)) {
        delete patch.WorkMode;
        delete patch.CustomWorkScale;
        patch.ColorStrength = 0;
      }
      return writeConfig(file, patch);
    }
  };
}

module.exports = { createAppService };
