'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const packageJson = require('../package.json');

test('Windows builds request administrator privileges for game-directory writes', () => {
  assert.equal(packageJson.build.win.requestedExecutionLevel, 'requireAdministrator');
});

test('portable startup hands off to a UAC-elevated process when needed', () => {
  const main = fs.readFileSync(require.resolve('../main.js'), 'utf8');
  assert.match(main, /Start-Process/);
  assert.match(main, /-Verb RunAs/);
  assert.match(main, /WindowsBuiltInRole/);
});
