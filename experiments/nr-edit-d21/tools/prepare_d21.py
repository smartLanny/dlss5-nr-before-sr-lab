#!/usr/bin/env python3
"""Export immutable local v4, adapt it to frozen D21, and build a separate candidate.
No checkout/reset/stash/commit/push, PR closure or game installation.
"""
from pathlib import Path,PurePosixPath
import argparse,hashlib,io,json,os,re,subprocess,sys,tarfile,time
from upgrade_v4 import BASE,D21,upgrade
ROOT=Path(__file__).resolve().parents[1]
ALLOWED={'.cpp','.c','.h','.hpp','.inl','.hlsl','.hlsli','.py','.ps1','.md','.json','.txt','.cmake'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(core,*args):
    p=subprocess.run(['git','-C',str(core),*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode:raise RuntimeError('git '+args[0]+': '+p.stderr.decode('utf-8',errors='replace').strip())
    return p.stdout

def target(core,descendant=False):
    top=Path(git(core,'rev-parse','--show-toplevel').decode().strip()).resolve()
    if top!=core:raise ValueError('--core must be the worktree root')
    head=git(core,'rev-parse','HEAD^{commit}').decode().strip()
    if not re.fullmatch('[0-9a-f]{40}',head):raise ValueError('Missing full source commit')
    if head!=D21:
        if not descendant:raise ValueError('Select reviewed D21 '+D21+' or explicitly allow its descendant')
        git(core,'merge-base','--is-ancestor',D21,head)
    if git(core,'status','--porcelain','--untracked-files=no').strip():
        raise ValueError('Tracked uncommitted changes present; select a clean owner-approved checkpoint')
    for rel in ['AGENTS.md','src/nr_full_runtime.inl','src/nr_full_pass.hpp','src/nr_dual_runtime.inl','scripts/prepare-multi-api-beta.py']:
        (core/rel).resolve(strict=True)
    return head

def extract(data,destination):
    if destination.exists():raise FileExistsError('Preserve existing output')
    entries=[];seen=set()
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:') as tar:
        for m in tar.getmembers():
            p=PurePosixPath(m.name)
            if p.is_absolute() or '..' in p.parts or '\\' in m.name or ':' in m.name:raise ValueError('Unsafe archive path')
            if m.isdir():continue
            if not m.isfile() or m.size>16*1024*1024:raise ValueError('Source-only regular files required')
            rel=p.relative_to('experiments/nr-edit')
            if not rel.parts or (rel.suffix.lower() not in ALLOWED and rel.name not in ('.gitignore','CMakeLists.txt')):raise ValueError('Unexpected file category')
            key=str(rel).casefold()
            if key in seen:raise ValueError('Duplicate/case-colliding archive path')
            seen.add(key);stream=tar.extractfile(m)
            if stream is None:raise ValueError('Unreadable archive member')
            content=stream.read()
            if len(content)!=m.size:raise ValueError('Truncated archive member')
            entries.append((Path(*rel.parts),content))
    if not {'src/nr_edit_core_runtime.inl','tools/stage_edit_integration.py','tools/build_against.py'}.issubset(seen):
        raise ValueError('Incomplete old overlay')
    destination.mkdir(parents=True)
    hashes={}
    for rel,data in entries:
        p=destination/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);hashes[rel.as_posix()]=sha(p)
    return hashes

def dependencies(a,core):
    common=Path(git(core,'rev-parse','--path-format=absolute','--git-common-dir').decode().strip())
    lab=Path(a.lab_root or os.environ.get('DLSS5_LAB_ROOT') or common.parent.parent/'dlss5-lab').resolve()
    options={'toolchain':[p.parent.parent for p in (lab/'toolchains').glob('llvm-mingw-*/bin/x86_64-w64-mingw32-clang++.exe')],
             'reshade':[lab/'toolchains/reshade-source'],'upstream':[lab/'deps/dlss5-dx11-bridge-28aed409']}
    markers={'toolchain':'bin/x86_64-w64-mingw32-clang++.exe','reshade':'include/reshade.hpp','upstream':'.git'}
    env={'toolchain':'DLSS5_LLVM_MINGW_ROOT','reshade':'DLSS5_RESHADE_ROOT','upstream':'DLSS5_UPSTREAM_ROOT'}
    result={}
    for name in options:
        explicit=getattr(a,name) or os.environ.get(env[name])
        choices=[Path(explicit).resolve()] if explicit else [p for p in options[name] if (p/markers[name]).exists()]
        if len(choices)!=1 or not (choices[0]/markers[name]).exists():raise ValueError('Specify existing --'+name+'; no unique dependency found')
        result[name]=choices[0]
    return result

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--output',type=Path)
    for name in ['lab-root','toolchain','reshade','upstream','codec-cache','face-runtime']:p.add_argument('--'+name,type=Path)
    p.add_argument('--allow-descendant',action='store_true');p.add_argument('--prepare-only',action='store_true');p.add_argument('--stage-only',action='store_true')
    a=p.parse_args(argv);core=a.core.resolve(strict=True);head=target(core,a.allow_descendant)
    deps={} if a.prepare_only else dependencies(a,core)
    out=(a.output or ROOT/'build'/('d21-'+time.strftime('%Y%m%d-%H%M%S'))).resolve()
    if out.exists() or out==core or out in core.parents or out==ROOT or out in ROOT.parents:raise ValueError('Choose a NEW separate output directory')
    # Prove old source availability before writing a candidate.
    if git(core,'rev-parse',BASE+'^{commit}').decode().strip()!=BASE:raise ValueError('Missing immutable v4 source')
    archive=git(core,'archive','--format=tar',BASE,'experiments/nr-edit')
    source_files=['src/nr_full_runtime.inl','src/nr_full_pass.hpp','src/nr_dual_runtime.inl','src/nr_processing_runtime.inl','scripts/build.ps1']
    source_hashes={r:sha(core/r) for r in source_files}
    out.mkdir(parents=True)
    report={'schema':'nr-edit-d21-build-v1','target_commit':head,'overlay_base':BASE,'target_inputs':source_hashes,
            'status':'preparing','windows_build':'not-run','nvidia':'not-run','game':'not-run',
            'active_checkout_changed':False,'git_commit_created':False,'pr_mutations':False}
    def save():(out/'D21-RESULT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    try:
        save();overlay=out/'overlay';report['v4_exported']=extract(archive,overlay)
        upgrade(overlay,ROOT)
        report['upgrade_manifest_sha256']=sha(overlay/'D21-UPGRADE.json')
        if target(core,a.allow_descendant)!=head or source_hashes!={r:sha(core/r) for r in source_files}:
            raise RuntimeError('Target changed during preparation')
        if a.prepare_only:
            report['status']='prepared-only-NOT-merged-or-built';save();print(report['status']+': '+str(out));return 0
        args=[sys.executable,str(overlay/'tools/build_against.py'),'--core',str(core),'--output',str(out/'validation')]
        for name,value in deps.items():args+=['--'+name,str(value)]
        for name in ['codec_cache','face_runtime']:
            value=getattr(a,name)
            if value:args+=['--'+name.replace('_','-'),str(value.resolve(strict=True))]
        if a.stage_only:args+=['--stage-only']
        report['command']=args;save()
        with (out/'runner.log').open('wb') as log:
            done=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT)
        report['exit_code']=done.returncode
        evidence=out/'validation/validation.json'
        if evidence.exists():report['build_result']=json.loads(evidence.read_text(encoding='utf-8-sig'))
        if done.returncode:raise RuntimeError('Build failed; preserve runner.log and validation/logs. No game file changed.')
        if a.stage_only:report['status']='staged-only-NOT-compiled-or-merged'
        else:
            if report.get('build_result',{}).get('status')!='windows-build-and-warp-passed-game-pending':
                raise RuntimeError('Missing required successful build/WARP receipt')
            report['windows_build']='passed';report['status']='candidate-built-WARP-passed-NVIDIA-game-pending'
        save();print(report['status']+': '+str(out));return 0
    except Exception as e:
        report['status']='failed';report['error']=str(e);save();raise
if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as e:
        print('FAILED: '+str(e),file=sys.stderr);raise SystemExit(1)
