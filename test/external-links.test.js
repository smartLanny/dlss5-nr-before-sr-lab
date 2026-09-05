'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { configuredHttpsLink, openConfiguredLink } = require('../src/product/external-links');
const branding = require('../product.json');
const { PRODUCT } = require('../src/product/constants');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

function definition(source, marker) {
  const start = source.indexOf(marker);
  assert.notEqual(start, -1, 'production homepage controller must exist');
  let end = source.indexOf('{', start) + 1, depth = 1;
  for (; depth && end < source.length; ++end) {
    depth += (source[end] === '{') - (source[end] === '}');
  }
  assert.equal(depth, 0);
  return source.slice(start, end);
}
function controller(product, open) {
  const classes = new Set(), notes = [], calls = [];
  const button = { hidden: false, disabled: false, title: '', attributes: {},
    classList: { toggle(name, value) { if (value) classes.add(name); else classes.delete(name); } },
    setAttribute(name, value) { this.attributes[name] = value; } };
  const state = { busy: true }; // A browser launch must not take or release the install lock.
  const context = vm.createContext({
    $: id => { assert.equal(id, 'bilibili'); return button; }, state,
    api: { openExternal: key => { calls.push(key); return open(key); } },
    note: (message, error) => notes.push({ message, error }),
    unwrap: result => { if (!result?.ok) throw new Error('IPC failed'); return result.value; },
  });
  vm.runInContext(definition(read('src/renderer/renderer.js'), 'function configureHomepage('), context);
  context.configureHomepage(product);
  return { button, classes, notes, calls, state };
}

test('only configured HTTPS product keys are resolvable', async () => {
  assert.equal(configuredHttpsLink(branding, 'bilibiliUrl'), branding.bilibiliUrl);
  for (const value of [null, undefined, '', ' ', 42, {}, 'http://example.test', 'file:///tmp/page',
    'javascript:alert(1)', 'https://', 'https:///example.test', 'https://bad host/',
    'https://user:password@example.test/', 'https://example.test/\nnext', 'https:\\example.test',
    'https://example.test/' + 'x'.repeat(2048)]) {
    assert.equal(configuredHttpsLink({ bilibiliUrl: value }, 'bilibiliUrl'), '', String(value));
  }
  assert.equal(configuredHttpsLink({ releaseUrl: 'https://example.test/releases' }, 'releaseUrl'), 'https://example.test/releases');
  assert.equal(configuredHttpsLink({ unexpected: 'https://example.test' }, 'unexpected'), '');
  assert.equal(configuredHttpsLink(Object.create({ bilibiliUrl: branding.bilibiliUrl }), 'bilibiliUrl'), '');
  let calls = 0;
  for (const key of [null, undefined, {}, 'https://evil.test', 'unexpected']) {
    assert.equal(await openConfiguredLink(branding, key, async () => { ++calls; }), false);
  }
  assert.equal(calls, 0, 'renderer cannot pass arbitrary URLs as keys');
});

test('browser failures return false without changing other operations', async () => {
  const called = [];
  assert.equal(await openConfiguredLink(branding, 'bilibiliUrl', async url => called.push(url)), true);
  assert.deepEqual(called, [branding.bilibiliUrl]);
  assert.equal(await openConfiguredLink(branding, 'bilibiliUrl', async () => { throw new Error('OS failure'); }), false);
  assert.equal(await openConfiguredLink({ bilibiliUrl: '' }, 'bilibiliUrl', () => assert.fail('must not open')), false);
});

test('main IPC remains trusted and config-driven; renderer and preview agree', () => {
  const main = read('main.js'), renderer = read('src/renderer/renderer.js');
  assert.match(main, /if\(!trusted\(e\)\)/);
  assert.match(main, /openConfiguredLink\(service\.product,key/);
  assert.match(renderer, /configureHomepage\(d\.product\)/);
  assert.equal(renderer, read('preview/renderer.js'));
  assert.doesNotMatch(renderer, /space\.bilibili\.com/);
  assert.equal(PRODUCT.bilibiliUrl, configuredHttpsLink(branding, 'bilibiliUrl'));
  assert.equal(PRODUCT.releaseUrl, configuredHttpsLink(branding, 'releaseUrl'));
});

test('missing or invalid homepage configuration hides the persistent sidebar entry', async () => {
  for (const product of [{}, { bilibiliUrl: '' }, { bilibiliUrl: null }, { bilibiliUrl: 'javascript:bad' }]) {
    const ui = controller(product, () => assert.fail('hidden entry must not open'));
    assert.equal(ui.button.hidden, true);
    assert.equal(ui.classes.has('hidden'), true);
    await ui.button.onclick();
    assert.equal(ui.calls.length, 0);
    assert.equal(ui.state.busy, true);
  }
  const restored = controller(branding, async () => ({ ok: true, value: true }));
  assert.equal(restored.button.hidden, false);
  assert.equal(restored.classes.has('hidden'), false);
});

test('homepage click is deduplicated and does not enter the global operation lock', async () => {
  let resolve;
  const ui = controller(branding, () => new Promise(r => { resolve = r; }));
  const first = ui.button.onclick();
  await ui.button.onclick();
  assert.deepEqual(ui.calls, ['bilibiliUrl']);
  assert.equal(ui.button.disabled, true);
  assert.equal(ui.state.busy, true);
  resolve({ ok: true, value: true }); await first;
  assert.equal(ui.button.disabled, false);
  assert.equal(ui.state.busy, true);
  assert.equal(ui.notes.length, 0);
});

test('failed external opens use an inline message and remain retryable', async () => {
  for (const open of [async () => ({ ok: true, value: false }),
    async () => ({ ok: false, error: { message: 'internal path' } }),
    async () => { throw new Error('transport'); }]) {
    const ui = controller(branding, open);
    await ui.button.onclick();
    assert.equal(ui.notes.length, 1);
    assert.equal(ui.notes[0].error, true);
    assert.match(ui.notes[0].message, /主页/);
    assert.equal(ui.button.disabled, false);
    assert.equal(ui.state.busy, true);
    await ui.button.onclick(); assert.equal(ui.calls.length, 2);
  }
});
