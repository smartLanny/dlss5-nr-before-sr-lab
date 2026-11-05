#!/usr/bin/env python3
"""Exact generated Core codec on D3D12 WARP/hardware, with controlled model planes.
Every selected CSO is tied to the actual HLSL. No NGX/inference/game claim.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys,time
R=Path(__file__).resolve().parents[1]
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',required=True,type=Path);p.add_argument('--compiler',required=True)
    p.add_argument('--output',required=True,type=Path);p.add_argument('--artifacts',type=Path)
    p.add_argument('--hardware',action='store_true');p.add_argument('--require-manifest',action='store_true')
    a=p.parse_args();src=a.source_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    if os.name!='nt':raise RuntimeError('Requires Windows D3D12; no CPU fallback')
    text=(src/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    if 'NR_RECONSTRUCTION_MATH_HLSLI' not in text or 'constants.region_flags |= reconstruction.flags;' not in text:
        raise ValueError('Input is not the actual reconstruction candidate; preserve as negative baseline only')
    receipt={'source_sha256':sha(src/'src/nr_before_sr.cpp'),'model':'controlled FP16 input/output planes, not NGX',
             'game_verified':False,'hardware':a.hardware,'manifest_verified':False,'runs':[]}
    if a.require_manifest:
        mf=src.parent/'source-manifest.json';m=json.loads(mf.read_text(encoding='utf-8-sig'))
        for n,h in m['files'].items():
            path=(mf.parent/n).resolve();assert path.is_relative_to(mf.parent) and sha(path)==h,n
        receipt.update(manifest_verified=True,source_commit=m['source_commit'])
    artifact_args=[]
    if a.artifacts:
        artifacts=a.artifacts.resolve();matches=re.findall(r'const char kCodecShader\[\] = R"HLSL\((.*?)\)HLSL";',text,re.S)
        assert len(matches)==1
        shader_hash=hashlib.sha256(matches[0].encode()).hexdigest()
        selected={}
        index=artifacts/'codec-artifacts.json'
        if index.is_file():
            catalog=json.loads(index.read_text(encoding='utf-8-sig'))
            assert catalog['schema']=='nr-codec-artifacts-v1' and catalog['source']==shader_hash
            parents=set()
            for name in ['EncodeV2CS','CombineCS','CombineV2CS']:
                rows=[r for r in catalog['artifacts'] if r['entry']==name and not r['face'] and r['domain']==-1]
                assert len(rows)==1
                row=rows[0];cso=(artifacts/row['file']).resolve()
                assert cso.is_relative_to(artifacts) and row['shader_sha256']==shader_hash and row['sha256']==sha(cso)
                assert row['bytes']==cso.stat().st_size and row['target']=='cs_5_0' and row['flags']=='optimization-level-3'
                selected[name]={'file':str(cso),'sha256':sha(cso)};parents.add(cso.parent)
            assert len(parents)==1
            artifacts=parents.pop()
        else:
            assert (artifacts/'actual-codec.hlsl').read_bytes()==matches[0].encode()
            for name in ['EncodeV2CS','CombineCS','CombineV2CS']:
                cso=artifacts/(name+'-face0-domain-1.cso');m=json.loads((artifacts/(name+'-face0-domain-1.json')).read_text(encoding='utf-8-sig'))
                assert m['sha256']==sha(cso) and m['shader_sha256']==shader_hash
                selected[name]={'file':str(cso),'sha256':sha(cso)}
        receipt['exact_bytecode']=selected
        artifact_args=['--artifacts='+str(artifacts)]
    def run(name,cmd,expect=0,timeout=1200):
        begin=time.monotonic()
        with (out/(name+'.stdout.log')).open('x',encoding='utf-8') as so,(out/(name+'.stderr.log')).open('x',encoding='utf-8') as se:
            r=subprocess.run(list(map(str,cmd)),cwd=R,stdout=so,stderr=se,timeout=timeout)
        item={'name':name,'exit':r.returncode,'expected':expect,'seconds':time.monotonic()-begin}
        receipt['runs'].append(item);print(json.dumps(item),flush=True)
        if r.returncode!=expect:raise RuntimeError(name+' failed: '+(out/(name+'.stderr.log')).read_text()[-3500:])
    try:
        harness=out/'post_scaled_work_gpu.cpp'
        run('extract',[sys.executable,R/'scripts/extract-post-gpu.py','--source-root',src,'--template',R/'tests/post_scaled_work_gpu.cpp.in','--output',harness])
        receipt['ci_warp_runtime_helper_sha256']=sha(out/'ci_warp_runtime.h')
        exe=out/'reconstruction_gpu.exe'
        run('compile',[a.compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror',R/'tests/reconstruction_gpu.cpp','-I'+str(out),'-I'+str(src/'src/interop'),'-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid','-lole32','-o',exe])
        args=artifact_args+(['--hardware'] if a.hardware else [])
        run('positive',[exe,*args])
        run('negative-disabled-protection',[exe,*args,'--negative-disable-guard'],1)
        assert 'near-black chroma GPU protection did not intervene' in (out/'negative-disabled-protection.stderr.log').read_text()
        receipt['status']='passed'
    except Exception as e:receipt.update(status='failed',error=str(e));raise
    finally:(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
