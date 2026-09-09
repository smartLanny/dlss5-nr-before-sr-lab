#!/usr/bin/env python3
"""Package a compiled, disabled-by-default local research candidate. Never deploy."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--kernels',type=Path,required=True)
    p.add_argument('--io',type=Path,required=True)
    p.add_argument('--host',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Preserve previous package; choose a new output directory')
    manifest=json.loads((a.stage/'source-manifest.json').read_text(encoding='utf-8-sig'))
    research_commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    if subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=no'],text=True).strip():
        raise ValueError('Commit the research source before packaging')
    build=a.stage/'core/build'
    sources=list(build.glob('*nvfp4-dev1*.addon64'))
    chinese=[s for s in sources if '.en.' not in s.name]
    if len(chinese)!=1:raise ValueError('Exactly one compiled experimental Chinese Core is required')
    if manifest['nvfp4']['default_precision']!='Off / original FP8':raise ValueError('Unexpected default precision')
    kernel_receipt=json.loads((a.kernels/'kernel-build.json').read_text(encoding='utf-8-sig'))
    for item in kernel_receipt['artifacts']:
        if sha(a.kernels/item['file'])!=item['sha256']:raise ValueError('Kernel artifact changed after build receipt')
    a.output.mkdir(parents=True)
    core=a.output/'core-preview';core.mkdir()
    for path in [chinese[0],build/'nrchain_nvngx.dll',build/'nr_before_sr.ini']:
        shutil.copy2(path,core/path.name)
    kernels=a.output/'kernels';kernels.mkdir()
    for item in kernel_receipt['artifacts']:shutil.copy2(a.kernels/item['file'],kernels/item['file'])
    shutil.copy2(a.kernels/'kernel-build.json',kernels/'kernel-build.json')
    for name in ('nr_original_io.cubin','io-build.json'):shutil.copy2(a.io/name,kernels/name)
    host=a.output/'controlled-host';host.mkdir();shutil.copy2(a.host/'nr-host.exe',host/'nr-host.exe')
    shutil.copy2(a.stage/'source-manifest.json',a.output/'source-manifest.json')
    shutil.copy2(ROOT/'docs/NVFP4_DEV14.md',a.output/'TECHNICAL-NOTES.md')
    shutil.copy2(ROOT/'THIRD_PARTY_NOTICES.txt',a.output/'THIRD_PARTY_NOTICES.txt')
    shutil.copy2(ROOT/'scripts/install-nvfp4-preview.ps1',a.output/'install-nvfp4-preview.ps1')
    licenses=a.output/'licenses';licenses.mkdir();shutil.copy2(ROOT/'third_party/nvfp4_reference/LICENSE.txt',licenses/'OptiScaler-GPL-3.0.txt')
    # Provide the exact generated compilation inputs, without model payloads,
    # bug inboxes, captures, credentials, .git, or unrelated project history.
    with zipfile.ZipFile(a.output/'generated-core-source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for path in sorted((a.stage/'core').rglob('*')):
            if path.is_file() and path.relative_to(a.stage/'core').parts[0] not in ('build','.git'):
                z.write(path,path.relative_to(a.stage/'core').as_posix())
    with zipfile.ZipFile(a.output/'nvfp4-research-source.zip','w',zipfile.ZIP_DEFLATED) as z:
        paths=subprocess.check_output(['git','-C',str(ROOT),'ls-files'],text=True).splitlines()
        for name in paths:
            if (name.startswith(('kernels/','tools/nvfp4_kernel/','tests/nvfp4_host/','tests/nvfp4_io/','tests/nvfp4_kernel/','tests/nvfp4_backend/','src/nvfp4/','third_party/nvfp4_reference/'))
                    or name in ('docs/NVFP4_DEV14.md','THIRD_PARTY_NOTICES.txt')
                    or ('nvfp4' in name and name.startswith('scripts/'))):
                z.write(ROOT/name,name)
    instructions='''# dev14 NVFP4 源码预览包

本包是默认关闭精度替换的本地源码预览。NVFP4 自动档尚无合格性能／画质策略，保持原 FP8。
研究阶段已有局部 GPU 和真实 Core 静态 NR→SR 证据，准确范围见 TECHNICAL-NOTES.md。
本包的实际资格以构建身份和 package-manifest.json 为准；它不是已验收提速版本，未部署到游戏。

- core-preview：默认关闭精度替换的中文实验 Core、调用桥和示例配置。
- kernels：可重建的收缩、融合收缩及 IO 内核和参数构造 DLL。
- controlled-host：独立 Feature18 验证宿主。
- 两份 source.zip：实际生成的 Core 编译输入及 NVFP4 研究源码。
- source-manifest.json / package-manifest.json：源码身份、二进制哈希和准确验收边界。

原 NVIDIA NR DLL 和上游模型资产不随本包分发。已有个人配置请保留。
继续测试前请核对 TECHNICAL-NOTES.md 中的验收记录、未完成项及各自实际构建身份。
已有融合 GPU 和静态链路结果不能自动给本包二进制授予资格；完整 NR 长程配对、运动和游戏
验收仍需各自证据。package-manifest.json 的 false 字段表示本包未获得相应资格，
不否认技术记录中单独列明的局部 GPU 验证。不要把独立 GEMM 时间当作游戏收益。
启用 Auto 而显示回退原 FP8 是当前资格门槛的预期行为。
'''
    (a.output/'开始阅读.md').write_text(instructions,encoding='utf-8')
    receipt={'schema':1,'source_commit':manifest['nvfp4']['source_commit'],'research_source_commit':research_commit,'baseline_commit':manifest['nvfp4']['baseline_commit'],
             'kernel_abi_fingerprint':kernel_receipt['abi_fingerprint'],
             'kind':'local source preview','default_precision':'original FP8','auto_qualified':False,
             'fused_gpu_verified':False,'game_verified':False,'performance_verified':False,'deployed':False,
             'files':{p.relative_to(a.output).as_posix():sha(p) for p in sorted(a.output.rglob('*')) if p.is_file()}}
    (a.output/'package-manifest.json').write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    zip_path=a.output.with_suffix('.zip')
    if zip_path.exists():raise ValueError('Preserve the existing archive')
    with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
        for path in sorted(a.output.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(a.output).as_posix())
    print(json.dumps({'archive':str(zip_path),'sha256':sha(zip_path)},ensure_ascii=False))

if __name__=='__main__':main()
