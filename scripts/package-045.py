"""Package/verify only the two owned-addon OTA updates. Never copy game runtimes.
This does not install files or modify a user's game directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile
from nr_ota_pe import companion_check

ROOT=Path(__file__).resolve().parents[1]
VERSION='beta0.4.5'
FEEDBACK=['collect-feedback.ps1','Collect-Feedback.cmd','collect-crash-feedback.ps1','Collect-Crash-Feedback.cmd']
ADDONS={
 'Chinese':f'DLSS5-AI渲染超分版-{VERSION}-@野生的装机宅-Bilibili.addon64',
 'English':f'DLSS5-AI-Rendering-Upscaling-{VERSION}-@野生的装机宅-Bilibili.en.addon64',
}
README_ZH='beta0.4.5 — 0.4 集中修复测试版（无 DX11）\n退出游戏，备份旧同类 addon 和 nrchain_nvngx.dll，再放入本包一个语言 addon 与配套 nrchain_nvngx.dll。\n保留原 INI、ReShade 和显卡对应 NVIDIA 运行库；不改驱动。自有配套 DLL 不是 NVIDIA DLL 或 DX11 桥。\n进原问题场景，F6 看效果；后置问题切到后置测试。画质页可一键恢复推荐，恢复不会强开 NR。\nF8 同时保存核心 ZIP 与独立 feedback-report.txt；用“复制完整报告（群文档）”粘贴，ZIP 留存。F7 仅供画面异常补充。\n回复有效/无效、是否闪烁/崩溃。异常就停止并还原备份，不必反复试崩。\n这是未完成游戏实测的候选，不是通用 MFG 解锁或首次安装包。\n'
README_EN='beta0.4.5 — integrated 0.4 D3D12 test, NO DX11\nExit the game; back up the previous addon AND nrchain_nvngx.dll. Install ONE language addon and the companion DLL from this OTA. Preserve INI, ReShade and GPU-specific NVIDIA runtimes.\nUse F6 in the problem scene; test post mode for post-mode bugs. Restore recommended picture settings does not enable NR.\nF8 saves both core ZIP and standalone feedback-report.txt. Copy full TXT into your feedback document; retain ZIP. F7 is optional for image problems.\nStop and roll back on persistent faults. This candidate has not passed target-game validation; no MFG unlock or proprietary runtime included.\n'

def digest(data:bytes)->str:return hashlib.sha256(data).hexdigest()
def source_sha()->str:
    sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if not re.fullmatch('[0-9a-f]{40}',sha): raise ValueError('Invalid source identity')
    return sha

def pe_check(data:bytes,sha:str)->None:
    if len(data)<256 or data[:2]!=b'MZ':raise ValueError('Not a PE image')
    pe=struct.unpack_from('<I',data,0x3c)[0]
    if pe>len(data)-26 or data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid PE header')
    if struct.unpack_from('<H',data,pe+4)[0]!=0x8664 or struct.unpack_from('<H',data,pe+24)[0]!=0x20b:
        raise ValueError('Expected x64 PE32+ addon')
    if VERSION.encode() not in data or sha.encode() not in data:
        raise ValueError('Compiled version/source identity not embedded')
    if '0.4.5 beta'.encode('utf-16le') not in data:
        raise ValueError('PE version text not frozen')

def verify(path:Path,sha:str,language:str)->dict:
    with zipfile.ZipFile(path) as z:
        names=z.namelist()
        required=set(FEEDBACK+['nrchain_nvngx.dll',ADDONS[language],'Instructions.txt','ota-manifest.json'])
        if len(names)!=len(set(names)) or set(names)!=required:raise ValueError('Unexpected/duplicate OTA files')
        if any('/' in n or '\\' in n or n in {'.','..'} for n in names):raise ValueError('OTA path is not a leaf')
        if path.stat().st_size>8*1024*1024:raise ValueError('Unexpected OTA size')
        if sum(i.file_size for i in z.infolist())>16*1024*1024:raise ValueError('Unexpected expanded OTA size')
        manifest=json.loads(z.read('ota-manifest.json'))
        if manifest['schema']!='nr-branch-ota-v1' or manifest['version']!=VERSION or manifest['language']!=language:
            raise ValueError('Wrong package channel or language')
        if manifest['sourceCommit']!=sha or manifest['sourceDirty'] is not False:raise ValueError('Wrong source identity')
        if manifest['api']!='D3D12-x64' or manifest['includesDx11'] is not False:raise ValueError('Wrong API lane')
        payload=manifest['files']
        if {p['name'] for p in payload}!=required-{'ota-manifest.json'}:raise ValueError('Incomplete payload manifest')
        if len(payload)!=len(required)-1:raise ValueError('Duplicate payload identity')
        for entry in payload:
            data=z.read(entry['name'])
            if digest(data)!=entry['sha256'] or len(data)!=entry['bytes']:raise ValueError('Payload hash/size mismatch')
        pe_check(z.read(ADDONS[language]),sha)
        companion_check(z.read("nrchain_nvngx.dll"))
    return {'name':path.name,'bytes':path.stat().st_size,'sha256':digest(path.read_bytes()),'sourceCommit':sha,'language':language}

def package(out:Path,sha:str)->None:
    changes=subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True)
    if changes.strip():raise ValueError('Release requires clean tracked source: '+changes)
    out.mkdir(parents=True,exist_ok=False)
    for language,addon in ADDONS.items():
        data={addon:(ROOT/'build'/addon).read_bytes(),'nrchain_nvngx.dll':(ROOT/'build/nrchain_nvngx.dll').read_bytes()}
        pe_check(data[addon],sha)
        companion_check(data["nrchain_nvngx.dll"])
        for name in FEEDBACK:data[name]=(ROOT/'scripts'/name).read_bytes()
        readme=README_ZH if language=='Chinese' else README_EN
        data['Instructions.txt']=b'\xef\xbb\xbf'+readme.replace('\n','\r\n').encode('utf-8')
        manifest={'schema':'nr-branch-ota-v1','version':VERSION,'language':language,'api':'D3D12-x64',
          'sourceCommit':sha,'sourceDirty':False,'includesDx11':False,'channel':'targeted-test-prerelease',
          'preserve':['nr_before_sr.ini','ReShade.ini','ReShadePreset.ini','nvngx_dlssnr.dll','nvngx_dlss.dll','nvngx_dlssg.dll','nvngx_dlssd.dll'],
          'files':[{'name':n,'bytes':len(b),'sha256':digest(b)} for n,b in sorted(data.items())]}
        data['ota-manifest.json']=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
        path=out/f'DLSS5-{VERSION}-{language}-OTA.zip'
        with zipfile.ZipFile(path,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for name,blob in sorted(data.items()):
                info=zipfile.ZipInfo(name,date_time=(2026,9,6,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
                z.writestr(info,blob)
        print(json.dumps(verify(path,sha,language),ensure_ascii=False))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'ota')
    p.add_argument('--verify-dir',type=Path)
    a=p.parse_args();sha=source_sha()
    if a.verify_dir:
        expected={f'DLSS5-{VERSION}-{language}-OTA.zip' for language in ADDONS}
        if {f.name for f in a.verify_dir.iterdir()}!=expected:raise SystemExit('Expected exactly two OTA ZIPs')
        for language in ADDONS:print(json.dumps(verify(a.verify_dir/f'DLSS5-{VERSION}-{language}-OTA.zip',sha,language)))
    else: package(a.output,sha)
