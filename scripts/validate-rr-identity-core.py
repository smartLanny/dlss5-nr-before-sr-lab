#!/usr/bin/env python3
"""Isolated identity copy with actual complete-Core WARP state/slot/fence adapters; no NGX."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import time
from temporal_test_objects import objects_for

ROOT=Path(__file__).resolve().parents[1]
MODES=('lifetime','failed-reset','unsubmitted','state-refusal','opaque-failure','off','replay')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('core','compiler','reshade','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--mode',action='append',choices=MODES)
    parser.add_argument('--negative',choices=('recording-pin','envelope-restore'),
        help='Explicit test-only removal; expected exit41 at the exact named assertion, never a skipped production gate.')
    parser.add_argument('--compile-only',action='store_true')
    a=parser.parse_args();core=a.core.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=core/'src/nr_before_sr.cpp';build=core/'build';compiler=a.compiler.resolve(strict=True)
    manifest_path=core.parent/'source-manifest.json';manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for name,digest in manifest['files'].items():
        p=(manifest_path.parent/name).resolve(strict=True)
        if not p.is_relative_to(manifest_path.parent.resolve()) or sha(p)!=digest:raise ValueError('Generated source receipt mismatch: '+name)
    rsp=build/'obj/source-defines.rsp';rsp.resolve(strict=True)
    original_source=source
    queue=ROOT/'tests/queue_retirement_harness.cpp';descriptor=ROOT/'tests/descriptor_provenance_warp.inl'
    adapter=ROOT/'tests/native_observer_device.inl';fixture=ROOT/'tests/rr_identity_core_host.cpp.in'
    module=ROOT/'experiments/nr-edit/src/nr_rr_identity_dx12.cpp'
    queue_text=queue.read_text(encoding='utf-8-sig');descriptor_text=descriptor.read_text(encoding='utf-8-sig')
    anchors=('void OneShot() {','template<class Function> NgxResult Native(')
    if queue_text.count(anchors[0])!=1 or descriptor_text.count(anchors[1])!=1:raise ValueError('Existing real WARP helper boundary changed')
    helpers=queue_text.split(anchors[0],1)[0]+'\n}\n'+descriptor_text.split(anchors[1],1)[0]+'\n}\n'
    (out/'helpers.inl').write_text(helpers,encoding='utf-8')
    source_text=source.read_text(encoding='utf-8-sig')
    if a.negative=='recording-pin':
        pattern=r'(bool RecordNrUseLocked\(ID3D12GraphicsCommandList \*command_list,\s*NrContext &context, NrSlot &slot\) \{)'
        changed,count=re.subn(pattern,r'\1\n    return true; // TEST-ONLY removal of production recording publication.\n',source_text)
        if count!=1:raise ValueError('Exact production RecordNrUse negative anchor changed')
        source=out/'negative-core.cpp';source.write_text(changed,encoding='utf-8')
    wrapper=out/'rr-identity-core.cpp'
    wrapper.write_text(f'#include "{source.as_posix()}"\n#include "helpers.inl"\n#include "{fixture.as_posix()}"\n'
        'int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);'
        'if(argc!=2)return 2;rr_identity_core_test::Run(argv[1]);return 0;}\n',encoding='utf-8')
    objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]
    objects+=objects_for(original_source)
    inputs=[queue,descriptor,adapter,fixture,module,module.with_suffix('.hpp'),ROOT/'experiments/nr-edit/src/nr_edit_dx12.hpp',ROOT/'experiments/nr-edit/src/nr_private_sr_runtime.hpp',Path(__file__).resolve()]
    # The experiment ABI headers must match the assembled Core's actual headers.
    # File location differs after staging, but their content must not.
    staged_headers={}
    for header in ('nr_edit_dx12.hpp','nr_private_sr_runtime.hpp'):
        staged=core/'src/private_sr'/header
        if not staged.is_file():staged=core/'src'/header
        if not staged.is_file():raise ValueError('Staged experiment ABI header not found: '+header)
        if sha(staged)!=sha(ROOT/'experiments/nr-edit/src'/header):raise ValueError('Experiment/Core ABI header mismatch: '+header)
        staged_headers[header]=staged
    api_text=module.with_suffix('.hpp').read_text(encoding='utf-8-sig')
    if api_text.count('#include "nr_edit_dx12.hpp"')!=1:raise ValueError('Identity public header include anchor changed')
    # The full Core already includes the staged copy. Use that exact same file
    # in this TU so pragma-once does not define identical ABI types twice.
    api_text=api_text.replace('#include "nr_edit_dx12.hpp"',f'#include "{staged_headers["nr_edit_dx12.hpp"].as_posix()}"')
    (out/'nr_rr_identity_dx12.hpp').write_text(api_text,encoding='utf-8')
    modes=['lifetime'] if a.negative else list(a.mode or MODES)
    record={'schema':'rr-identity-production-adapter-v1','result':'running','core_source_commit':manifest['source_commit'],
        'core_source':str(original_source),'core_source_sha256':sha(original_source),'manifest_sha256':sha(manifest_path),
        'source_defines_rsp_sha256':sha(rsp),'compiler_sha256':sha(compiler),
        'inputs_sha256':{str(p.relative_to(ROOT)):sha(p) for p in inputs},
        'linked_objects_sha256':{str(p):sha(p) for p in objects},'commands':[],'cases':[],
        'negative':a.negative,'negative_core_sha256':sha(source) if a.negative=='recording-pin' else None,
        'wrapper_sha256':sha(wrapper),'helpers_sha256':sha(out/'helpers.inl'),
        'public_header_adapter':{'only_change':'include exact previously included staged nr_edit_dx12.hpp','sha256':sha(out/'nr_rr_identity_dx12.hpp'),'staged_headers_sha256':{str(p):sha(p) for p in staged_headers.values()}},
        'scope':'Complete assembled Core NativeCommandStateScope, real Create/setter/Reset hooks, AcquireBindingPacket/RecordNrUseLocked/CanMutateSlotLocked and actual WARP Execute/Signal/fence. Controlled source/module/feature facts, resource params and copy-only native callback. No actual Feature13/NGX, game, quality, FPS, or default route claim.',
        'second_submission_ledger':False,'product_entry_connected':False,'nvidia_ngx':False,'game':False,'warp':False}
    def save():(out/'validation.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    def run(name,command,timeout):
        start=time.monotonic()
        try:r=subprocess.run(list(map(str,command)),cwd=out,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as e:
            (out/(name+'.log')).write_bytes((e.stdout or b'')+(e.stderr or b''));record['commands'].append({'name':name,'args':list(map(str,command)),'exit':None,'timeout':timeout});save();raise
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        record['commands'].append({'name':name,'args':list(map(str,command)),'exit':r.returncode,'seconds':time.monotonic()-start})
        save();print((r.stdout+r.stderr).decode('utf-8','replace')[-12000:],flush=True);return r
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1',
        '-ffunction-sections','-fdata-sections','-I'+str(build),'-I'+str(original_source.parent),
        '-I'+str(out),'-I'+str(ROOT/'experiments/nr-edit/src'),'-I'+str(ROOT/'tests'),
        '-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade.resolve()/'include'),'-I'+str(a.reshade.resolve()/'deps/imgui'),'@'+str(rsp)]
    if a.negative=='envelope-restore':flags.append('-DNR_RR_IDENTITY_NEGATIVE_RESTORE_SKIP=1')
    save()
    try:
        experiment=out/'nr_rr_identity_dx12.o'
        result=run('compile-identity-module',[compiler,*flags,'-c',module,'-o',experiment],120)
        if result.returncode:record['result']='failed';save();return result.returncode
        record['identity_object_sha256']=sha(experiment)
        binary=out/'rr-identity-core.exe'
        result=run('compile-core',[compiler,*flags,'-static','-Wl,--gc-sections',wrapper,experiment,*objects,'-o',binary,
            '-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],240)
        if result.returncode:record['result']='failed';save();return result.returncode
        record['binary_sha256']=sha(binary)
        if a.compile_only:record['result']='compiled-not-run';save();return 0
        expected='LEASE_PINNED_BEFORE_FIRST_COPY' if a.negative=='recording-pin' else 'PRODUCTION_ENVELOPE_RESTORED'
        for mode in modes:
            r=run('warp-'+mode,[binary,mode],60);log=(r.stdout+r.stderr).decode('utf-8','replace')
            passed=(r.returncode==41 and 'FAIL '+expected in log) if a.negative else r.returncode==0 and 'PASS identity Core '+mode+':' in log
            record['cases'].append({'mode':mode,'exit':r.returncode,'expected_assertion':expected if a.negative else None,'passed':passed})
            record['warp']=True;save()
            if not passed:record['result']='failed';save();return r.returncode or 1
        if any(sha(p)!=record['inputs_sha256'][str(p.relative_to(ROOT))] for p in inputs):raise ValueError('Input changed during run; retain receipt but do not promote')
        record['result']='negative-reproduced' if a.negative else 'passed';save();return 0
    except Exception as e:record['result']='failed';record['error']=str(e);save();raise
if __name__=='__main__':raise SystemExit(main())
