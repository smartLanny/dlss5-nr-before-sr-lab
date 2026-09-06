'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { executeUiAction } = require('../src/renderer/action-flow');

test('completed action stays successful when the follow-up refresh fails', async () => {
  const refreshError = new Error('temporary scan failure');
  const outcome = await executeUiAction(
    async () => 'installed',
    async () => { throw refreshError; }
  );

  assert.equal(outcome.completed, true);
  assert.equal(outcome.value, 'installed');
  assert.equal(outcome.refreshError, refreshError);
});

test('action errors are still surfaced as action failures', async () => {
  const actionError = new Error('write failed');
  const outcome = await executeUiAction(async () => { throw actionError; });

  assert.equal(outcome.completed, false);
  assert.equal(outcome.error, actionError);
});
