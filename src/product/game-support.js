'use strict';

function isDx11Only(chosen) {
  const label = String(chosen && chosen.apiLabel || '');
  return /DirectX\s*11/i.test(label) && !/DirectX\s*12/i.test(label);
}

function apiSupported(chosen, options = {}) {
  if (!chosen) return false;
  const label = String(chosen.apiLabel || '');
  // D3D12 remains supported by the ordinary core path. Pure DX11 is admitted
  // only for the compatibility payload and still requires an existing native
  // or mod-provided nvngx_dlss.dll; this is not a no-DLSS/Feeder route.
  return chosen.api === 'dxgi' && (/DirectX\s*12/i.test(label) || chosen.dx12 === true ||
    (options.allowDx11 === true && isDx11Only(chosen)));
}

function assess(scan, options = {}) {
  if (!scan || !scan.chosen) return { supported: false, code: 'ERR_NO_GAME_EXE' };
  if (scan.chosen.emulator || scan.emulator) return { supported: false, code: 'ERR_EMULATOR_UNSUPPORTED' };
  if (Number(scan.chosen.bitness) !== 64) return { supported: false, code: 'ERR_UNSUPPORTED_BITNESS' };
  if (!apiSupported(scan.chosen, options)) return { supported: false, code: 'ERR_UNSUPPORTED_API' };
  if (!scan.primaryDlss || !/^nvngx_dlss\.dll$/i.test(scan.primaryDlss.name || '')) {
    return { supported: false, code: 'ERR_NO_DLSS' };
  }
  return { supported: true, code: null };
}

module.exports = { assess, apiSupported, isDx11Only };
