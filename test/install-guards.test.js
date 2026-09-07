'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('path');
const { createInstallGuards } = require('../src/core/install-guards');
const { normalizeError } = require('../src/product/errors');

const root = path.resolve('guard-fixture-game');
const game = path.join(root, 'NBA2K27.exe');
const manager = path.join(root, 'Manager', 'DLSS 5 AI 超分管理器.exe');
const portable = path.join(root, 'DLSS5-Manager-portable-renamed.exe');

function row(ProcessId, ParentProcessId, Name, ExecutablePath) {
  return { ProcessId, ParentProcessId, Name, ExecutablePath };
}

test('Manager process family is ignored only with exact path and process relationships', () => {
  const rows = [
    row(100, 90, 'DLSS 5 AI 超分管理器.exe', manager.toUpperCase()),
    row(101, 100, 'DLSS 5 AI 超分管理器.exe', manager.toUpperCase()),
    row(102, 101, 'DLSS 5 AI 超分管理器.exe', manager),
    row(90, 1, 'renamed-portable.exe', portable.toUpperCase()),
    row(110, 100, 'NBA2K27.exe', game),
    row(111, 100, 'helper.exe', path.join(root, 'tools', 'helper.exe')),
    row(112, 100, 'DLSS 5 AI 超分管理器.exe', path.join(root, 'other', 'DLSS 5 AI 超分管理器.exe')),
    row(114, 1, 'DLSS 5 AI 超分管理器.exe', manager)
  ];
  const guards = createInstallGuards({
    processId: 100,
    executablePath: manager,
    portableExecutablePath: portable,
    queryProcesses: async () => rows
  });
  assert.deepEqual(guards.matchingProcesses(rows, root, game).map(item => item.ProcessId), [110, 111, 112, 114]);
});

test('assertion succeeds when only the proven Manager process family is present', async () => {
  const rows = [
    row(100, 90, 'DLSS 5 AI 超分管理器.exe', manager),
    row(101, 100, 'DLSS 5 AI 超分管理器.exe', manager),
    row(102, 101, 'DLSS 5 AI 超分管理器.exe', manager),
    row(90, 1, 'renamed-portable.exe', portable)
  ];
  const guards = createInstallGuards({
    processId: 100,
    executablePath: manager,
    portableExecutablePath: portable,
    queryProcesses: async () => rows
  });
  await guards.assertGameClosed(root, game);
});

test('portable environment path is not trusted without an exact direct-parent match', () => {
  const rows = [
    row(100, 80, 'DLSS 5 AI 超分管理器.exe', manager),
    row(80, 1, 'powershell.exe', path.join(root, 'powershell.exe')),
    row(90, 1, 'DLSS5-Manager-portable-renamed.exe', portable)
  ];
  const guards = createInstallGuards({
    processId: 100,
    executablePath: manager,
    portableExecutablePath: portable
  });
  assert.deepEqual(guards.matchingProcesses(rows, root, game).map(item => item.ProcessId), [80, 90]);
});

test('selected executable identity is never hidden by Manager path exemptions', () => {
  const rows = [
    row(100, 90, 'DLSS 5 AI 超分管理器.exe', manager),
    row(101, 100, 'DLSS 5 AI 超分管理器.exe', manager),
    row(90, 1, 'renamed-portable.exe', portable)
  ];
  const guards = createInstallGuards({
    processId: 100,
    executablePath: manager,
    portableExecutablePath: portable
  });
  assert.deepEqual(guards.matchingProcesses(rows, path.dirname(manager), manager).map(item => item.ProcessId), [100, 101]);
  assert.deepEqual(guards.matchingProcesses(rows, root, portable).map(item => item.ProcessId), [90]);
});

test('real, unrelated and path-hidden game processes remain blocked with useful details', async () => {
  const rows = [
    row(100, 90, 'DLSS 5 AI 超分管理器.exe', manager),
    row(90, 1, 'renamed-portable.exe', portable),
    row(110, 100, 'NBA2K27.exe', game),
    row(111, 1, 'helper.exe', path.join(root, 'tools', 'helper.exe')),
    row(112, 1, 'NBA2K27.exe', null),
    row(113, 1, 'dlss5-feed-host64.exe', null)
  ];
  const guards = createInstallGuards({
    processId: 100,
    executablePath: manager,
    portableExecutablePath: portable,
    queryProcesses: async () => rows
  });
  let caught;
  try { await guards.assertGameClosed(root, game); } catch (error) { caught = error; }
  assert.equal(caught && caught.code, 'errGameRunning');
  assert.deepEqual(caught.details.processes, [
    { pid: 110, name: 'NBA2K27.exe' },
    { pid: 111, name: 'helper.exe' },
    { pid: 112, name: 'NBA2K27.exe' },
    { pid: 113, name: 'dlss5-feed-host64.exe' }
  ]);
  assert.equal(JSON.stringify(caught.details).includes(root), false);
  const normalized = normalizeError(caught);
  assert.deepEqual(normalized.details, caught.details);
  assert.match(normalized.message, /NBA2K27\.exe/);
});

test('process-query failure remains a conservative process-check error', async () => {
  const cause = new Error('access denied');
  const guards = createInstallGuards({ queryProcesses: async () => { throw cause; } });
  await assert.rejects(guards.assertGameClosed(root, game), error =>
    error.code === 'errProcessCheck' && error.cause === cause);

  const invalid = createInstallGuards({ queryProcesses: async () => null });
  await assert.rejects(invalid.assertGameClosed(root, game), { code: 'errProcessCheck' });
});
