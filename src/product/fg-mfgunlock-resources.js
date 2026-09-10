'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ID = 'mfgunlock-0.7-zh-CN';
const BACKEND = 'mfgunlock';
const ADDON = 'renodx-mfgunlock.addon64';
const SHA256 = '2cd3a44e78429da4b07b2a50d694750e7b3dd34bf4ce007207f1635cf4a70d4e';
const LICENSE_SHA256 = '4ce922ecb6aa4b60d24f63f6241f0436e960a00989d71c68e2eb81ecc3d718fd';
const HASH = /^[a-f0-9]{64}$/;
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const PROVIDERS = Object.freeze([
  Object.freeze({ id: ID, version: '0.7', label: 'MFG Unlock 0.7 中文版', language: 'zh-CN', recommended: true, directory: '', sha256: SHA256,
    commit: 'ffe6169b5e98ad578fcf2c30614d06a567790fe1', readmeSha256: '59707735445f072c80f349eafcf73a51be5467699db2adde91cf806f757ecd2d', origin: 'source-build' }),
  Object.freeze({ id: 'mfgunlock-0.7', version: '0.7', label: 'MFG Unlock 0.7 官方原版', language: 'en', recommended: false, directory: 'versions/0.7',
    sha256: '6f42b545b122f4711a580dea6dda47b04a72d113bd76c371fa430ca377a7700f', commit: 'ffe6169b5e98ad578fcf2c30614d06a567790fe1',
    readmeSha256: '59707735445f072c80f349eafcf73a51be5467699db2adde91cf806f757ecd2d', origin: 'official-release' }),
  Object.freeze({ id: 'mfgunlock-0.6.1', version: '0.6.1', label: 'MFG Unlock 0.6.1 回退版', language: 'en', recommended: false, directory: 'versions/0.6.1',
    sha256: '334a750accb892a8422b88f2891d91344f9dfeea9473bcd248f5e43a80d06b8c', commit: 'a975e84712a6bfc84284f50c9621c349ee806734',
    readmeSha256: '2b8b6ded45233ce1454c7d8bd9fc39daf05d7f460dd006de389924f93005d1bc', origin: 'official-release' })
]);
const knownProviderForHash = hash => PROVIDERS.find(row => row.sha256 === hash) || null;
const providerById = id => PROVIDERS.find(row => row.id === id) || null;
function readMfgUnlockResources(root, providerId = ID) {
  const fail = message => { throw Object.assign(new Error(message), { code: 'SETTINGS_FG_RESOURCES' }); };
  const selected = providerById(providerId); if (!selected) fail('未知 MFG Unlock 版本。');
  const manifestFile = path.join(root, 'manifest.json'); let catalog;
  try {
    if (fs.statSync(manifestFile).size > 65536) fail('MFG Unlock 资源清单过大。');
    catalog = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
  } catch { fail('MFG Unlock 资源清单缺失或损坏。'); }
  if (catalog.version !== 3 || catalog.backend !== BACKEND || catalog.defaultProvider !== ID || !Array.isArray(catalog.providers) ||
      catalog.providers.length !== PROVIDERS.length || new Set(catalog.providers.map(row => row.id)).size !== PROVIDERS.length)
    fail('MFG Unlock 资源目录身份无效。');
  // The catalog cannot authorize new bytes. Optional missing fallbacks do not
  // make the selected pinned provider unusable.
  for (const row of catalog.providers) {
    const pin = providerById(row.id);
    if (!pin || row.releaseVersion !== pin.version || row.directory !== pin.directory || row.license !== 'MIT' ||
        row.source?.commit !== pin.commit || row.source?.tag !== pin.version || row.files?.addon?.file !== ADDON || row.files.addon.sha256 !== pin.sha256 ||
        row.files?.license?.file !== 'LICENSE' || row.files.license.sha256 !== LICENSE_SHA256 ||
        row.files?.readme?.file !== 'README.md' || row.files.readme.sha256 !== pin.readmeSha256 || Object.keys(row.files).length !== 3)
      fail('MFG Unlock 资源清单身份无效。');
  }
  const row = catalog.providers.find(value => value.id === providerId);
  const manifest = { ...structuredClone(row), version: 3, backend: BACKEND, sources: [row.source] };
  for (const item of Object.values(manifest.files)) {
    const file = path.join(root, selected.directory, item.file); let bytes;
    try {
      let current = file;
      while (true) { if (fs.lstatSync(current).isSymbolicLink()) throw new Error(); const parent = path.dirname(current); if (parent === current) break; current = parent; }
      const stat = fs.statSync(file); if (!stat.isFile() || stat.nlink !== 1 || stat.size > 64 * 1024 * 1024) throw new Error(); bytes = fs.readFileSync(file);
    } catch { fail('MFG Unlock 资源缺失或路径不安全：' + selected.id + '/' + item.file); }
    if (!bytes.length || sha256(bytes) !== item.sha256) fail('MFG Unlock 资源校验失败：' + selected.id + '/' + item.file);
    item.source = file;
  }
  return manifest;
}
function readMfgUnlockCatalog(root) {
  return PROVIDERS.map(row => {
    try { readMfgUnlockResources(root, row.id); return { ...row, available: true, ready: true }; }
    catch (error) { return { ...row, available: false, ready: false, blocker: error.message }; }
  });
}
module.exports = { ID, BACKEND, ADDON, SHA256, HASH, PROVIDERS, sha256, providerById, knownProviderForHash, readMfgUnlockResources, readMfgUnlockCatalog };
