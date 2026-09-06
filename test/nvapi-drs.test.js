'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { createNvapiDrs, parseJsonLine } = require('../src/product/nvapi-drs');

const helper = path.join(__dirname, '..', 'src', 'product', 'nvapi-drs.ps1');

test('NvAPI wrapper passes only the requested SR preset to the helper', async () => {
  let captured = null;
  const nvapi = createNvapiDrs({
    scriptPath: helper,
    platform: 'win32',
    exists: () => true,
    runner: async (_script, args) => {
      captured = args;
      return { stdout: '{"ok":true,"rawPreset":13,"profile":"Game"}\n', stderr: '', error: null };
    }
  });
  const result = await nvapi.applySrPreset({ exePath: 'C:\\Games\\Test\\game.exe', preset: 'm', friendlyName: 'Test' });
  assert.equal(result.ok, true);
  assert.equal(result.preset, 'm');
  assert.deepEqual(captured.slice(captured.indexOf('-Preset'), captured.indexOf('-Preset') + 2), ['-Preset', 'M']);
});

test('PowerShell helper contains SR-only Driver Settings ids', () => {
  const source = fs.readFileSync(helper, 'utf8');
  assert.match(source, /0x10E41E01/);
  assert.match(source, /0x10E41DF3/);
  assert.doesNotMatch(source, /0x10E41E02|0x10E41E03|0x10E41DF7|0x10E41DF1/);
});

test('PowerShell helper C# layout self-test compiles on Windows', { skip: process.platform !== 'win32' }, () => {
  const output = execFileSync('powershell.exe', [
    '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
    '-File', helper, '-Action', 'selftest'
  ], { encoding: 'utf8', timeout: 20000, windowsHide: true });
  const parsed = parseJsonLine(output);
  assert.equal(parsed.ok, true);
  assert.match(parsed.layout, /NVDRS_SETTING=12320/);
  assert.match(parsed.layout, /NVDRS_APPLICATION_V4=20492/);
});
