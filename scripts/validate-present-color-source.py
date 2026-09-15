"""Compile actual no-guide source admission and recording consumers on CPU."""
import argparse, hashlib, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def definition(text,marker,semicolon=False):
    start=text.index(marker);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end+int(semicolon)]
def main():
    p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--whole-write-negative',action='store_true',help='Require the obsolete exact-target-only promotion rule to fail the actual consumer tests.')
    a=p.parse_args()
    paths=[ROOT/'src/nr_source_frame_runtime.inl',ROOT/'src/nr_source_transport.inl',ROOT/'tests/present_color_source_host.cpp.in',ROOT/'src/nr_source_frame.hpp']
    captured=[p.read_bytes() for p in paths];runtime,transport,harness,header=[b.decode('utf-8-sig') for b in captured]
    harness=harness.replace('// @PRESENT_LEASE@',definition(runtime,'struct PresentLease',True))
    start=runtime.index('struct Producer {');end=runtime.index('// Streamline adapter publishes',start)
    harness=harness.replace('// @PRODUCER_REGISTRY@',runtime[start:end])
    start=transport.index('struct ReceiptStream');end=transport.index('std::atomic<bool> guides_awaiting_submission',start)
    harness=harness.replace('// @RECEIPT_STATE@',transport[start:end])
    functions=['ReceiptStream* ReceiptStreamFor(','DeviceOpportunity* ReceiptDeviceFor(',
        'void BeginNativeReceiptOpportunityLocked(','void CompleteNativeReceiptLocked(',
        'void MarkReceiptInlineProcessedLocked(','void MarkReceiptFgConsumedLocked(','void RetireReceiptStreamLocked(']
    harness=harness.replace('// @RECEIPT_FUNCTIONS@','\n'.join(definition(transport,m) for m in functions))
    harness=harness.replace('// @COLOR_FUNCTIONS@','\n'.join(definition(runtime if m in ['bool ApplicationPresentCurrent(','bool AcquireApplicationPresentColor('] else transport,m) for m in
        ['bool PresentColorOnlyCurrent(','bool AcquirePresentColorOnly(','bool ApplicationPresentCurrent(',
         'bool AcquireApplicationPresentColor(','bool BeginPresentReceiptWork(']))
    harness=harness.replace('// @PRESENT_DECISION@',definition(runtime,'PresentDecision PresentDecisionFor('))
    harness=harness.replace('// @CAN_PRESENT@',definition(runtime,'bool CanPresent('))
    harness=harness.replace('// @RELEASE_GUIDES@',definition(transport,'void ReleasePresentGuides('))
    harness=harness.replace('// @COMPLETE_PRESENT@',definition(runtime,'void CompletePresent('))
    harness=harness.replace('// @CANCEL_PRESENT@',definition(runtime,'void CancelPresentBeforeWork('))
    a.output.mkdir(parents=True,exist_ok=True);cpp=a.output/'host.cpp';exe=a.output/'host.exe';cpp.write_text(harness,encoding='utf-8')
    if a.whole_write_negative:
        expected='label.exact_write&&label.whole_write&&label.content'
        if header.count(expected)!=1:raise RuntimeError('whole-write negative rule no longer matches')
        header=header.replace(expected,'label.exact_write&&label.content')
    (a.output/'nr_source_frame.hpp').write_text(header,encoding='utf-8')
    # Unoptimized full bounded-registry reset fixtures need more than the
    # Windows default stack; production registries have static storage.
    command=[a.compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror','-static','-Wl,--stack,16777216','-I'+str(ROOT/'src'),str(cpp),'-o',str(exe)]
    build=subprocess.run(command,capture_output=True,text=True);(a.output/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:raise RuntimeError(build.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);(a.output/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    unchanged=all(p.read_bytes()==b for p,b in zip(paths,captured))
    negative_caught=a.whole_write_negative and run.returncode!=0 and 'target invalidation never counts as a full write' in run.stderr
    passed=(negative_caught if a.whole_write_negative else run.returncode==0) and unchanged
    (a.output/'validation.json').write_text(json.dumps({'passed':passed,'negative_control':a.whole_write_negative,'negative_caught':negative_caught,'host_exit':run.returncode,'scope':'actual CanPresent and BeginPresentReceiptWork CPU consumers; native guide selector and graphics interfaces are stubs; no GPU/NGX/game','command':command,'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(b).hexdigest() for p,b in zip(paths,captured)},'unchanged':unchanged},indent=2),encoding='utf-8')
    print(run.stdout,end='')
    if negative_caught:print('PASS: obsolete exact-target-only rule rejected by actual consumer test')
    if not passed:raise RuntimeError(run.stderr or f'host exit={run.returncode:#x}; source unchanged={unchanged}')
if __name__=='__main__':main()
