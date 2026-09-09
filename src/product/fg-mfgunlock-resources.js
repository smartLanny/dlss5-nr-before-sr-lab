'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ID = 'mfgunlock-0.6.1';
const BACKEND = 'mfgunlock';
const ADDON = 'renodx-mfgunlock.addon64';
const SHA256 = '334a750accb892a8422b88f2891d91344f9dfeea9473bcd248f5e43a80d06b8c';
const LICENSE_SHA256 = '4ce922ecb6aa4b60d24f63f6241f0436e960a00989d71c68e2eb81ecc3d718fd';
const README_SHA256 = '2b8b6ded45233ce1454c7d8bd9fc39daf05d7f460dd006de389924f93005d1bc';
const HASH = /^[a-f0-9]{64}$/;
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function readMfgUnlockResources(root) {
  const fail = message => { throw Object.assign(new Error(message), { code: 'SETTINGS_FG_RESOURCES' }); };
  const manifestFile = path.join(root, 'manifest.json');
  let manifest;
  try {
    if (fs.statSync(manifestFile).size > 65536) fail('MFG Unlock 资源清单过大。');
    manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
  } catch { fail('MFG Unlock 资源清单缺失或损坏。'); }
  if (manifest.version !== 2 || manifest.backend !== BACKEND || manifest.id !== ID || manifest.releaseVersion !== '0.6.1' ||
      manifest.files?.addon?.file !== ADDON || manifest.files.addon.sha256 !== SHA256 ||
      manifest.files?.license?.file !== 'LICENSE' || manifest.files?.readme?.file !== 'README.md' ||
      manifest.files.license.sha256 !== LICENSE_SHA256 || manifest.files.readme.sha256 !== README_SHA256 ||
      manifest.license !== 'MIT' || !Array.isArray(manifest.sources) || manifest.sources.length !== 1 ||
      manifest.sources[0].commit !== 'a975e84712a6bfc84284f50c9621c349ee806734' || manifest.sources[0].tag !== '0.6.1') fail('MFG Unlock 资源清单身份无效。');
  for (const [role, row] of Object.entries(manifest.files)) {
    if (!['addon', 'license', 'readme'].includes(role) || !row || path.basename(row.file || '') !== row.file || !HASH.test(row.sha256 || '')) fail('MFG Unlock 资源路径或摘要无效。');
    const file = path.join(root, row.file);
    let bytes;
    try { if (!fs.statSync(file).isFile()) throw new Error(); bytes = fs.readFileSync(file); } catch { fail(`MFG Unlock 资源缺失：${row.file}`); }
    if (!bytes.length || sha256(bytes) !== row.sha256) fail(`MFG Unlock 资源校验失败：${row.file}`);
    row.source = file;
  }
  return manifest;
}
module.exports = { ID, BACKEND, ADDON, SHA256, HASH, sha256, readMfgUnlockResources };
