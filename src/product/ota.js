'use strict';

const fs = require('fs');
const crypto = require('crypto');
const yauzl = require('yauzl');

const MAX_ENTRY_BYTES = 64 * 1024 * 1024;
const MAX_TOTAL_BYTES = 128 * 1024 * 1024;

function sha256Buffer(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function readZipEntries(file) {
  return new Promise((resolve, reject) => {
    yauzl.open(file, { lazyEntries: true, autoClose: true }, (error, zip) => {
      if (error) return reject(error);
      const entries = new Map();
      let total = 0;
      let settled = false;
      const fail = reason => {
        if (settled) return;
        settled = true;
        try { zip.close(); } catch {}
        reject(reason);
      };
      zip.on('error', fail);
      zip.on('end', () => {
        if (!settled) { settled = true; resolve(entries); }
      });
      zip.on('entry', entry => {
        if (settled) return;
        const name = String(entry.fileName || '').replace(/\\/g, '/');
        if (!name || name.endsWith('/')) return zip.readEntry();
        if (name.startsWith('/') || name.split('/').includes('..')) return fail(new Error('unsafe zip path'));
        if (entry.uncompressedSize > MAX_ENTRY_BYTES || total + entry.uncompressedSize > MAX_TOTAL_BYTES) {
          return fail(new Error('zip payload too large'));
        }
        zip.openReadStream(entry, (streamError, stream) => {
          if (streamError) return fail(streamError);
          const chunks = [];
          let size = 0;
          stream.on('data', chunk => {
            size += chunk.length;
            if (size > MAX_ENTRY_BYTES || total + size > MAX_TOTAL_BYTES) {
              stream.destroy(new Error('zip payload too large'));
              return;
            }
            chunks.push(chunk);
          });
          stream.on('error', fail);
          stream.on('end', () => {
            if (settled) return;
            const data = Buffer.concat(chunks);
            total += data.length;
            entries.set(name, data);
            zip.readEntry();
          });
        });
      });
      zip.readEntry();
    });
  });
}

async function readOtaPackage(file) {
  if (typeof file !== 'string' || !/\.zip$/i.test(file) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    throw new Error('invalid OTA package');
  }
  const entries = await readZipEntries(file);
  const manifestBytes = entries.get('ota-manifest.json');
  if (!manifestBytes) throw new Error('OTA manifest missing');
  let manifest;
  try { manifest = JSON.parse(manifestBytes.toString('utf8')); } catch { throw new Error('invalid OTA manifest'); }
  if (!manifest || manifest.schema !== 'nr-branch-ota-v1' || !Array.isArray(manifest.files)) {
    throw new Error('unsupported OTA manifest');
  }
  for (const row of manifest.files) {
    if (!row || typeof row.name !== 'string' || !entries.has(row.name)) throw new Error(`OTA file missing: ${row && row.name}`);
    const data = entries.get(row.name);
    if (Number(row.bytes) !== data.length || String(row.sha256).toLowerCase() !== sha256Buffer(data)) {
      throw new Error(`OTA file hash mismatch: ${row.name}`);
    }
  }
  const addonRows = manifest.files.filter(row => /\.addon64$/i.test(row.name));
  const bridgeRow = manifest.files.find(row => /^nrchain_nvngx\.dll$/i.test(row.name));
  if (addonRows.length !== 1 || !bridgeRow) throw new Error('OTA package must contain one addon64 and nrchain_nvngx.dll');
  if (manifest.includesDx11 === true || !/^D3D12-x64$/i.test(String(manifest.api || ''))) {
    throw new Error('this OTA package is not a standard DX12 OTA');
  }
  return {
    manifest,
    addonName: addonRows[0].name,
    addon: entries.get(addonRows[0].name),
    bridgeName: bridgeRow.name,
    bridge: entries.get(bridgeRow.name),
    bridgeSha256: String(bridgeRow.sha256).toLowerCase(),
    instructions: entries.has('Instructions.txt') ? entries.get('Instructions.txt').toString('utf8') : ''
  };
}

module.exports = { readOtaPackage, sha256Buffer };
