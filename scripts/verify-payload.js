'use strict';

const fs = require('fs');
const path = require('path');
const { createBundle, inspectPayload } = require('../src/product/payload');
const dir = path.join(__dirname, '..', 'payload', 'nr-before-sr');
const write = process.argv.includes('--write');

try {
  if (write) {
    const bundle = createBundle(dir);
    fs.writeFileSync(path.join(dir, 'bundle.json'), JSON.stringify(bundle, null, 2) + '\n', 'utf8');
    console.log('Wrote payload/nr-before-sr/bundle.json');
  }
  const result = inspectPayload(dir);
  for (const row of result.files) console.log(`${row.valid ? 'OK  ' : 'FAIL'} ${row.name}`);
  if (!result.ready) process.exitCode = 1;
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
