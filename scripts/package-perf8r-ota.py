#!/usr/bin/env python3
"""Package one frozen, locally validated Perf8R Chinese Core+owned companion.
No game writes. No NVIDIA runtime/model/config payload. No release upload.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
from nr_ota_pe import companion_check

VERSION = 'beta0.5-dline21-perf8r'
CORE_NAME = 'DLSS5-AI渲染超分版-beta0.5-dline21-perf8r-@野生的装机宅-Bilibili.addon64'
INSTRUCTIONS = '''D21 Perf8R 中文性能测试候选
适用于已有 D21/Lock2/Perf3/Perf4/Perf5 安装，不是初次安装全套包，不代表游戏性能验收通过。
退出游戏，把旧 Core 和 nrchain 备份到游戏目录外，再使用本包一个中文 Core 和配套 nrchain。
不要同时加载 D13、D21、Lock2、Perf3、Perf4、Perf5 或 RenoDX 的另一份 NR Core。
保留 INI、ReShade、桥接器、NVIDIA 运行库、模型及所有画质参数。不能搭配 D13 的旧小体积 nrchain。
本包为 nr-branch-ota-v1 格式，已检查文件/哈希/PE 身份；本轮未操作管理器界面，导入后仍需选择版本并应用。
保持当前路由、分辨率和强度参数，测试默认关闭 -> 开启 -> 再次关闭，并保存同期 F8。前置NR到SR与后置SR到NR须分开比较；开启时确认NR成功计数增长。
F8 保留 descriptor.rangeQueries，以及 descriptor.slotQueries：缓存命中、未命中与链表检查次数；不是性能百分比或资源有效性证明。
首次开启和关闭的长帧单独记录。这一版没有单独修复初始化卡顿，不可用安全旁路的高帧率作为通过依据。
出现旁路、无效果、闪烁或崩溃时回滚备份，上传 F8 TXT、Core 日志及版本/哈希反馈 BUG。
Windows 构建及 WARP 读回验证已完成；调试层是否可用单独见回执。NVIDIA 推理、游戏和管理器界面未由此脚本测试。
'''


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load(path: Path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    base, out = args.candidate.resolve(), args.output.resolve()
    if out.exists(): raise ValueError('Preserve prior packages; choose a fresh output')
    receipt = load(base / 'PERF8R-CANDIDATE.json')
    canonical = load(base / 'validation.json')
    source = receipt['source_commit']
    if receipt['candidate'] != VERSION or canonical['source_commit'] != source:
        raise ValueError('Candidate/source mismatch')
    for key in ('source_verified', 'compile_verified', 'warp_verified'):
        if canonical.get(key) is not True: raise ValueError('Missing canonical gate: ' + key)
    for key in ('snapshot_gpu_readback_verified', 'snapshot_negative_control_verified', 'inherited_windows_regressions'):
        if receipt.get(key) is not True: raise ValueError('Missing inherited gate: ' + key)
    for key in ('concurrency_gate', 'hotpath_gate', 'perf3_gate', 'perf4_gate', 'perf5_gate', 'perf6_gate', 'perf7_gate', 'perf8r_gate', 'reset_gate', 'ranges_gate'):
        if load(Path(receipt[key])).get('status') != 'passed': raise ValueError('Missing gate: ' + key)
    extras = Path(receipt['perf3_gate']).parent
    descriptor = load(extras / 'descriptor-warp/result.json')
    transport = load(extras / 'source-transport/validation.json')
    if descriptor.get('warp_exit') != 0 or not descriptor.get('gpu_executed') or not transport.get('warp_verified'):
        raise ValueError('Missing descriptor/source WARP')
    entry = next(x for x in receipt['binaries'] if x['language'] == 'zh-CN')
    if Path(entry['file']).name != entry['file']: raise ValueError('Invalid Core leaf name')
    core = (base / 'TEST-zh-CN' / entry['file']).read_bytes()
    chain = (base / 'source/core/build/nrchain_nvngx.dll').read_bytes()
    chain_entry = next(x for x in canonical['binaries'] if x['file'] == 'nrchain_nvngx.dll')
    if digest(core) != entry['sha256'] or digest(chain) != chain_entry['sha256']:
        raise ValueError('Runtime changed after validation')
    if len(core) < 256 or core[:2] != b'MZ': raise ValueError('Core is not PE')
    pe = struct.unpack_from('<I', core, 0x3c)[0]
    if pe > len(core)-24 or core[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', core, pe+4)[0] != 0x8664:
        raise ValueError('Core is not x64 PE')
    if VERSION.encode() not in core or source.encode() not in core: raise ValueError('Embedded identity mismatch')
    companion_check(chain)
    license_text = (base / 'source/core/third_party/minhook/LICENSE.txt').read_bytes()
    license_text += b'\n--- owned companion build notice ---\n' + (base / 'source/core/build/NVIDIA-NGX-LICENSE.txt').read_bytes()
    payload = {CORE_NAME: core, 'nrchain_nvngx.dll': chain, 'LICENSES.txt': license_text,
               'Instructions.txt': b'\xef\xbb\xbf' + INSTRUCTIONS.replace('\n', '\r\n').encode('utf-8')}
    manifest = {'schema': 'nr-branch-ota-v1', 'version': VERSION, 'display_version': '0.5 D21 Perf8R',
                'language': 'Chinese', 'api': 'D3D12-x64', 'sourceCommit': source,
                # OTA v1 requires this exact boolean. It names the frozen,
                # clean integration checkout, not the generated Core tree's
                # separate F8 sourceDirty bit. Both use the verified DLL hash.
                'sourceDirty': False, 'generated_source': True, 'source_checkout_clean': True,
                'sourceDirty_scope': 'frozen integration checkout at build start', 'includesDx11': False,
                'channel': 'd21-perf8r-test-upgrade', 'manager_install_scope': 'Core and nrchain',
                'face_assets_included': False, 'universal_game_compatibility_verified': False,
                'whole_frame_performance_verified': False,
                'snapshot_debug_layer_verified': receipt['snapshot_debug_layer_verified'],
                'preserve': ['nr_before_sr.ini', 'ReShade.ini', 'ReShadePreset.ini', 'nvngx_dlssnr.dll',
                             'nvngx_dlss.dll', 'nvngx_dlssg.dll', 'nvngx_dlssd.dll'],
                'files': [{'name': n, 'bytes': len(b), 'sha256': digest(b)} for n, b in sorted(payload.items())]}
    payload['ota-manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    out.mkdir(parents=True)
    target = out / 'DLSS5-0.5-D21-Perf8R-zh-CN-OTA.zip'
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, blob in sorted(payload.items()): z.writestr(name, blob)
    with zipfile.ZipFile(target) as z:
        if z.testzip() or set(z.namelist()) != set(payload): raise ValueError('ZIP integrity failed')
        for f in manifest['files']:
            b = z.read(f['name'])
            if len(b) != f['bytes'] or digest(b) != f['sha256']: raise ValueError('Archived file mismatch')
    final = {'file': str(target), 'bytes': target.stat().st_size, 'sha256': digest(target.read_bytes()),
             'source_commit': source, 'core_sha256': digest(core), 'nrchain_sha256': digest(chain),
             'snapshot_debug_layer_verified': receipt['snapshot_debug_layer_verified'],
             'game_verified': False, 'manager_ui_tested': False, 'deployed': False}
    (out / 'package-receipt.json').write_text(json.dumps(final, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (out / 'SHA256SUMS.txt').write_text(final['sha256']+'  '+target.name+'\n', encoding='utf-8')
    print(json.dumps(final, ensure_ascii=False, indent=2))


if __name__ == '__main__': main()
