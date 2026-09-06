'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { pathToFileURL } = require('url');

const art = require('../../vendor/DLSS5-Swapper/src/steamart');

function keyFor(dir) {
  return crypto.createHash('sha1').update(path.resolve(dir).toLowerCase()).digest('hex').slice(0, 20);
}

function createArtService({ userData }) {
  const artDir = path.join(userData, 'art');

  async function fetchGameArt(game) {
    if (!game || typeof game.dir !== 'string') return null;
    const key = keyFor(game.dir);
    const cover = path.join(artDir, `${key}-cover.jpg`);
    try {
      // Keep a previous good cover, but discard Steam's tiny placeholder or a
      // truncated download so a later refresh can obtain the HD asset again.
      if (fs.statSync(cover).size >= 8 * 1024) return pathToFileURL(cover).href;
      fs.unlinkSync(cover);
    } catch {}

    // Steam entries already carry an authoritative app id. This avoids the
    // name search used for loose/Epic/GOG games and keeps the common path fast.
    const hit = game.launcher === 'Steam' && game.id
      ? {
          coverUrl: `https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/${game.id}/library_600x900.jpg`
        }
      : await art.look(game.name || path.basename(game.dir), game.id || null);
    if (!hit || !hit.coverUrl) return null;

    try {
      await art.download(hit.coverUrl, cover);
      return pathToFileURL(cover).href;
    } catch {
      try { if (fs.existsSync(cover)) fs.unlinkSync(cover); } catch {}
      return null;
    }
  }

  return { fetchGameArt };
}

module.exports = { createArtService, keyFor };
