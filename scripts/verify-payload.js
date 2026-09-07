'use strict';

const fs = require('fs');
const path = require('path');
const { createBundle, createVariantsBundle, createVersionedBundle, createCompactBundle, inspectPayload } = require('../src/product/payload');
const { FAMILIES } = require('../src/product/gpu');
const { DX11_COMPAT_VERSION, DX11_COMPAT_LABEL, DX11_COMPAT_CARRIER, DX11_COMPAT_NOTES } = require('../src/product/constants');
const dir = path.join(__dirname, '..', 'payload', 'nr-before-sr');
const write = process.argv.includes('--write');

try {
  if (write) {
    const versionIds = ['0.2.0-beta.2', '0.3.3.5', DX11_COMPAT_VERSION];
    const compatDir = path.join(dir, 'versions', DX11_COMPAT_VERSION);
    const compatFiles = ['nr-before-sr.zh-CN.addon64', 'nr_before_sr.ini', 'nrchain_nvngx.dll', DX11_COMPAT_CARRIER];
    if (fs.existsSync(compatDir) && !compatFiles.every(name => fs.existsSync(path.join(compatDir, name)))) {
      throw new Error('0.4.5 DX11 compatibility payload requires matched addon, nrchain, carrier and INI');
    }
    const hasCompactVersions = versionIds.every(id => fs.existsSync(path.join(dir, 'versions', id, 'nr-before-sr.zh-CN.addon64')) && fs.existsSync(path.join(dir, 'versions', id, 'nr_before_sr.ini')))
      && FAMILIES.every(family => ['ReShade64.dll', 'nrchain_nvngx.dll', 'nvngx_dlssnr.dll'].every(name => fs.existsSync(path.join(dir, 'fixed', family, name))));
    const hasVersions = versionIds.every(id => FAMILIES.every(family => fs.existsSync(path.join(dir, 'versions', id, family))));
    let bundle = hasCompactVersions
      ? createCompactBundle(dir, [
          { id: '0.2.0-beta.2', label: '0.2.0-beta.2（2K27 兼容候选）', notes: '历史 beta0.2 addon；针对 2K27 等反馈优先用于兼容性对比。当前固定运行组件仍按显卡族共用。', source: 'local historical tag v0.2.0-beta.2' },
          { id: '0.3.3.5', label: '0.3.3.5（默认稳定版）', notes: '默认稳定槽；本地现有 beta0.3.3.5 addon，用于日常安装和兼容性基线。', source: 'beta0.3.3.5-080d' },
          { id: DX11_COMPAT_VERSION, label: DX11_COMPAT_LABEL, notes: DX11_COMPAT_NOTES, source: 'beta0.4.5-dx11-compat@dccb5b398ccc540723f84a6ef789dd70dbda5cd3', compatibility: 'dx11', ota: true }
        ], '0.3.3.5')
      : hasVersions
      ? createVersionedBundle(dir, [
          { id: '0.2.0-beta.2', label: '0.2.0-beta.2（2K27 兼容候选）', notes: '历史 beta0.2 addon；针对 2K27 等反馈优先用于兼容性对比。当前固定运行组件仍按显卡族共用。', source: 'local historical tag v0.2.0-beta.2' },
          { id: '0.3.3.5', label: '0.3.3.5（默认稳定版）', notes: '默认稳定槽；本地现有 beta0.3.3.5 addon，用于日常安装和兼容性基线。', source: 'beta0.3.3.5-080d' },
          { id: DX11_COMPAT_VERSION, label: DX11_COMPAT_LABEL, notes: DX11_COMPAT_NOTES, source: 'beta0.4.5-dx11-compat@dccb5b398ccc540723f84a6ef789dd70dbda5cd3', compatibility: 'dx11', ota: true }
        ], '0.3.3.5')
      : FAMILIES.every(family => fs.existsSync(path.join(dir, family)))
        ? createVariantsBundle(dir)
        : createBundle(dir);
    const latestDir = path.join(dir, 'versions', '0.4.6');
    if (fs.existsSync(latestDir)) {
      if (bundle.version !== 4) throw new Error('0.4.6 requires the compact paired payload');
      const { ENTRY, verifyVersion } = require('./prepare-release-046');
      verifyVersion(latestDir);
      const entries = Object.entries(bundle.versions).map(([id, row]) => ({ ...row, id }));
      for (const entry of entries) if (entry.id === '0.3.3.5') {
        entry.label = '0.3.3.5（历史稳定版）'; entry.notes = '保留的历史核心，用于兼容性对比与回退。';
      }
      bundle = createCompactBundle(dir, [...entries, ENTRY], ENTRY.id);
    }
    fs.writeFileSync(path.join(dir, 'bundle.json'), JSON.stringify(bundle, null, 2) + '\n', 'utf8');
    console.log('Wrote payload/nr-before-sr/bundle.json');
  }
  const result = inspectPayload(dir);
  const rows = result.versions
    ? Object.entries(result.versions).flatMap(([version, item]) => FAMILIES.flatMap(family => item.variants[family].files.map(row => ({ ...row, name: `${version}/${family}/${row.name}` }))))
    : result.variants
      ? FAMILIES.flatMap(family => result.variants[family].files.map(row => ({ ...row, name: `${family}/${row.name}` })))
      : result.files;
  for (const row of rows) console.log(`${row.valid ? 'OK  ' : 'FAIL'} ${row.name}`);
  if (!result.ready || rows.some(row => !row.valid || !row.expected)) process.exitCode = 1;
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
