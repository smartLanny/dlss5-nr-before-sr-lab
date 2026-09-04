'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { PAYLOAD_FILES } = require('../src/product/constants');
const { createBundle, inspectPayload } = require('../src/product/payload');

test('requires every payload file and detects tampering', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'xiaofeng-payload-'));
  for (const name of Object.values(PAYLOAD_FILES)) fs.writeFileSync(path.join(dir, name), name);
  const bundle = createBundle(dir);
  fs.writeFileSync(path.join(dir, 'bundle.json'), JSON.stringify(bundle));
  assert.equal(inspectPayload(dir).ready, true);
  fs.appendFileSync(path.join(dir, PAYLOAD_FILES.bridge), 'changed');
  const changed = inspectPayload(dir);
  assert.equal(changed.ready, false);
  assert.deepEqual(changed.invalid, [PAYLOAD_FILES.bridge]);
});
