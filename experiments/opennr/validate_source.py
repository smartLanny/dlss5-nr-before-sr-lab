"""Compile/run upstream portable OpenNR tests without changing its checkout.

No vendor DLL, generated PTX, CUDA assembly, or NVIDIA workload is involved.
Windows-only PE/hash/process tests are explicitly excluded, not called passed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PIN='62587ae0f581be8e8bc3bb01619f6e3b8efd1983'
EXTRA=r'''
    const std::string mma="mma.sync.aligned.m16n8k32.row.col.f32.e4m3.e4m3.f32 {%f0, %f1, %f2, %f3}, {%r0, %r1, %r2, %r3}, {%r4, %r5}, {%f4, %f5, %f6, %f7};";
    const auto ada_mma=LowerOriginalPtxForAda(Replace(source,"ret;",mma+"\nret;"));
    check(ada_mma.find(mma)!=std::string::npos,"Ada must preserve FP8 MMA; do not confuse it with SM86 lowering");
    for(int bytes:{512,1024,2048,8192}){
      const auto copy=std::format("mov.b32 %r3, {};\ncp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes [%r1], [%rd2], %r3, [%r4];\nret;",bytes);
      const auto result=LowerOriginalPtxForAda(Replace(source,"ret;",copy));
      std::size_t count=0,pos=0;const std::string operation="cp.async.ca.shared.global";
      while((pos=result.find(operation,pos))!=std::string::npos){++count;pos+=operation.size();}
      check(count==static_cast<std::size_t>(bytes/512),"Ada copy expansion no longer follows 32 lanes times 16 bytes");
      check(result.find("cp.async.wait_group 0;")!=std::string::npos,"copy completion wait disappeared");
      std::cout<<"synthetic-copy bytes="<<bytes<<" per-lane-instructions="<<count<<" completion-wait=1\n";
    }
    bool wrong_size=false;
    try{(void)LowerOriginalPtxForAda(Replace(source,"ret;","mov.b32 %r3, 513;\ncp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes [%r1], [%rd2], %r3, [%r4];\nret;"));}
    catch(const std::exception&){wrong_size=true;}
    check(wrong_size,"misaligned synthetic copy accepted");
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--opennr-root',required=True,type=Path)
    ap.add_argument('--compiler',required=True);ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--sanitize',action='store_true')
    a=ap.parse_args();root=a.opennr_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
    if commit!=PIN:raise RuntimeError('review the new OpenNR source before updating this test recipe')
    if subprocess.check_output(['git','-C',str(root),'status','--porcelain'],text=True).strip():
        raise RuntimeError('reference checkout must remain clean')
    files=['tests/native/text_tests.cpp','src/text.hpp','src/architectures.hpp','src/transforms.hpp']
    hashes={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files}
    text=(root/files[0]).read_text(encoding='utf-8')
    # Keep the author's original pure tests; exclude the exact Windows-only blocks.
    first=text[:text.index('    const auto temporary =')]
    second=text[text.index('    const std::string source ='):text.index('    std::string pe_image(')]
    first=first.replace('#include <windows.h>','').replace('#include "binary.hpp"','').replace('#include "io.hpp"','')
    first=first.replace('    const auto check = [](bool condition, const char* message) {',
                        '    std::size_t checks=0;\n    const auto check = [&](bool condition, const char* message) {\n      ++checks;')
    code=first+second+EXTRA+r'''
    std::cout<<"PASS upstream portable tests and Ada path contracts checks="<<checks<<"\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
'''
    test=out/'portable-opennr.cpp';test.write_text(code,encoding='utf-8');exe=out/'portable-opennr'
    command=[a.compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(test),'-o',str(exe)]
    if a.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    report={'passed':False,'upstream_commit':commit,'source_sha256':hashes,'command':command,
            'scope':'portable text/transforms plus synthetic Ada contracts',
            'windows_pe_hash_process_tests_run':False,'ptx_assembled':False,'dll_built':False,
            'gpu_verified':False,'performance_measured':False}
    def save():(out/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    run=subprocess.run(command,capture_output=True,text=True)
    (out/'compile.log').write_text(run.stdout+run.stderr,encoding='utf-8');report['compile_exit']=run.returncode;save()
    if run.returncode:raise RuntimeError(run.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    (out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');report['run_exit']=run.returncode
    report['source_unchanged']=all(hashlib.sha256((root/p).read_bytes()).hexdigest()==v for p,v in hashes.items())
    report['passed']=run.returncode==0 and report['source_unchanged'];save()
    if not report['passed']:raise RuntimeError(run.stdout+run.stderr)
    print(run.stdout.strip())

if __name__=='__main__':main()
