const test = require('node:test');
const assert = require('node:assert/strict');
const { resolveVersion } = require('../src/product/version-selection');

test('a game lock wins over a newly changed global addon selection', () => {
  assert.equal(resolveVersion({
    game: { addonVersion: '0.3.3.4' },
    globalVersion: 'imported-new-ota',
    dx11Version: 'dx11'
  }), '0.3.3.4');
});

test('an explicit version apply overrides the existing game lock', () => {
  assert.equal(resolveVersion({
    game: { addonVersion: '0.3.3.4' },
    requestedVersion: 'imported-new-ota',
    globalVersion: '0.4.1-r2',
    dx11Version: 'dx11'
  }), 'imported-new-ota');
});

test('DX11-only games keep their dedicated compatibility version', () => {
  assert.equal(resolveVersion({
    game: { addonVersion: '0.3.3.4' },
    requestedVersion: '0.4.5',
    globalVersion: '0.4.1-r2',
    dx11Version: 'dx11',
    dx11Only: true
  }), 'dx11');
});
