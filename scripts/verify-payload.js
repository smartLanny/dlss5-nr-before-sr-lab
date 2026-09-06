'use strict';

const fs = require('fs');
const path = require('path');
const { createBundle, createVariantsBundle, createVersionedBundle, createCompactBundle, inspectPayload } = require('../src/product/payload');
const { FAMILIES } = require('../src/product/gpu');
const dir = path.join(__dirname, '..', 'payload', 'nr-before-sr');
const write = process.argv.includes('--write');

try {
  if (write) {
    const versionIds = ['0.2.0-beta.2', '0.3.3.5', '0.4.5-ota', '0.4.2-dx11-native-bridge-exp1-r1'];
    const hasCompactVersions = versionIds.every(id => fs.existsSync(path.join(dir, 'versions', id, 'nr-before-sr.zh-CN.addon64')) && fs.existsSync(path.join(dir, 'versions', id, 'nr_before_sr.ini')))
      && FAMILIES.every(family => ['ReShade64.dll', 'nrchain_nvngx.dll', 'nvngx_dlssnr.dll'].every(name => fs.existsSync(path.join(dir, 'fixed', family, name))));
    const hasVersions = versionIds.every(id => FAMILIES.every(family => fs.existsSync(path.join(dir, 'versions', id, family))));
    const bundle = hasCompactVersions
      ? createCompactBundle(dir, [
          { id: '0.2.0-beta.2', label: '0.2.0-beta.2（2K27 兼容候选）', notes: '历史 beta0.2 addon；针对 2K27 等反馈优先用于兼容性对比。当前固定运行组件仍按显卡族共用。', source: 'local historical tag v0.2.0-beta.2' },
          { id: '0.3.3.5', label: '0.3.3.5（默认稳定版）', notes: '默认稳定槽；本地现有 beta0.3.3.5 addon，用于日常安装和兼容性基线。', source: 'beta0.3.3.5-080d' },
          { id: '0.4.5-ota', label: 'Beta0.4.5.2（标准 OTA 更新包）', notes: '当前唯一标准 Beta0.4 槽；从已有 Beta0.4 安装升级时只替换 Addon，并按 OTA 一起更新配套 chain，保留 INI、ReShade、Runtime 和游戏自带 DLSS。仅支持 D3D12-x64，不含 DX11。', source: 'DLSS5-beta0.4.5-fix2-Chinese-OTA.zip', compatibility: 'dx12', ota: true },
          { id: '0.4.2-dx11-native-bridge-exp1-r1', label: 'Beta0.4.2 · DX11 前置兼容测试', notes: '仅供 DX11 原生/模组 DLSS 游戏尝试；实验包可能无法工作或影响启动，遇到问题请进群联系 @群RBQ 并提交报告。DX12/Vulkan 不使用此槽。', source: 'beta0.4.2-DX11兼容-exp1-windows-x64.zip', compatibility: 'dx11' }
        ], '0.3.3.5')
      : hasVersions
      ? createVersionedBundle(dir, [
          { id: '0.2.0-beta.2', label: '0.2.0-beta.2（2K27 兼容候选）', notes: '历史 beta0.2 addon；针对 2K27 等反馈优先用于兼容性对比。当前固定运行组件仍按显卡族共用。', source: 'local historical tag v0.2.0-beta.2' },
          { id: '0.3.3.5', label: '0.3.3.5（默认稳定版）', notes: '默认稳定槽；本地现有 beta0.3.3.5 addon，用于日常安装和兼容性基线。', source: 'beta0.3.3.5-080d' },
          { id: '0.4.5-ota', label: 'Beta0.4.5.2（标准 OTA 更新包）', notes: '当前唯一标准 Beta0.4 槽；从已有 Beta0.4 安装升级时只替换 Addon，并按 OTA 一起更新配套 chain，保留 INI、ReShade、Runtime 和游戏自带 DLSS。仅支持 D3D12-x64，不含 DX11。', source: 'DLSS5-beta0.4.5-fix2-Chinese-OTA.zip', compatibility: 'dx12', ota: true },
          { id: '0.4.2-dx11-native-bridge-exp1-r1', label: 'Beta0.4.2 · DX11 前置兼容测试', notes: '仅供 DX11 原生/模组 DLSS 游戏尝试；实验包可能无法工作或影响启动，遇到问题请进群联系 @群RBQ 并提交报告。DX12/Vulkan 不使用此槽。', source: 'beta0.4.2-DX11兼容-exp1-windows-x64.zip', compatibility: 'dx11' }
        ], '0.3.3.5')
      : FAMILIES.every(family => fs.existsSync(path.join(dir, family)))
        ? createVariantsBundle(dir)
        : createBundle(dir);
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
  if (!result.ready) process.exitCode = 1;
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
