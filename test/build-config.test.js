'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const packageJson = require('../package.json');

test('Windows builds request administrator privileges for game-directory writes', () => {
  assert.equal(packageJson.build.win.requestedExecutionLevel, 'requireAdministrator');
});
