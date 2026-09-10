'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { sha256, SHA256 } = require('../src/product/fg-mfgunlock-resources');
function verifySource(root = path.resolve(__dirname, '../native/mfgunlock')) {
  const lock = JSON.parse(fs.readFileSync(path.join(root, 'source-lock.json'), 'utf8'));
  if (lock.upstream.commit !== 'ffe6169b5e98ad578fcf2c30614d06a567790fe1' || lock.provider !== 'mfgunlock-0.7-zh-CN') throw new Error('Unreviewed MFG source identity.');
  for (const [file, digest] of Object.entries(lock.upstream.files)) {
    if (path.basename(file) !== file || sha256(fs.readFileSync(path.join(root, 'upstream', file))) !== digest)
      throw new Error('MFG upstream source changed: ' + file);
  }
  const normalize = text => text.replaceAll('\r\n', '\n');
  let expected = normalize(fs.readFileSync(path.join(root, 'upstream/addon.cpp'), 'utf8'));
  for (const file of ['framecount.hpp', 'loadhook.hpp', 'midpoint.hpp', 'ngx_hook.hpp']) expected = expected.replace('#include "./' + file + '"', '#include "./upstream/' + file + '"');
  const start = expected.indexOf('void OnRegisterOverlay('), end = expected.indexOf('void LoadConfig()', start);
  if (start < 0 || end < 0) throw new Error('The upstream overlay boundary changed.');
  expected = expected.slice(0, start) + '#include "./panel_zh.inl"\n\n' + expected.slice(end);
  if (normalize(fs.readFileSync(path.join(root, 'addon.cpp'), 'utf8')) !== expected)
    throw new Error('MFG runtime code changed outside the isolated overlay adaptation.');
  const panel = fs.readFileSync(path.join(root, 'panel_zh.inl'), 'utf8');
  const keys = [...panel.matchAll(/set_config_value\(nullptr, kConfigSection, "([^"]+)"/g)].map(match => match[1]).sort();
  const wanted = ['DepthEdgeGuardLevel', 'Enabled', 'ForceFlipMeteringOff', 'ForceMultiplier', 'HDRCompatibilityMode', 'MaxCount', 'TemporalFix'].sort();
  if (JSON.stringify(keys) !== JSON.stringify(wanted) || !panel.includes('const unsigned int choices[] = {0, 2, 3, 4};') ||
      /(?:AddFont|BuildAtlas|GetIO\(\)\.Fonts|GlyphRanges)/.test(panel)) throw new Error('MFG panel crossed its preserved configuration/font contract.');
  return { upstreamCommit: lock.upstream.commit, runtimeOutsideOverlayUnchanged: true, configurationKeysPreserved: true, fontAtlasOwnedByHost: true,
    binarySha256: SHA256, panelSha256: sha256(Buffer.from(panel)), sourceSha256: sha256(fs.readFileSync(path.join(root, 'addon.cpp'))) };
}
if (require.main === module) console.log(JSON.stringify(verifySource(), null, 2));
module.exports = { verifySource };
