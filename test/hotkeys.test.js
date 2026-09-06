'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseKeyOverlay, updateKeyOverlay, writeReShadeHotkey, readReShadeHotkey } = require('../src/product/hotkeys');

test('reads ReShade Home shortcut and modifier order', () => {
  assert.deepEqual(parseKeyOverlay('[INPUT]\nKeyOverlay=36,0,0,0\n'), {
    key: 36, ctrl: false, shift: false, alt: false, present: true
  });
  assert.deepEqual(parseKeyOverlay('[INPUT]\nKeyOverlay=113,0,1,0\n'), {
    key: 113, ctrl: false, shift: true, alt: false, present: true
  });
});

test('updates only ReShade INPUT KeyOverlay', () => {
  const input = '; keep\n[OTHER]\nKeyOverlay=1\n[INPUT]\n; note\nKeyOverlay=36,0,0,0\n';
  const output = updateKeyOverlay(input, { key: 36, ctrl: true, shift: true, alt: false });
  assert.match(output, /\[OTHER\]\nKeyOverlay=1/);
  assert.match(output, /; note\nKeyOverlay=36,1,1,0/);
});

test('atomically writes a ReShade shortcut', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-hotkey-'));
  const file = path.join(dir, 'ReShade.ini');
  await writeReShadeHotkey(file, { key: 35, ctrl: false, shift: true, alt: true });
  assert.deepEqual(readReShadeHotkey(file), {
    key: 35, ctrl: false, shift: true, alt: true, present: true
  });
});
