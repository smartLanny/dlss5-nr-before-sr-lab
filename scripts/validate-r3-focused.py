"""Windows CPU regression of current exact-first/advisory and indexed registry code."""
import argparse, hashlib, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    names=['src/nr_present_mapping.h','src/nr_late_present_receipt.hpp','src/nr_descriptor_provenance.h',
        'tests/present_mapping.cpp','tests/present_r3_compat.cpp','tests/descriptor_r3_registry.cpp',
        'tests/descriptor_batch.cpp','tests/r3_support/baseline_receipt_generated.hpp','tests/r3_support/r2_descriptor_generated.h']
    hashes={}
    for name in names:
        data=(ROOT/name).read_bytes();target=out/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        hashes[name]=hashlib.sha256(data).hexdigest()
    report={'scope':'Windows CPU policy/registry tests; no Win32 events, GPU, NGX or game claim','inputs':hashes,'runs':[]}
    cases=[('mapping','present_mapping.cpp',0),('compat','present_r3_compat.cpp',0),
        ('batch','descriptor_batch.cpp',0),('registry','descriptor_r3_registry.cpp',0),('registry-profile','descriptor_r3_registry.cpp',1)]
    for label,source,profile in cases:
        binary=out/(label+'.exe')
        cmd=[str(a.compiler),'-std=c++20','-O2','-UNDEBUG','-static','-Wl,--stack,33554432',
            '-DNR_DESCRIPTOR_PROFILE='+str(profile),'-I'+str(out/'src'),'-I'+str(out/'tests/r3_support'),str(out/'tests'/source),'-o',str(binary)]
        compiled=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        (out/(label+'-compile.log')).write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
        row={'name':label,'command':cmd,'compile_exit':compiled.returncode,'profile':profile};report['runs'].append(row)
        if compiled.returncode:break
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=120)
        (out/(label+'-run.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        row.update(exit=run.returncode,stdout=run.stdout);print(label+': '+run.stdout.strip(),flush=True)
        if run.returncode:break
    report['inputs_unchanged']=all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==v for n,v in hashes.items())
    report['passed']=report['inputs_unchanged'] and len(report['runs'])==len(cases) and all(r.get('exit')==0 for r in report['runs'])
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
