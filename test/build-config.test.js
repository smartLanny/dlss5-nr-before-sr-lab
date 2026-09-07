'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const packageJson = require('../package.json');
const { DX11_COMPAT_LABEL, DX11_COMPAT_CARRIER } = require('../src/product/constants');

test('Windows builds request administrator privileges for game-directory writes', () => {
  assert.equal(packageJson.build.win.requestedExecutionLevel, 'requireAdministrator');
});

test('portable startup hands off to a UAC-elevated process when needed', () => {
  const main = fs.readFileSync(require.resolve('../main.js'), 'utf8');
  assert.match(main, /Start-Process/);
  assert.match(main, /-Verb RunAs/);
  assert.match(main, /WindowsBuiltInRole/);
});

test('public DX11 compatibility identity and matched carrier name are exact', () => {
  assert.equal(DX11_COMPAT_LABEL, '0.4.5-DX11-兼容增强');
  assert.equal(DX11_COMPAT_CARRIER, 'dlss5-native-carrier-045-dx11-compat.addon64');
});
