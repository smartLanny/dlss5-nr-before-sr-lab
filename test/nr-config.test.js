'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseSection, updateSection, writeConfig, readConfig } = require('../src/product/nr-config');

test('parses and normalizes public NR settings', () => {
  const value = parseSection('[NRBeforeSR]\nEnabled=1\nMode=9\nIntensity=3.7\nStyle=2\nAutoMask=0\n');
  assert.deepEqual(value, { Enabled: 1, Mode: 2, Intensity: 2, Style: 2, AutoMask: 0 });
});

test('updates only the target section and preserves comments/other sections', () => {
  const input = '; keep\r\n[Other]\r\nEnabled=9\r\n[NRBeforeSR]\r\n; note\r\nIntensity=1.0\r\n';
  const output = updateSection(input, { Intensity: 1.2, Mode: 2 });
  assert.match(output, /\[Other\]\r\nEnabled=9/);
  assert.match(output, /; note\r\nIntensity=1.2\r\nMode=2/);
});

test('atomically writes and reads a new config', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-ini-'));
  const file = path.join(dir, 'nr_before_sr.ini');
  await writeConfig(file, { Enabled: 1, Mode: 2, Intensity: 1.2, Style: 1, AutoMask: 1 });
  assert.deepEqual(readConfig(file), { Enabled: 1, Mode: 2, Intensity: 1.2, Style: 1, AutoMask: 1 });
});
