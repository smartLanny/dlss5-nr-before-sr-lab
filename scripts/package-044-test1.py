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

ROOT=Path(__file__).resolve().parents[1]
VERSION='beta0.4.4-test1'
FEEDBACK=['collect-feedback.ps1','Collect-Feedback.cmd','collect-crash-feedback.ps1','Collect-Crash-Feedback.cmd']
ADDONS={
 'Chinese':f'DLSS5-AI渲染超分版-{VERSION}-@野生的装机宅-Bilibili.addon64',
 'English':f'DLSS5-AI-Rendering-Upscaling-{VERSION}-@野生的装机宅-Bilibili.en.addon64',
}
README_ZH='''beta0.4.4-test1 — D3D12 定向修复测试版（不含 DX11）
1. 完全退出游戏；将旧版同类 addon 备份到游戏目录外，只放入本包的一个语言版本。
2. 保留原有 nr_before_sr.ini、ReShade、nrchain_nvngx.dll 和显卡对应的 NR 运行库，不改驱动或 DLSS DLL。
3. 进原问题场景，按 F6 看是否有变化。回复：有效/无效、还闪不闪、是否崩溃。F8 收日志；画面异常且方便时补 F7。
崩溃/黑屏/持续异常就停下，恢复旧 addon，不必反复测试。
本包不自动安装、不自动上传、不含默认 INI，不是首次安装包，也不是 DX11/MFG 解锁包。
修复候选不等于已经确认你的游戏通过；包内 ota-manifest.json 记录精确源码和文件 SHA256。
'''
README_EN='''beta0.4.4-test1 — targeted D3D12 compatibility test (NO D3D11)
1. Exit the game. Back up the old copy of this add-on outside the game directory. Install ONE language only.
2. Preserve nr_before_sr.ini, ReShade, nrchain_nvngx.dll and your existing GPU-specific NR runtime. Do not change drivers or DLSS DLLs for this test.
3. Revisit the problem scene. Use F6 to compare. Report whether it works, flickers or crashes. Collect F8; add F7 only for a picture problem when convenient.
Stop on crashes, black screens or persistent faults and restore the backup. Repeated crashes are not required.
No automatic installation/upload, no default INI, no proprietary runtime, no D3D11 support or MFG unlock is included.
This is a candidate, not a claim of game verification. ota-manifest.json identifies the exact source and payload hashes.
'''

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
    if '0.4.4-test1 beta'.encode('utf-16le') not in data:
        raise ValueError('PE version text not frozen')

def verify(path:Path,sha:str,language:str)->dict:
    with zipfile.ZipFile(path) as z:
        names=z.namelist()
        required=set(FEEDBACK+[ADDONS[language],'Instructions.txt','ota-manifest.json'])
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
    return {'name':path.name,'bytes':path.stat().st_size,'sha256':digest(path.read_bytes()),'sourceCommit':sha,'language':language}

def package(out:Path,sha:str)->None:
    changes=subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True)
    if changes.strip():raise ValueError('Release requires clean tracked source: '+changes)
    out.mkdir(parents=True,exist_ok=False)
    for language,addon in ADDONS.items():
        data={addon:(ROOT/'build'/addon).read_bytes()}
        pe_check(data[addon],sha)
        for name in FEEDBACK:data[name]=(ROOT/'scripts'/name).read_bytes()
        readme=README_ZH if language=='Chinese' else README_EN
        data['Instructions.txt']=b'\xef\xbb\xbf'+readme.replace('\n','\r\n').encode('utf-8')
        manifest={'schema':'nr-branch-ota-v1','version':VERSION,'language':language,'api':'D3D12-x64',
          'sourceCommit':sha,'sourceDirty':False,'includesDx11':False,'channel':'targeted-test-prerelease',
          'preserve':['nr_before_sr.ini','ReShade.ini','ReShadePreset.ini','nrchain_nvngx.dll','nvngx_dlssnr.dll'],
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
