'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { readBundle, sha256 } = require('./payload');
const { DX11_COMPAT_CARRIER } = require('./constants');
const CORE = 'b7bef0c7ad637ef35d2f0d327201636cb80f808a6af62cb6221f980343376dbf';
const CHAIN = '46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb';
const BRIDGES = Object.freeze([
  Object.freeze({ id: 'nigos-1.4.12-nr', label: 'NIGos Bridge 1.4.12 · NR 适配', upstreamVersion: '1.4.12',
    registeredName: 'DLSS 5 Bridge 1.4.12', sourceVersion: '0.4.7beta', channel: 'compatible', default: true,
    sha256: '4656d9aac382a6f9b5c8488669aa5365283f03b5b94b2267f1c7f9b53927dc86',
    source: 'https://github.com/smartLanny/dlss5-nr-before-sr-lab', patchId: 'nr-carrier-045-dx11-compat',
    note: '本项目已适配组件；版本名相同的上游 DLL 不能直接替换。' }),
  Object.freeze({ id: 'nigos-1.4.11-nr', label: 'NIGos Bridge 1.4.11 · 回退', upstreamVersion: '1.4.11',
    registeredName: 'DLSS 5 Bridge 1.4.11', sourceVersion: '0.4.7beta-bg3-bridge1411', channel: 'fallback', default: false,
    sha256: '73d438ee9427e73d9919d169a107c7f5d73f60b291ea2cd66bd33279a35d3e95',
    source: 'https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/224', patchId: 'issue-224-fixed-bridge1411',
    note: '保留已知回退；Core 可能提示版本差异，实际 NR 仍按游戏验收。' })
]);
const fail = (code, message) => { throw Object.assign(new Error(message), { code }); };
function bridgeByHash(digest) { return BRIDGES.find(row => row.sha256 === digest) || null; }
function bridgeCatalog(payloadDir, { coreHash = null, chainHash = null, installedHash = null } = {}) {
  return BRIDGES.map(row => ({ ...row, filename: DX11_COMPAT_CARRIER, architecture: 'x64', api: 'dx11',
    hardwareFamilies: ['RTX40', 'RTX50'], coreSha256: CORE, chainSha256: CHAIN,
    installed: row.sha256 === installedHash,
    compatible: coreHash === CORE && chainHash === CHAIN,
    ready: fs.existsSync(path.join(payloadDir, 'versions', row.sourceVersion, DX11_COMPAT_CARRIER)), runtimeVerified: false }));
}
function selectNativeComponents(payloadDir, payload, { api, bridgeId, installedHash } = {}) {
  if (api !== 'dx11') {
    if (bridgeId) fail('COMPONENT_BRIDGE_API', 'NIGos Bridge 仅适用于原生 DX11 路线；加载入口不会改变游戏 API。');
    return { ...payload, components: { bridge: null } };
  }
  const pinned = bridgeByHash(installedHash);
  const target = bridgeId ? BRIDGES.find(row => row.id === bridgeId) : pinned || bridgeByHash(payload.carrier?.actual);
  if (!target) {
    if (bridgeId) fail('COMPONENT_BRIDGE_UNKNOWN', '找不到所选的适配版桥接器。');
    return payload; // Historical packages retain their own verified companion.
  }
  if (payload.addon?.actual !== CORE || payload.bridge?.actual !== CHAIN) {
    if (bridgeId) fail('COMPONENT_BRIDGE_CORE', '所选 Core 与此桥接器的固定配套接口尚未验证。');
    return payload;
  }
  const file = path.join(payloadDir, 'versions', target.sourceVersion, DX11_COMPAT_CARRIER);
  const stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.nlink !== 1 || sha256(file) !== target.sha256)
    fail('COMPONENT_BRIDGE_HASH', '桥接器来源摘要与固定适配版本不一致。');
  return { ...payload, carrier: { ...payload.carrier, name: DX11_COMPAT_CARRIER, file, actual: target.sha256, expected: target.sha256 },
    components: { bridge: target.id }, componentMetadata: { bridge: { ...target } } };
}
function knownPayloadComponents(payloadDir) {
  const bundle = readBundle(payloadDir), result = [];
  for (const [version, row] of Object.entries(bundle.versions || {})) {
    for (const [name, hash] of Object.entries(row.files || {})) {
      if (!/\.addon(?:32|64)?$/i.test(name)) continue;
      const bridge = bridgeByHash(hash), role = name === 'nr-before-sr.zh-CN.addon64' ? 'core' : bridge ? 'carrier' : null;
      if (!role) continue;
      result.push({ sha256: hash, role, version, ...(bridge ? { registeredName: bridge.registeredName, compatibility: 'compatible' } : {}) });
    }
    for (const hash of row.trustedUpgradeFrom || []) result.push({ sha256: hash, role: 'core', version: 'historical' });
  }
  return result;
}
module.exports = { BRIDGES, bridgeByHash, bridgeCatalog, selectNativeComponents, knownPayloadComponents };
