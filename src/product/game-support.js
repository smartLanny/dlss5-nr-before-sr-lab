'use strict';

function apiSupported(chosen) {
  if (!chosen) return false;
  const label = String(chosen.apiLabel || '');
  return chosen.api === 'dxgi' || /DirectX\s*(?:11|12|11\/12)/i.test(label);
}

function assess(scan) {
  if (!scan || !scan.chosen) return { supported: false, code: 'ERR_NO_GAME_EXE' };
  if (scan.chosen.emulator || scan.emulator) return { supported: false, code: 'ERR_EMULATOR_UNSUPPORTED' };
  if (Number(scan.chosen.bitness) !== 64) return { supported: false, code: 'ERR_UNSUPPORTED_BITNESS' };
  if (!apiSupported(scan.chosen)) return { supported: false, code: 'ERR_UNSUPPORTED_API' };
  if (!scan.primaryDlss || !/^nvngx_dlss\.dll$/i.test(scan.primaryDlss.name || '')) {
    return { supported: false, code: 'ERR_NO_DLSS' };
  }
  return { supported: true, code: null };
}

module.exports = { assess, apiSupported };
