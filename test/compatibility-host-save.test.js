'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { createRequire } = require('node:module');
const mainFile = path.resolve(__dirname, '../main.js');
const source = fs.readFileSync(mainFile, 'utf8').replace(/\r\n/g, '\n');
const start = source.indexOf('writePackage: async (');
const end = source.indexOf('\n      }\n    });', start);
assert.ok(start > 0 && end > start, 'The actual main-process feedback save adapter is present');
const adapterSource = source.slice(start + 'writePackage: '.length, end) + '\n      }';

function host(t, select) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'compat-host-save-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const revealed = [];
  const save = vm.runInNewContext('(' + adapterSource + ')', {
    dialog: { showSaveDialog: select }, win: {}, app: { getPath: () => directory },
    path, fs, require: createRequire(mainFile), shell: { showItemInFolder: file => revealed.push(file) }, Error, Object
  });
  return { save, directory, revealed };
}

test('actual host save revalidates after the dialog and preserves the selected bytes', async t => {
  let file, selected = false, validations = 0;
  const h = host(t, async () => { selected = true; return { filePath: file }; });
  file = path.join(h.directory, 'feedback.zip'); const bytes = Buffer.from('fixture package bytes');
  const result = await h.save({ filename: 'feedback.zip', bytes, validateBeforeWrite: async () => { assert.equal(selected, true); validations++; } });
  assert.equal(result.saved, true); assert.equal(validations, 1);
  assert.deepEqual(fs.readFileSync(file), bytes); assert.deepEqual(h.revealed, [file]);
});

test('actual host save cannot write when identity changes while selecting the destination', async t => {
  let file, changed = false;
  const h = host(t, async () => { changed = true; return { filePath: file }; });
  file = path.join(h.directory, 'stale.zip');
  await assert.rejects(h.save({ filename: 'stale.zip', bytes: Buffer.from('stale'), validateBeforeWrite: async () => {
    if (changed) throw Object.assign(new Error('session changed'), { code: 'SESSION_CHANGED' });
  } }), { code: 'SESSION_CHANGED' });
  assert.equal(fs.existsSync(file), false); assert.equal(h.revealed.length, 0);
});

test('actual host cancellation performs no validation or write', async t => {
  const h = host(t, async () => ({ canceled: true }));
  const result = await h.save({ filename: 'cancelled.zip', bytes: Buffer.from('none'), validateBeforeWrite: async () => assert.fail('Cancelled save') });
  assert.equal(result.cancelled, true); assert.deepEqual(fs.readdirSync(h.directory), []);
});

test('actual host refuses an existing file with a useful message and never overwrites it', async t => {
  let file;
  const h = host(t, async () => ({ filePath: file }));
  file = path.join(h.directory, 'existing.zip'); fs.writeFileSync(file, 'original');
  await assert.rejects(h.save({ filename: 'existing.zip', bytes: Buffer.from('replacement'), validateBeforeWrite: async () => {} }), {
    code: 'COMPATIBILITY_FILE_EXISTS', message: '保存位置已有同名文件，请选择新的文件名。'
  });
  assert.equal(fs.readFileSync(file, 'utf8'), 'original'); assert.equal(h.revealed.length, 0);
});
