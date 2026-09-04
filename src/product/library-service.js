'use strict';

const path = require('path');
const crypto = require('crypto');
const { assess } = require('./game-support');
const { readManifest } = require('./manifest');
const { MESSAGES } = require('./errors');

function idFor(dir) {
  return crypto.createHash('sha1').update(path.resolve(dir).toLowerCase()).digest('hex').slice(0, 16);
}

async function mapLimit(items, limit, worker) {
  const result = new Array(items.length);
  let cursor = 0;
  async function run() {
    while (cursor < items.length) {
      const index = cursor++;
      try { result[index] = await worker(items[index], index); }
      catch (error) { result[index] = { error }; }
    }
  }
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, run));
  return result;
}

function createLibraryService(overrides = {}) {
  const library = overrides.library || require('../library');
  const scanModule = overrides.scan || require('../core/scan');

  async function scanAll(state) {
    const discovered = library.discover(
      state.scanFolders || [],
      state.scanDrives === true,
      state.excludedRoots || []
    );
    const manual = (state.manualGames || []).map(dir => ({
      launcher: '手动添加', id: null, name: path.basename(dir), dir, poster: null
    }));
    const candidates = library.dedupe([...(discovered.games || []), ...manual]);
    const rows = await mapLimit(candidates, 2, async game => {
      let scan;
      try {
        scan = await scanModule.scanGame(game.dir);
      } catch (error) {
        return {
          id: idFor(game.dir),
          name: game.name || path.basename(game.dir),
          launcher: game.launcher || '本地游戏',
          dir: game.dir,
          poster: game.poster && game.poster.file ? game.poster.file : null,
          chosen: null,
          supported: false,
          supportCode: 'ERR_INTERNAL',
          supportText: '扫描失败，请检查目录权限后重试。',
          installed: false,
          scan: null,
          scanError: error && error.message ? error.message : 'scan failed'
        };
      }
      const support = assess(scan);
      let manifest = null;
      try { manifest = readManifest(game.dir); } catch {}
      return {
        id: idFor(game.dir),
        name: game.name || scan.gameName || path.basename(game.dir),
        launcher: game.launcher || '本地游戏',
        dir: game.dir,
        poster: game.poster && game.poster.file ? game.poster.file : null,
        chosen: scan.chosen ? {
          path: scan.chosen.path,
          rel: scan.chosen.rel,
          api: scan.chosen.api,
          apiLabel: scan.chosen.apiLabel,
          bitness: scan.chosen.bitness
        } : null,
        supported: support.supported,
        supportCode: support.code,
        supportText: support.code ? MESSAGES[support.code] : '支持安装',
        installed: Boolean(manifest),
        scan
      };
    });

    return rows.filter(Boolean).sort((a, b) =>
      Number(b.installed) - Number(a.installed) ||
      Number(b.supported) - Number(a.supported) ||
      a.name.localeCompare(b.name, 'zh-CN', { numeric: true })
    );
  }

  return { scanAll, idFor };
}

module.exports = { createLibraryService, idFor, mapLimit };
