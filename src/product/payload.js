'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { PAYLOAD_FILES } = require('./constants');
const { appError } = require('./errors');

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function payloadRoot(baseDir) {
  return path.join(baseDir, 'payload', 'nr-before-sr');
}

function readBundle(dir) {
  const file = path.join(dir, 'bundle.json');
  if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: 'bundle.json' });
  let bundle;
  try { bundle = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' }); }
  if (!bundle || bundle.version !== 1 || !bundle.files || typeof bundle.files !== 'object') {
    throw appError('ERR_PAYLOAD_HASH', { file: 'bundle.json' });
  }
  return bundle;
}

function inspectPayload(dir, options = {}) {
  const rows = [];
  let bundle = null;
  try { bundle = readBundle(dir); }
  catch (error) {
    if (!options.allowMissingBundle) throw error;
  }

  for (const [kind, name] of Object.entries(PAYLOAD_FILES)) {
    const file = path.join(dir, name);
    const exists = fs.existsSync(file) && fs.statSync(file).isFile();
    const actual = exists ? sha256(file) : null;
    const expected = bundle && bundle.files[name] ? String(bundle.files[name]).toLowerCase() : null;
    rows.push({ kind, name, file, exists, actual, expected, valid: exists && (!expected || actual === expected) });
  }

  return {
    dir,
    bundle,
    files: rows,
    ready: Boolean(bundle) && rows.every(row => row.valid && row.expected),
    missing: rows.filter(row => !row.exists).map(row => row.name),
    invalid: rows.filter(row => row.exists && (!row.expected || row.actual !== row.expected)).map(row => row.name)
  };
}

function requirePayload(dir) {
  const result = inspectPayload(dir);
  if (result.missing.length) throw appError('ERR_PAYLOAD_MISSING', { files: result.missing });
  if (!result.ready) throw appError('ERR_PAYLOAD_HASH', { files: result.invalid });
  return Object.fromEntries(result.files.map(row => [row.kind, row]));
}

function createBundle(dir) {
  const files = {};
  for (const name of Object.values(PAYLOAD_FILES)) {
    const file = path.join(dir, name);
    if (!fs.existsSync(file)) throw appError('ERR_PAYLOAD_MISSING', { file: name });
    files[name] = sha256(file);
  }
  return { version: 1, generatedAt: new Date().toISOString(), files };
}

module.exports = { sha256, payloadRoot, readBundle, inspectPayload, requirePayload, createBundle };
