'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseSection, updateSection, writeConfig, writeDefaultConfig, readConfig } = require('../src/product/nr-config');

test('parses and normalizes public NR settings', () => {
  const value = parseSection('[NRBeforeSR]\nEnabled=1\nMode=9\nIntensity=9\nWorkMode=7\nCustomWorkScale=0.1\nStyle=2\nAutoMask=0\nColorStrength=2\nSkinStructureStrength=3\n');
  assert.deepEqual(value, { Enabled: 1, Mode: 2, Intensity: 2, WorkMode: 5, CustomWorkScale: 0.25, Style: 2, AutoMask: 0, ColorStrength: 1, SkinStructureStrength: 2 });
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
  await writeConfig(file, { Enabled: 1, Intensity: 1.2, WorkMode: 2, CustomWorkScale: 0.75, Style: 1, AutoMask: 1, ColorStrength: 0.5, SkinStructureStrength: 0.8, TransferStrength: 2.5, PostTransferStrength: 0.66 });
  assert.deepEqual(readConfig(file), {
    Enabled: 1, Intensity: 1.2, WorkMode: 2, CustomWorkScale: 0.75,
    Style: 1, AutoMask: 1, ColorStrength: 0.5, SkinStructureStrength: 0.8,
    TransferStrength: 2.5, PostTransferStrength: 0.66,
    capabilities: {
      WorkMode: true, CustomWorkScale: true, ColorStrength: true,
      SkinStructureStrength: true, TransferStrength: true, PostTransferStrength: true
    }
  });
});

test('keeps legacy 0.2 and 0.3 configs readable while clamping old intensity above the current core range', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-legacy-ini-'));
  const file = path.join(dir, 'nr_before_sr.ini');
  fs.writeFileSync(file, '[NRBeforeSR]\nMode=2\nIntensity=2.75\nTransferStrength=1\nPostTransferStrength=1\nColorStrength=0\n');

  const value = readConfig(file);
  assert.equal(value.Intensity, 2);
  assert.equal(value.TransferStrength, 1);
  assert.equal(value.PostTransferStrength, 1);
  assert.deepEqual(value.capabilities, {
    WorkMode: false,
    CustomWorkScale: false,
    ColorStrength: true,
    SkinStructureStrength: false,
    TransferStrength: true,
    PostTransferStrength: true
  });
});

test('writing model strength or shared effect strength preserves color strength', () => {
  const original = '[NRBeforeSR]\nIntensity=1.25\nTransferStrength=3.25\nPostTransferStrength=0.66\nColorStrength=0.2\n';
  const edited = updateSection(original, {
    Intensity: 1.75,
    TransferStrength: 2.5,
    PostTransferStrength: 2.5
  });
  const value = parseSection(edited);

  assert.equal(value.Intensity, 1.75);
  assert.equal(value.TransferStrength, 2.5);
  assert.equal(value.PostTransferStrength, 2.5);
  assert.equal(value.ColorStrength, 0.2);
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
  assert.equal(defaultPatch('0.4.7beta').Intensity, 1.2);
  assert.equal(defaultPatch('0.4.7beta').ColorStrength, 1);
  assert.equal(defaultPatch('0.4.6-hotfix.1').Intensity, 1);
});

test('ordinary NR edits preserve jitter choices and reject developer or policy keys', () => {
  const original = '[NRBeforeSR]\nIntensity=1\nJitterMode=1\nJitterPolicyVersion=1\n';
  assert.match(updateSection(original, { Intensity: 1.2 }), /JitterMode=1\nJitterPolicyVersion=1/);
  for (const key of ['JitterMode', 'JitterPolicyVersion']) {
    assert.throws(() => updateSection(original, { [key]: 0 }), { code: 'ERR_BAD_REQUEST' });
  }
});

test('explicit default write disables jitter and preserves its policy marker and other sections', async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-jitter-reset-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const file = path.join(directory, 'nr_before_sr.ini');
  const original = '; keep\r\n[Other]\r\nJitterMode=1\r\n[NRBeforeSR]\r\n' +
    '; experiment\r\njittermode = 1\r\nJitterMode=1\r\nJitterPolicyVersion=3\r\n' +
    'Intensity=1.7\r\nProviderValue=keep\r\n';
  fs.writeFileSync(file, original);
  await writeDefaultConfig(file, { Intensity: 1.2 });
  const saved = fs.readFileSync(file, 'utf8');
  assert.match(saved, /\[Other\]\r\nJitterMode=1/);
  assert.match(saved, /; experiment\r\nJitterMode = 0\r\nJitterMode=0/);
  assert.match(saved, /JitterPolicyVersion=3/);
  assert.match(saved, /Intensity=1.2\r\nProviderValue=keep/);
  assert.equal(fs.existsSync(`${file}.xiaofeng.tmp`), false);
  await writeConfig(file, { Style: 1 });
  assert.match(fs.readFileSync(file, 'utf8'), /JitterPolicyVersion=3/);
});

test('default write does not invent or complete the Core migration marker', async t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-jitter-policy-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const file = path.join(directory, 'nr_before_sr.ini');
  await writeDefaultConfig(file, { Enabled: 1 });
  assert.match(fs.readFileSync(file, 'utf8'), /JitterMode=0/);
  assert.doesNotMatch(fs.readFileSync(file, 'utf8'), /JitterPolicyVersion/);
  fs.writeFileSync(file, '[NRBeforeSR]\nJitterMode=1\nJitterPolicyVersion=0\n');
  await writeDefaultConfig(file, { Enabled: 1 });
  assert.match(fs.readFileSync(file, 'utf8'), /JitterMode=0\nJitterPolicyVersion=0/);
  await assert.rejects(writeDefaultConfig(file, { JitterMode: 1 }), { code: 'ERR_BAD_REQUEST' });
});
