'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { readWindow, readFeederEvidence } = require('../src/product/feeder-runtime-evidence');
const { DIRECTORY } = require('../src/product/feeder-runtime');

function fixture(t) {
  const exeDir = fs.mkdtempSync(path.join(os.tmpdir(), 'feeder-evidence-'));
  t.after(() => fs.rmSync(exeDir, { recursive: true, force: true }));
  const file = path.join(exeDir, DIRECTORY, 'addons/dlss5-feed.log'); fs.mkdirSync(path.dirname(file), { recursive: true });
  const lastLaunch = { pid: 42, startedAt: new Date(Date.now() - 10000).toISOString() };
  return { exeDir, file, lastLaunch, write: text => fs.writeFileSync(file, text), read: () => readFeederEvidence({ exeDir, lastLaunch }) };
}
const session = pid => `[nr-feeder-dx12-session] pid=${pid}\n`;
const complete = count => `[nr-feeder-dx12-completion] frame=${count} nr_completed=1 output_recorded=1 provenance=Synthetic\n`;

test('only this launch PID and a complete callback record establish processed status', async t => {
  const f = fixture(t); assert.equal((await f.read()).processed, 'unknown');
  for (const text of [complete(60), session(41) + complete(60), session(42) + complete(0), session(42) + complete(60).replace('output_recorded=1', 'output_recorded=0')]) {
    f.write(text); assert.equal((await f.read()).processed, 'unknown');
  }
  f.write(session(42) + complete(60)); assert.deepEqual(await f.read(), { loaded: true, processed: true, frame: 60, detail: '本次 NR 已完成并录制输出回填；真实画面仍需核对。' });
  fs.utimesSync(f.file, 0, 0); assert.equal((await f.read()).processed, 'unknown');
});

test('later retain or different session cannot inherit an old successful completion', async t => {
  const f = fixture(t); f.write(session(42) + complete(60) + '[nr-feeder-dx12-retained] reason=unsupported actual swapchain format\n');
  assert.equal((await f.read()).processed, false);
  fs.appendFileSync(f.file, session(43)); assert.equal((await f.read()).loaded, 'unknown');
  f.write(session(42)); assert.equal((await f.read()).loaded, true); assert.equal((await f.read()).processed, 'unknown');
});

test('bounded head and tail reading keeps session identity without joining distant partial lines', async t => {
  const f = fixture(t); f.write(session(42) + 'ordinary line\n'.repeat(7000) + complete(120));
  assert.equal((await f.read()).frame, 120);
  const text = await readWindow(f.file, 0); assert.ok(Buffer.byteLength(text) <= 52 * 1024 + 1);
  f.write(session(42) + '[nr-feeder-dx12-completion] frame=120 nr_completed=1 output_recorded=1 ' + 'x'.repeat(75000) + 'provenance=Synthetic\n');
  assert.equal((await f.read()).processed, 'unknown');
});

test('log changes during the extra head read fail closed and hardlinks are not read', async t => {
  const f = fixture(t); f.write(session(42) + 'ordinary line\n'.repeat(7000) + complete(120));
  const result = await readWindow(f.file, 0, async (...args) => {
    const handle = await fsp.open(...args);
    return { stat: () => handle.stat(), close: () => handle.close(), async read(...readArgs) {
      const got = await handle.read(...readArgs); if (readArgs[3] === 0) fs.appendFileSync(f.file, 'changed\n'); return got;
    } };
  });
  assert.equal(result, null);
  const link = path.join(f.exeDir, 'linked.log'); fs.linkSync(f.file, link);
  assert.equal(await readWindow(f.file, 0), null);
});
