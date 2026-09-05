'use strict';

const LINK_KEYS = new Set(['bilibiliUrl', 'releaseUrl']);

/** Resolve only a named product setting, never a URL supplied by the renderer. */
function configuredHttpsLink(product, key) {
  if (!LINK_KEYS.has(key) || !product || !Object.hasOwn(product, key)) return '';
  const value = product[key];
  if (typeof value !== 'string' || value.length > 2048 ||
      /[\u0000-\u0020\u007f\\]/u.test(value) || !/^https:\/\/[^/?#]+(?:[/?#]|$)/u.test(value)) return '';
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || !url.hostname || url.username || url.password) return '';
    return url.href;
  } catch {
    return '';
  }
}

/** Failure is local to this action; it must not become an install/scan failure. */
async function openConfiguredLink(product, key, openExternal) {
  const url = configuredHttpsLink(product, key);
  if (!url || typeof openExternal !== 'function') return false;
  try {
    await openExternal(url);
    return true;
  } catch {
    return false;
  }
}

module.exports = { configuredHttpsLink, openConfiguredLink };
