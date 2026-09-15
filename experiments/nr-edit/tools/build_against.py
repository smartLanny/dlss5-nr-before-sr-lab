#!/usr/bin/env python3
"""Build the additive NR edit package against a frozen local product checkout.
No checkout/reset/stash/cherry-pick, installation, configuration rewrite or push.
"""
from __future__ import annotations
import argparse,datetime,hashlib,json,os,re,subprocess,sys
from pathlib import Path
import stage_edit_integration as integration
PACKAGE=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(root,*args):
 r=subprocess.run(['git','-C',str(root),*args],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 return r.stdout.decode('utf-8').strip()
def source_snapshot(root):
 result={}
 for folder in ('src','scripts','config','third_party','tools','shaders','include'):
  path=root/folder
  if not path.exists():continue
  for p in path.rglob('*'):
   if p.is_symlink():raise RuntimeError('Symlink source refused: '+str(p))
   if p.is_file() and p.suffix.lower() in {'.cpp','.c','.h','.hpp','.inl','.hlsl','.hlsli','.def','.rc','.manifest','.json','.py','.ps1','.ini','.txt','.md','.cmake'}:
    result[p.relative_to(root).as_posix()]=sha(p)
 return result

def dependency(explicit,env,alternatives,label):
 value=explicit or os.environ.get(env)
 if value:
  p=Path(value).expanduser().resolve(strict=True)
  return p
 matches=[p.resolve() for p in alternatives if p.is_dir()]
 if len(matches)!=1:raise RuntimeError('Cannot uniquely resolve '+label+'; provide --'+label)
 return matches[0]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--core',type=Path,default=PACKAGE.parents[1]);p.add_argument('--output',type=Path)
 p.add_argument('--toolchain',type=Path);p.add_argument('--reshade',type=Path);p.add_argument('--upstream',type=Path)
 p.add_argument('--codec-cache',type=Path);p.add_argument('--face-runtime',type=Path)
 p.add_argument('--stage-only',action='store_true');p.add_argument('--allow-dirty-source',action='store_true')
 a=p.parse_args();core=a.core.resolve(strict=True)
 commit=git(core,'rev-parse','HEAD')
 if not re.fullmatch('[0-9a-f]{40}',commit):raise RuntimeError('No exact target commit')
 changes=git(core,'status','--porcelain','--untracked-files=no')
 if changes and not a.allow_dirty_source:raise RuntimeError('Target has tracked uncommitted work. Commit/checkpoint it first; no files have been changed.')
 lab=Path(os.environ.get('DLSS5_LAB_ROOT',str(Path.home()/'Documents/ChatGPT/DLSS5/dlss5-lab')))
 toolchain=dependency(a.toolchain,'DLSS5_LLVM_MINGW_ROOT',[lab/'toolchains/llvm-mingw-20260826-ucrt-x86_64'],'toolchain')
 reshade=dependency(a.reshade,'DLSS5_RESHADE_ROOT',[lab/'toolchains/reshade-source'],'reshade')
 upstream=dependency(a.upstream,'DLSS5_UPSTREAM_ROOT',[lab/'deps/dlss5-dx11-bridge-28aed409'],'upstream')
 compiler=toolchain/'bin/x86_64-w64-mingw32-clang++.exe'
 for required in [compiler,reshade/'include/reshade.hpp',core/'scripts/prepare-multi-api-beta.py']:
  required.resolve(strict=True)
 timestamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
 out=(a.output or PACKAGE/'build'/('against-'+commit[:8]+'-'+timestamp)).resolve()
 if out.exists():raise RuntimeError('Output already exists; choose a fresh directory: '+str(out))
 if out==core or out in core.parents:raise RuntimeError('Unsafe output directory')
 before=source_snapshot(core)
 experiment={f.relative_to(PACKAGE).as_posix():sha(f) for folder in ['src','tools','shaders','tests']
  for f in (PACKAGE/folder).rglob('*') if f.is_file() and f.suffix in {'.py','.cpp','.hpp','.inl','.hlsl','.hlsli'}}
 out.mkdir(parents=True);logs=out/'logs';logs.mkdir()
 report={'schema':'nr-edit-build-against-v4','target_commit':commit,'target_dirty':bool(changes),
  'target_root':str(core),'experiment_root':str(PACKAGE),'source_inputs':before,'experiment_inputs':experiment,
  'commands':[],'status':'running','nvidia_nr_sr':'not-run','game':'not-run'}
 def save(): (out/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 def run(name,args):
  print('Running '+name,flush=True);log=logs/(name+'.log')
  with log.open('wb') as file:
   r=subprocess.run([str(v) for v in args],stdout=file,stderr=subprocess.STDOUT)
  report['commands'].append({'name':name,'args':[str(v) for v in args],'exit_code':r.returncode,'log_sha256':sha(log)})
  save()
  if r.returncode:raise RuntimeError(name+' failed; inspect '+str(log))
 try:
  save();generated=out/'generated';candidate=out/'candidate'
  run('product-generator',[sys.executable,core/'scripts/prepare-multi-api-beta.py','--core',core,'--upstream',upstream,'--output',generated])
  if git(core,'rev-parse','HEAD')!=commit or source_snapshot(core)!=before:raise RuntimeError('Target changed during generation; reject this candidate and retry a frozen checkout')
  manifest=integration.stage(generated/'core',candidate,commit)
  report['version']=manifest['base_version'];report['preserved_count']=len(manifest['preserved_files'])
  report['integration_manifest_sha256']=sha(candidate/'edit-integration-manifest.json');save()
  if a.stage_only:report['status']='source-staged-not-built';save();print('Source stage only: '+str(candidate));return
  args=['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',candidate/'scripts/build.ps1','-SkipUnchangedNrChain',
   '-ToolchainRoot',toolchain,'-ReShadeSourceRoot',reshade]
  if a.codec_cache:args+=['-CodecCacheRoot',a.codec_cache.resolve(strict=True)]
  face=a.face_runtime or os.environ.get('DLSS5_FACE_RUNTIME_ROOT')
  if face:args+=['-FaceRuntimeRoot',face]
  run('product-build',args)
  binaries=sorted((candidate/'build').glob('*-edit-v4.addon64'))
  if len(binaries)!=2:raise RuntimeError('Expected two language addons, found '+str(len(binaries)))
  run('runtime-test-compile',[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-I'+str(PACKAGE/'src'),
   PACKAGE/'tests/runtime_contract_test.cpp','-o',out/'runtime-contract-test.exe'])
  run('runtime-test',[out/'runtime-contract-test.exe'])
  run('warp-test-compile',[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-static','-municode','-I'+str(candidate/'src'),
   PACKAGE/'tests/windows_shader_validation.cpp',candidate/'src/nr_edit_dx12.cpp','-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid','-lole32','-o',out/'warp-edit.exe'])
  run('warp-six-shaders',[out/'warp-edit.exe',candidate/'build'])
  if source_snapshot(core)!=before:raise RuntimeError('Original checkout changed during validation; result is bound to frozen staged source only')
  report['addons']={p.name:sha(p) for p in binaries}
  report['shader_manifest_sha256']=sha(candidate/'build/edit-shader-manifest.json')
  report['status']='windows-build-and-warp-passed-game-pending'
  print('Built and WARP-validated: '+str(candidate/'build'),flush=True)
 except Exception as e:
  report['status']='failed';report['error']=str(e);save();raise
 save()
if __name__=='__main__':
 try:main()
 except Exception as e:print('FAILED: '+str(e),file=sys.stderr);raise SystemExit(1)
