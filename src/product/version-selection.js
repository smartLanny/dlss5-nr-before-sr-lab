'use strict';

function resolveVersion({ game, requestedVersion = null, globalVersion = null, dx11Version, dx11Only = false }) {
  if (dx11Only) return dx11Version;
  if (requestedVersion) return requestedVersion;
  if (game && game.addonVersion) return game.addonVersion;
  return globalVersion || undefined;
}

module.exports = { resolveVersion };
