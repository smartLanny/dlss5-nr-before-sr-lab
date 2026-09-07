'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseSection, updateSection, writeConfig, readConfig } = require('../src/product/nr-config');

test('parses and normalizes public NR settings', () => {
  const value = parseSection('[NRBeforeSR]\nEnabled=1\nMode=9\nIntensity=9\nWorkMode=7\nCustomWorkScale=0.1\nStyle=2\nAutoMask=0\nColorStrength=2\nSkinStructureStrength=3\n');
  assert.deepEqual(value, { Enabled: 1, Mode: 2, Intensity: 8, WorkMode: 5, CustomWorkScale: 0.25, Style: 2, AutoMask: 0, ColorStrength: 1, SkinStructureStrength: 2 });
});

test('updates only the target section and preserves comments/other sections', () => {
  const input = '; keep\r\n[Other]\r\nEnabled=9\r\n[NRBeforeSR]\r\n; note\r\nIntensity=1.0\r\n';
  const output = updateSection(input, { Intensity: 1.2, WorkMode: 2 });
  assert.match(output, /\[Other\]\r\nEnabled=9/);
  assert.match(output, /; note\r\nIntensity=1.2\r\nWorkMode=2/);
});

test('atomically writes and reads a new config', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-ini-'));
  const file = path.join(dir, 'nr_before_sr.ini');
  await writeConfig(file, { Enabled: 1, Intensity: 1.2, WorkMode: 2, CustomWorkScale: 0.75, Style: 1, AutoMask: 1, ColorStrength: 0.5, SkinStructureStrength: 0.8 });
  assert.deepEqual(readConfig(file), {
    Enabled: 1, Intensity: 1.2, WorkMode: 2, CustomWorkScale: 0.75,
    Style: 1, AutoMask: 1, ColorStrength: 0.5, SkinStructureStrength: 0.8,
    capabilities: { WorkMode: true, CustomWorkScale: true, ColorStrength: true, SkinStructureStrength: true }
  });
});


test('recommended reset sets both final blend routes to one without confusing model or color strength', () => {
  const { defaultPatch } = require('../src/product/nr-config');
  const original = '[NRBeforeSR]\nTransferStrength=3.25\nPostTransferStrength=0.66\nColorStrength=0.2\nIntensity=2\nOtherValue=keep\n';
  const editedColor = updateSection(original, { ColorStrength: 0.6 });
  assert.match(editedColor, /TransferStrength=3.25/);
  assert.match(editedColor, /PostTransferStrength=0.66/);
  assert.equal(parseSection(editedColor).TransferStrength, 3.25, 'reading does not pretend an explicit high blend is one');
  const restored = parseSection(updateSection(original, defaultPatch()));
  assert.equal(restored.TransferStrength, 1);
  assert.equal(restored.PostTransferStrength, 1);
  assert.equal(restored.ColorStrength, 0.75);
  assert.match(updateSection(original, defaultPatch()), /OtherValue=keep/);
});
