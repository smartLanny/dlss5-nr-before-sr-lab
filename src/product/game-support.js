'use strict';

function classifyApi(chosen) {
  if (!chosen) return 'unknown';
  const resolved = chosen.apiResolution && chosen.apiResolution.api;
  if (['dx11', 'dx12', 'mixed', 'unknown', 'unsupported'].includes(resolved)) return resolved;
  const label = String(chosen.apiLabel || '');
  const compactMixed = /(?:DirectX|D3D|DX)\s*11\s*[/&,或-]\s*(?:(?:DirectX|D3D|DX)\s*)?12\b/i.test(label);
  const dx11 = chosen.dx11 === true || /(?:DirectX|D3D|DX)\s*11\b/i.test(label);
  const dx12 = chosen.dx12 === true || /(?:DirectX|D3D|DX)\s*12\b/i.test(label) || compactMixed;
  if (dx11 && dx12) return 'mixed';
  if (chosen.api && !['dxgi', 'unknown'].includes(String(chosen.api).toLowerCase())) return 'unsupported';
  if (dx12) return 'dx12';
  // A scanner's import/string result is a capability hint, not a runtime
  // choice. In particular, a D3D11 bootstrapper may load a D3D12 engine.
  if (dx11) return chosen.via || chosen.dynamic ? 'unknown' : 'dx11';
  return 'unknown';
}

function isDx11Only(chosen) {
  return classifyApi(chosen) === 'dx11';
}

function apiSupported(chosen, options = {}) {
  if (!chosen) return false;
  // D3D12 remains supported by the ordinary core path. Pure DX11 is admitted
  // only for the compatibility payload and still requires an existing native
  // or mod-provided nvngx_dlss.dll; this is not a no-DLSS/Feeder route.
  const api = classifyApi(chosen);
  return api === 'dx12' || (options.allowDx11 === true && api === 'dx11');
}

function assess(scan, options = {}) {
  if (!scan || !scan.chosen) return { supported: false, code: 'ERR_NO_GAME_EXE' };
  if (scan.chosen.emulator || scan.emulator) return { supported: false, code: 'ERR_EMULATOR_UNSUPPORTED' };
  if (Number(scan.chosen.bitness) !== 64) return { supported: false, code: 'ERR_UNSUPPORTED_BITNESS' };
  if (['mixed', 'unknown'].includes(classifyApi(scan.chosen))) return { supported: false, code: 'ERR_API_SELECTION_REQUIRED' };
  if (!apiSupported(scan.chosen, options)) return { supported: false, code: 'ERR_UNSUPPORTED_API' };
  if (!scan.primaryDlss || !/^nvngx_dlss\.dll$/i.test(scan.primaryDlss.name || '')) {
    return { supported: false, code: 'ERR_NO_DLSS' };
  }
  if (isDx11Only(scan.chosen) && scan.componentSelection && scan.componentSelection.dx11Carrier === false) {
    return { supported: false, code: 'ERR_CARRIER_NOT_SELECTED' };
  }
  return { supported: true, code: null };
}

module.exports = { assess, apiSupported, isDx11Only, classifyApi };
