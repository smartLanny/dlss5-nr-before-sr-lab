'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { assess } = require('../src/product/game-support');

const base = {
  chosen: { bitness: 64, api: 'dxgi', apiLabel: 'DirectX 12', emulator: null },
  primaryDlss: { name: 'nvngx_dlss.dll' },
  emulator: null
};

test('accepts only native x64 DX12 DLSS games for the current addon path', () => {
  assert.deepEqual(assess(base), { supported: true, code: null });
  assert.equal(assess({ ...base, chosen: { ...base.chosen, apiLabel: 'DirectX 11' } }).code, 'ERR_UNSUPPORTED_API');
  assert.deepEqual(assess({ ...base, chosen: { ...base.chosen, apiLabel: 'DirectX 11' } }, { allowDx11: true }), { supported: true, code: null });
  assert.deepEqual(assess({ ...base, chosen: { ...base.chosen, apiLabel: 'DirectX 11/12', dx12: true } }), { supported: true, code: null });
  assert.equal(assess({ ...base, chosen: { ...base.chosen, bitness: 32 } }).code, 'ERR_UNSUPPORTED_BITNESS');
  assert.equal(assess({ ...base, chosen: { ...base.chosen, api: 'vulkan', apiLabel: 'Vulkan' } }).code, 'ERR_UNSUPPORTED_API');
  assert.equal(assess({ ...base, primaryDlss: null }).code, 'ERR_NO_DLSS');
});
