'use strict';

const { classifyApi } = require('./game-support');

const SUPPORTED = new Set(['dx9', 'dx10', 'dx11', 'dx12', 'vulkan']);

// The saved preference and the detected API are separate values. In particular,
// choosing auto must never reuse an effective API produced by a manual override.
function resolveOperationApi(game, request = {}) {
  const chosen = game?.scan?.chosen || game?.chosen || {};
  const preference = request.api ?? game?.apiOverride ??
    (chosen.apiResolution?.source === 'override' ? chosen.apiResolution.api : 'auto');
  const manuallyResolved = Boolean(game?.apiOverride && game.apiOverride !== 'auto') ||
    chosen.apiResolution?.source === 'override' || chosen.apiAssessment?.source === 'override';
  const detected = chosen.detectedApiResolution?.api || chosen.detectedApi ||
    (!manuallyResolved ? chosen.apiResolution?.api : null) ||
    (!manuallyResolved ? chosen.apiAssessment?.effectiveApi : null) ||
    classifyApi({ ...chosen, apiResolution: null });
  const effectiveApi = preference === 'auto' ? detected || 'unknown' : preference;
  return { api: preference, effectiveApi, detectedApi: detected || 'unknown', supported: SUPPORTED.has(effectiveApi),
    requiresManualSelection: ['mixed', 'unknown'].includes(effectiveApi) };
}

function requiresOperationApi(request) {
  if (request.uninstall) return false;
  return ['api', 'version', 'route', 'deployment', 'loadingMode', 'loadingBackend', 'proxyEntry', 'components', 'hoyo'].some(key => request[key] !== undefined) ||
    Boolean(request.nr && Object.keys(request.nr).length) ||
    Boolean(request.sr && request.sr.quality !== 'game') || Boolean(request.fg && request.fg.mode !== 'restore');
}

module.exports = { resolveOperationApi, requiresOperationApi };
