'use strict';

const fs = require('fs');
const path = require('path');
const { PRODUCT, DX11_COMPAT_VERSION } = require('./constants');
const { isDx11Only, classifyApi } = require('./game-support');
const { createStore } = require('./state-store');
const { createLibraryService } = require('./library-service');
const { createInstaller } = require('./installer');
const { payloadRoot, readBundle, inspectPayload, requirePayload, sha256 } = require('./payload');
const { readConfig, writeConfig, defaultPatch } = require('./nr-config');
const { readReShadeHotkey, writeReShadeHotkey } = require('./hotkeys');
const { readManifest } = require('./manifest');
const { appError, normalizeError } = require('./errors');
const { detectGpu: readGpu } = require('./gpu');
const { createArtService } = require('./art-service');
const { readOtaPackage } = require('./ota');
const { createFeedbackCollector } = require('./feedback');
const { resolveVersion } = require('./version-selection');

function createAppService({ userData, resourcesPath, appDir, version = '0.0.0', overrides = {} }) {
  const store = createStore(path.join(userData, 'settings.json'));
  const library = overrides.library || createLibraryService();
  const installer = overrides.installer || createInstaller();
  const detectGpu = overrides.detectGpu || readGpu;
  const artService = createArtService({ userData });
  const feedback = createFeedbackCollector({ userData, productVersion: version });
  const payloadDir = payloadRoot(fs.existsSync(path.join(resourcesPath || '', 'payload')) ? resourcesPath : appDir);
  const addonVersionsDir = path.join(userData, 'addon-versions');
  let games = [];
  let hardware = detectGpu();

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

  function selectedVersionForGame(game, requestedVersion = null) {
    // API chooses the deployment route; it does not override an explicit core
    // selection. The compatibility bundle is only a default recommendation.
    return resolveVersion({
      game,
      requestedVersion,
      globalVersion: selectedVersion(),
      dx11Version: recommendedCompatVersion(),
      dx11Only: Boolean(game && game.scan && isDx11Only(game.scan.chosen))
    });
  }

  function selectedPayload(game, requestedVersion = null) {
    const version = selectedVersionForGame(game, requestedVersion);
    if (/^imported-/i.test(version || '')) throw appError('ERR_ADDON_NOT_FOUND', { reason: 'Install a bundled base before applying an imported update.' });
    const result = requirePayload(payloadDir, (hardware = detectGpu()).family, version);
    if (requestedVersion && result.version !== requestedVersion) throw appError('ERR_ADDON_NOT_FOUND');
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
      inspected = inspectPayload(payloadDir, {
        allowMissingBundle: true,
        hardwareFamily: (hardware = detectGpu()).family,
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
          source: 'bundled',
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
    const family = (hardware = detectGpu()).family;
    const version = selectedVersionForGame(game, requestedVersion);
    const imported = importedAddon(version);
    if (/^imported-/i.test(version || '') && !imported) throw appError('ERR_ADDON_NOT_FOUND');
    if (!imported) {
      const payload = inspectPayload(payloadDir, {
        allowMissingBundle: true,
        hardwareFamily: family,
        version
      });
      return {
        payload,
        payloadVersion: payload.selectedVersion,
        payloadVersionLabel: payload.versions && payload.selectedVersion
          ? payload.versions[payload.selectedVersion] && payload.versions[payload.selectedVersion].label
          : null
      };
    }

    // An imported OTA owns the addon (and, when present, its paired bridge),
    // while runtime/ReShade/config remain game-install state. Do not compare
    // those preserved files against whichever bundled version is selected.
    const base = inspectPayload(payloadDir, {
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

  async function repairGame(game, options = {}) {
      const requestedVersion = options && typeof options.version === 'string' ? options.version : null;
      const version = selectedVersionForGame(game, requestedVersion);
      const imported = importedAddon(version);
      if (/^imported-/i.test(version || '') && !imported) throw appError('ERR_ADDON_NOT_FOUND');
      if (imported) {
        const result = await installer.upgradeAddon({ gameDir: game.dir, addon: imported, version: imported.id, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
        return result;
      }
      const payload = selectedPayload(game, requestedVersion);
      const result = await installer.repair({ gameDir: game.dir, payload, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return result;
  }

  function publicGame(game) {
    if (!game) return null;
    const { scan, ...safe } = game;
    return { ...safe, recommendedAddonVersion: safe.recommendedAddonVersion ? recommendedCompatVersion() : null };
  }

  function findGame(id) {
    const game = games.find(row => row.id === id);
    if (!game) throw appError('ERR_UNKNOWN_GAME');
    return game;
  }

  async function refresh() {
    games = await library.scanAll(store.read());
    return games.map(publicGame);
  }

  async function refreshCollection(fallback = games) {
    try { return await refresh(); } catch { return fallback; }
  }

  async function refreshAfterMutation(value) {
    // The journal transaction has already committed. A transient scan failure
    // must not turn that successful mutation into an IPC error; the renderer
    // retries the cache refresh separately.
    try { await refresh(); } catch {}
    return value;
  }

  async function withError(work, context = {}) {
    try {
      const value = await work();
      await feedback.record({ ...context, ok: true });
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
    const manualExecutables = state.manualExecutables.filter(row => pathKey(row.root) !== pathKey(root));
    manualExecutables.push({ root, file: executable });
    const gameOverrides = { ...state.gameOverrides };
    const name = typeof selection.name === 'string' ? selection.name.trim().slice(0, 160) : '';
    const icon = typeof selection.icon === 'string' && selection.icon.length <= 1024 * 1024 ? selection.icon : null;
    gameOverrides[pathKey(root)] = { ...gameOverrides[pathKey(root)], name, icon };
      await store.write({
        manualGames: [...state.manualGames, root],
        manualExecutables,
        gameOverrides,
        excludedRoots: state.excludedRoots.filter(item => pathKey(item) !== pathKey(root)),
        excludedGames: removeExcludedFor(state.excludedGames, root, executable)
      });
    return refreshCollection(games);
  }

  async function validateLaunch(id) {
    const game = findGame(id);
    if (!game.scan || !game.scan.chosen || !game.scan.chosen.path) throw appError('ERR_NO_GAME_EXE');
    if (game.installed) {
      const diagnostic = await installer.diagnose({ gameDir: game.dir, scan: game.scan });
      if (['mixed', 'unknown'].includes(classifyApi(game.scan.chosen))) throw appError('ERR_API_SELECTION_REQUIRED');
      if (diagnostic.routeMismatch) throw appError('ERR_API_ROUTE_PENDING');
    }
    const exe = game.scan.chosen.path;
    if (typeof exe !== 'string' || !path.isAbsolute(exe) || path.extname(exe).toLowerCase() !== '.exe' || !fs.existsSync(exe)) throw appError('ERR_NO_GAME_EXE');
    return exe;
  }

  return {
    product: { ...PRODUCT, version },
    store,
    payloadDir,
    withError,
    validateLaunch,
    boot: async () => ({
      product: { ...PRODUCT, version },
      settings: store.read(),
      hardware: (hardware = detectGpu()),
      payload: inspectPayload(payloadDir, {
        allowMissingBundle: true,
        hardwareFamily: hardware.family,
        version: selectedVersion()
      }),
      addons: listAddonVersions(),
      games: await refresh()
    }),
    refresh,
    gameDirectory: id => findGame(id).dir,
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
    dismissGame: async id => {
      const game = findGame(id);
      const key = pathKey(game.dir);
      const state = store.read();
      await store.write({
        excludedRoots: [...state.excludedRoots, game.dir],
        excludedGames: [...state.excludedGames, {
          dir: game.dir,
          executable: game.scan && game.scan.chosen ? game.scan.chosen.path : null,
          launcher: game.launcher || '',
          id: game.appid || null,
          appid: game.appid || null
        }],
        manualGames: state.manualGames.filter(dir => pathKey(dir) !== key),
        manualExecutables: state.manualExecutables.filter(row => pathKey(row.root) !== key),
        gameOverrides: Object.fromEntries(Object.entries(state.gameOverrides).filter(([dir]) => dir !== key))
      });
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
    setGameApi: async (id, api, options = {}) => {
      if (!['auto', 'dx11', 'dx12'].includes(api)) throw appError('ERR_BAD_REQUEST');
      const game = findGame(id);
      if (!game.scan || !game.scan.chosen) throw appError('ERR_NO_GAME_EXE');
      const state = store.read();
      const key = pathKey(game.dir);
      await store.write({ gameOverrides: { ...state.gameOverrides,
        [key]: { ...state.gameOverrides[key], api, apiExecutable: game.scan.chosen.path } } });
      try {
        // DX12 never needs the DX11 carrier. Retire it using the same journal
        // operation as manual-off; this does not alter game launch arguments.
        if (api === 'dx12' && readManifest(game.dir)) {
          const scan = { ...game.scan, chosen: { ...game.scan.chosen,
            apiResolution: { api: 'dx12', source: 'override', evidence: [] } },
            componentSelection: { dx11Carrier: false } };
          await installer.disableCarrier({ gameDir: game.dir, scan, manual: false,
            allowAntiCheat: options && options.allowAntiCheat === true });
        }
      } catch (error) {
        const current = store.read(), previous = state.gameOverrides[key] || {};
        try { await store.write({ gameOverrides: { ...current.gameOverrides,
          [key]: { ...current.gameOverrides[key], api: previous.api || 'auto',
            apiExecutable: previous.apiExecutable || null } } }); }
        catch { error.details = { ...error.details, preferenceRollbackFailed: true }; }
        throw error;
      }
      return refreshCollection(games);
    },
    setGameCarrier: async (id, enabled, options = {}) => {
      if (typeof enabled !== 'boolean') throw appError('ERR_BAD_REQUEST');
      const game = findGame(id);
      if (!game.scan || !game.scan.chosen) throw appError('ERR_NO_GAME_EXE');
      if (enabled && !isDx11Only(game.scan.chosen)) throw appError('ERR_UNSUPPORTED_API', { reason: 'carrier-only-for-dx11' });
      const installed = readManifest(game.dir);
      const state = store.read();
      const key = pathKey(game.dir);
      const previous = state.gameOverrides[key] || {};
      await store.write({ gameOverrides: { ...state.gameOverrides,
        [key]: { ...previous, carrierEnabled: enabled, carrierExecutable: game.scan.chosen.path } } });
      try {
        if (installed) {
          if (!enabled) await installer.disableCarrier({ gameDir: game.dir, scan: game.scan,
            allowAntiCheat: options && options.allowAntiCheat === true });
          else await repairGame({ ...game, scan: { ...game.scan,
            componentSelection: { dx11Carrier: true } } }, options);
        }
      } catch (error) {
        // A refused file operation must not leave the UI claiming the active
        // carrier was disabled. Restore just this preference, preserving peers.
        const current = store.read();
        try { await store.write({ gameOverrides: { ...current.gameOverrides,
          [key]: { ...current.gameOverrides[key], carrierEnabled: previous.carrierEnabled ?? null,
            carrierExecutable: previous.carrierExecutable ?? null } } }); }
        catch { error.details = { ...error.details, preferenceRollbackFailed: true }; }
        throw error;
      }
      return refreshCollection(games);
    },
    listAddonVersions,
    importAddonFile: async file => {
      if (typeof file !== 'string' || !path.isAbsolute(file) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
        throw appError('ERR_ADDON_INVALID');
      }
      if (/\.zip$/i.test(file)) {
        let ota;
        try { ota = await readOtaPackage(file); }
        catch (error) { throw appError('ERR_ADDON_INVALID', { reason: error && error.message ? error.message : 'invalid-ota' }); }
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
      const payload = selectedPayload(game, options && typeof options.version === 'string' ? options.version : null);
      const result = await installer.install({ gameDir: game.dir, payload, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return result;
    },
    repair: async (id, options = {}) => repairGame(findGame(id), options),
    upgradeAddon: async (id, version, options = {}) => {
      const game = findGame(id);
      const imported = importedAddon(version);
      if (!imported) throw appError('ERR_ADDON_NOT_FOUND');
      const result = await installer.upgradeAddon({ gameDir: game.dir, addon: imported, version: imported.id, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return result;
    },
    toggleD3D12: async (id, enabled, options = {}) => {
      const game = findGame(id);
      const result = await installer.toggleD3D12({ gameDir: game.dir, enabled: enabled === true, scan: game.scan, allowAntiCheat: options && options.allowAntiCheat === true });
      return result;
    },
    launch: async id => {
      const exe = await validateLaunch(id);
      const { spawn } = require('child_process');
      await new Promise((resolve, reject) => {
        const child = spawn(exe, [], { cwd: path.dirname(exe), detached: true, stdio: 'ignore', windowsHide: false });
        child.once('error', reject);
        child.once('spawn', () => { child.unref(); resolve(); });
      });
      return true;
    },
    uninstall: async (id, removeSettings = false) => {
      const game = findGame(id);
      const result = await installer.uninstall({ gameDir: game.dir, removeSettings, scan: game.scan });
      return result;
    },
    diagnose: async id => {
      const game = findGame(id);
      const inspected = inspectGamePayload(game);
      const payload = inspected.payload;
      const mapped = Object.fromEntries(payload.files.map(row => [row.kind, row]));
      mapped.versionInfo = payload.versionInfo || (payload.versions && payload.versions[payload.selectedVersion]);
      return installer.diagnose({
        gameDir: game.dir,
        payload: mapped,
        payloadVersion: inspected.payloadVersion,
        payloadVersionLabel: inspected.payloadVersionLabel,
        scan: game.scan
      });
    },
    collectFeedback: async (id, options = {}) => {
      const game = findGame(id);
      const includePaths = options && options.includePaths === true;
      let payload = null;
      let diagnostic = null;
      try {
        const inspected = inspectGamePayload(game);
        payload = inspected.payload;
        const mapped = Object.fromEntries(payload.files.map(row => [row.kind, row]));
        mapped.versionInfo = payload.versionInfo || (payload.versions && payload.versions[payload.selectedVersion]);
        diagnostic = await installer.diagnose({
          gameDir: game.dir,
          payload: mapped,
          payloadVersion: inspected.payloadVersion,
          payloadVersionLabel: inspected.payloadVersionLabel,
          scan: game.scan
        });
      } catch (error) {
        const normalized = normalizeError(error);
        diagnostic = {
          complete: false,
          components: [{ label: '诊断生成', ok: false, detail: normalized.message }]
        };
      }
      let settings = null;
      try {
        if (game.scan && game.scan.chosen && game.scan.chosen.path) {
          const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
          if (fs.existsSync(file)) settings = readConfig(file);
        }
      } catch {}
      return feedback.buildReport({
        game,
        diagnostic,
        hardware: hardware || detectGpu(),
        payload,
        settings,
        gameId: id,
        includePaths
      });
    },
    readNrSettings: async id => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
      return readConfig(file);
    },
    writeNrSettings: async (id, patch) => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
      return writeConfig(file, patch);
    },
    readGameHotkeys: async id => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const executableDir = path.dirname(game.scan.chosen.path);
      return {
        nr: { key: 117, label: 'F6', fixed: true, supported: false },
        reshade: readReShadeHotkey(path.join(executableDir, 'ReShade.ini'))
      };
    },
    writeGameHotkey: async (id, target, binding) => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      if (target !== 'reshade') throw appError('ERR_HOTKEY_UNSUPPORTED');
      const executableDir = path.dirname(game.scan.chosen.path);
      return writeReShadeHotkey(path.join(executableDir, 'ReShade.ini'), binding);
    },
    applyDefault: async id => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
      const patch = defaultPatch();
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
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
      const patch = defaultPatch();
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
