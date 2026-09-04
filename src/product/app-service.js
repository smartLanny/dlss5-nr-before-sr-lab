'use strict';

const fs = require('fs');
const path = require('path');
const { PRODUCT } = require('./constants');
const { createStore } = require('./state-store');
const { createLibraryService } = require('./library-service');
const { createInstaller } = require('./installer');
const { payloadRoot, inspectPayload, requirePayload } = require('./payload');
const { readConfig, writeConfig, recommendedPatch } = require('./nr-config');
const { readManifest } = require('./manifest');
const { appError, normalizeError } = require('./errors');

function createAppService({ userData, resourcesPath, appDir, version = '0.0.0' }) {
  const store = createStore(path.join(userData, 'settings.json'));
  const library = createLibraryService();
  const installer = createInstaller();
  const payloadDir = payloadRoot(fs.existsSync(path.join(resourcesPath || '', 'payload')) ? resourcesPath : appDir);
  let games = [];

  function publicGame(game) {
    if (!game) return null;
    const { scan, ...safe } = game;
    return safe;
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

  async function withError(work) {
    try { return { ok: true, value: await work() }; }
    catch (error) { return { ok: false, error: normalizeError(error) }; }
  }

  return {
    product: { ...PRODUCT, version },
    store,
    payloadDir,
    withError,
    boot: async () => ({
      product: { ...PRODUCT, version },
      settings: store.read(),
      payload: inspectPayload(payloadDir, { allowMissingBundle: true }),
      games: await refresh()
    }),
    refresh,
    gameDirectory: id => findGame(id).dir,
    addManualGame: async dir => {
      if (typeof dir !== 'string' || !path.isAbsolute(dir)) throw appError('ERR_BAD_REQUEST');
      const state = store.read();
      await store.write({ manualGames: [...state.manualGames, dir] });
      return refresh();
    },
    addScanFolder: async dir => {
      if (typeof dir !== 'string' || !path.isAbsolute(dir)) throw appError('ERR_BAD_REQUEST');
      const state = store.read();
      await store.write({ scanFolders: [...state.scanFolders, dir] });
      return refresh();
    },
    updateSettings: async patch => {
      const allowed = ['scanDrives'];
      if (!patch || Object.keys(patch).some(key => !allowed.includes(key))) throw appError('ERR_BAD_REQUEST');
      return store.write(patch);
    },
    install: async id => {
      const game = findGame(id);
      const payload = requirePayload(payloadDir);
      const result = await installer.install({ gameDir: game.dir, payload, scan: game.scan });
      await refresh();
      return result;
    },
    repair: async id => {
      const game = findGame(id);
      const payload = requirePayload(payloadDir);
      const result = await installer.repair({ gameDir: game.dir, payload, scan: game.scan });
      await refresh();
      return result;
    },
    uninstall: async (id, removeSettings = false) => {
      const game = findGame(id);
      const result = await installer.uninstall({ gameDir: game.dir, removeSettings, scan: game.scan });
      await refresh();
      return result;
    },
    diagnose: async id => {
      const game = findGame(id);
      const payload = inspectPayload(payloadDir, { allowMissingBundle: true });
      const mapped = Object.fromEntries(payload.files.map(row => [row.kind, row]));
      return installer.diagnose({ gameDir: game.dir, payload: mapped, scan: game.scan });
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
    applyRecommended: async id => {
      const game = findGame(id);
      if (!readManifest(game.dir)) throw appError('ERR_NOT_INSTALLED');
      const file = path.join(path.dirname(game.scan.chosen.path), 'nr_before_sr.ini');
      return writeConfig(file, recommendedPatch());
    }
  };
}

module.exports = { createAppService };
