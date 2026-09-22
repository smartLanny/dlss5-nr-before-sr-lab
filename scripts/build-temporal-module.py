"""Compile the actual production seam against an exact externally built SDK."""
import argparse,hashlib,json,subprocess,shutil,sys
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser()
    for key in ('source','compiler','sdk','sdk-cache','output'):ap.add_argument('--'+key,required=True,type=Path)
    a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sdk=a.sdk.resolve();cache=a.sdk_cache.resolve();compiler=a.compiler.resolve();src=a.source.resolve()
    manifest=json.loads((cache/'build.json').read_text());pin='c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
    assert subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()==pin
    assert not subprocess.check_output(['git','-C',str(sdk),'status','--porcelain'],text=True).strip()
    assert manifest['sdk_commit']==pin and manifest['effect_context_alignment_fixed']
    assert manifest['compiler_sha256']==sha(compiler) and manifest['license_sha256']==sha(sdk/'LICENSE.txt')
    objects=[]
    for item in manifest['sdk_objects']:
        p=Path(item['path']);assert sha(p)==item['sha256'];objects.append(str(p))
    assert len(objects)==8
    record={'passed':False,'sdk_build_sha256':sha(cache/'build.json'),'objects':objects,'commands':[],'source_sha256':{p.name:sha(p) for p in src.iterdir() if p.is_file()}}
    def save():(out/'module.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    artifacts=out/'bytecode'
    command=[sys.executable,str(Path(__file__).with_name('prepare-temporal-artifacts.py')),
        '--source',str(src),'--compiler',str(compiler),'--output',str(artifacts)]
    result=subprocess.run(command,capture_output=True,text=True)
    (out/'bytecode-build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    record['commands'].append({'command':command,'exit':result.returncode});save()
    if result.returncode:raise RuntimeError('Required temporal bytecode build failed: '+result.stderr[-4000:])
    receipt=json.loads((artifacts/'temporal-artifacts.json').read_text(encoding='utf-8'))
    assert receipt['passed'] and receipt['source_sha256']==sha(src/'residual_accumulator_shader.hpp')
    assert receipt['header_sha256']==sha(artifacts/'nr_temporal_precompiled.hpp')
    record['temporal_artifacts_sha256']=sha(artifacts/'temporal-artifacts.json')
    record['temporal_header_sha256']=receipt['header_sha256']
    for name in ('amd_of_producer','recorded_temporal'):
        obj=out/(name+'.o');cmd=[str(compiler),'-std=c++17','-O2','-fms-extensions','-DNDEBUG','-DFFX_OF','-DUNICODE','-D_UNICODE',
            '-include','windows.h','-include','cwchar','-Wall','-Wextra','-Werror',
            '-DNR_TEMPORAL_PRECOMPILED=1','-I',str(artifacts),
            '-isystem',str(sdk/'sdk/include'),'-c',str(src/(name+'.cpp')),'-o',str(obj)]
        r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        record['commands'].append({'command':cmd,'exit':r.returncode});save()
        if r.returncode:raise RuntimeError(r.stderr[-4000:])
        objects.append(str(obj))
    shutil.copyfile(sdk/'LICENSE.txt',out/'AMD-FidelityFX-LICENSE.txt')
    record['license_sha256']=sha(out/'AMD-FidelityFX-LICENSE.txt')
    record['passed']=True;record['object_sha256']={p:sha(Path(p)) for p in objects};save()
if __name__=='__main__':main()
