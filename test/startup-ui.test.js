'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { attach } = require('../src/renderer/startup-ui');
const ok = value => ({ ok: true, value });
const normal = { mode: 'normal', sandbox: true, privilege: 'standard', canRestartElevated: true, restarting: false };

function fixture(manager) {
  const ids = ['startupSettings', 'startupMode', 'startupPrivilege', 'startupModeNote', 'startupMessage', 'startupRefreshBtn', 'startupRestartAdminBtn'];
  const elements = Object.fromEntries(ids.map(id => [id, { textContent: '', hidden: false, disabled: false, attrs: {}, events: {},
    classList: { toggle() {} }, setAttribute(name, value) { this.attrs[name] = value; }, addEventListener(name, listener) { this.events[name] = listener; } }]));
  const document = { getElementById: id => elements[id] || null };
  return { elements, controller: attach(document, manager) };
}

test('normal startup shows ordinary privilege without requesting elevation', async () => {
  let writes = 0;
  const f = fixture({ getStartupContext: async () => ok(normal), restartElevated: async () => { writes++; } });
  await f.controller.ready;
  assert.equal(f.elements.startupMode.textContent, '正常启动 · 默认沙箱'); assert.equal(f.elements.startupPrivilege.textContent, '普通权限');
  assert.equal(f.elements.startupModeNote.hidden, true); assert.equal(f.elements.startupRestartAdminBtn.disabled, false); assert.equal(writes, 0);
});

test('compatibility and administrator modes are explicit and an administrator cannot trigger a redundant restart', async () => {
  let writes = 0;
  const f = fixture({ getStartupContext: async () => ok({ ...normal, mode: 'compatibility', sandbox: false, privilege: 'administrator' }), restartElevated: async () => { writes++; } });
  await f.controller.ready;
  assert.equal(f.elements.startupMode.textContent, '临时兼容启动'); assert.equal(f.elements.startupPrivilege.textContent, '管理员权限');
  assert.equal(f.elements.startupModeNote.hidden, false); assert.match(f.elements.startupModeNote.textContent, /本次.*关闭.*沙箱/);
  await f.elements.startupRestartAdminBtn.events.click(); assert.equal(writes, 0);
});

test('one explicit click sends confirm once, keeps the old-window status visible and reports a failed handoff', async () => {
  const requests = []; let complete;
  const f = fixture({ getStartupContext: async () => ok(normal), restartElevated: request => {
    requests.push(request); return new Promise(resolve => { complete = resolve; });
  } }); await f.controller.ready;
  const pending = f.elements.startupRestartAdminBtn.events.click();
  await f.elements.startupRestartAdminBtn.events.click();
  assert.deepEqual(requests, [{ confirm: true }]); assert.equal(f.elements.startupRestartAdminBtn.disabled, true);
  assert.match(f.elements.startupMessage.textContent, /当前窗口会保持打开/);
  complete({ ok: false, error: { code: 'STARTUP_CHILD_NOT_READY', message: '新窗口未就绪。' } }); await pending;
  assert.match(f.elements.startupMessage.textContent, /当前窗口仍保留/); assert.match(f.elements.startupMessage.textContent, /新窗口未就绪/);
  assert.equal(f.elements.startupMessage.attrs.role, 'alert'); assert.equal(f.elements.startupRestartAdminBtn.disabled, false);
});

test('unknown privilege and an unreleased child lock remain distinct and refresh can restore the button', async () => {
  let state = { ...normal, privilege: 'unknown', restarting: true };
  const f = fixture({ getStartupContext: async () => ok(state), restartElevated: async () => ok({ restarting: true }) }); await f.controller.ready;
  assert.equal(f.elements.startupPrivilege.textContent, '暂未确认'); assert.equal(f.elements.startupRestartAdminBtn.disabled, true);
  assert.match(f.elements.startupMessage.textContent, /暂不接受修改/);
  state = normal; await f.elements.startupRefreshBtn.events.click(); assert.equal(f.elements.startupRestartAdminBtn.disabled, false);
});

test('an unavailable status interface displays a readable local recovery path', async () => {
  const f = fixture({}); await f.controller.ready;
  assert.match(f.elements.startupMessage.textContent, /启动诊断\.cmd/); assert.equal(f.elements.startupRestartAdminBtn.disabled, true);
});

test('settings include one independent startup panel and script without changing the existing scan controls', () => {
  const html = fs.readFileSync(path.resolve(__dirname, '../src/renderer/index.html'), 'utf8');
  assert.equal((html.match(/id="startupSettings"/g) || []).length, 1);
  assert.ok(html.indexOf('id="view-settings"') < html.indexOf('id="startupSettings"'));
  assert.match(html, /<script src="startup-ui\.js"><\/script>/); assert.match(html, /id="scanDrivesToggle"/);
});
