'use strict';

function isDx11Only(chosen) {
  const label = String(chosen && chosen.apiLabel || '');
  return /DirectX\s*11/i.test(label) && !/DirectX\s*12/i.test(label);
}

function apiSupported(chosen, options = {}) {
  if (!chosen) return false;
  const label = String(chosen.apiLabel || '');
  // The stable NR addon path is validated for DX12 only. A Unity title that
  // exposes both runtime backends is acceptable because it carries explicit
  // DX12 evidence; DX11 is enabled only when the caller selected the
  // separately packaged experimental carrier slot.
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
