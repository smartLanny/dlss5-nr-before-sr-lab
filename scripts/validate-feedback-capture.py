"""Exercise production collector logic with fake ReShade/timing/packaging IO seams."""
from pathlib import Path
import argparse,base64,datetime,hashlib,json,os,subprocess,zipfile

FUNCTIONS=("void FeedbackAppend(","const char *FeedbackStageBlockReason(",
           "void FeedbackAppendNgxLifecycle(","void FeedbackAppendActivity(","bool RequestFeedbackBundle(",
           "void FeedbackStageExported(","void OnFeedbackScreenshot(",
           "void FeedbackObserveEvaluate(",
           "bool FeedbackCreateDirectory(",
           "DWORD WINAPI FeedbackWorker(","void FeedbackPresent(")

def extract_function(source, signature):
    start=source.index(signature)
    begin=source.index('{',start)
    # Lexical brace scan preserves strings/comments exactly without counting
    # their braces as C++ blocks. No source-to-test rewrite of behavior occurs.
    depth=0;i=begin;mode='code'
    while i<len(source):
        c=source[i];n=source[i:i+2]
        if mode=='line':
            if c=='\n':mode='code'
        elif mode=='block':
            if n=='*/':mode='code';i+=1
        elif mode in ('"',"'"):
            if c=='\\':i+=1
            elif c==mode:mode='code'
        elif n=='//':mode='line';i+=1
        elif n=='/*':mode='block';i+=1
        elif c in ('"',"'"):mode=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return source[start:i+1]
        i+=1
    raise ValueError('Unterminated function '+signature)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--toolchain-root',default=os.environ.get('DLSS5_LLVM_MINGW_ROOT'))
    p.add_argument('--output')
    p.add_argument('--old-capacity-negative',action='store_true',help='Test-only inverse change: restore both physical metadata buffers to 32KiB.')
    a=p.parse_args()
    if not a.toolchain_root:p.error('--toolchain-root is required')
    root=Path(__file__).resolve().parents[1]
    out=Path(a.output).resolve() if a.output else root/'test-output'/('feedback-capture-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True,exist_ok=False)
    source=(root/'src/nr_feedback_capture.inl').read_text(encoding='utf-8-sig')
    # The globals/struct are also production source, so changes to the session
    # layout and synchronization declarations invalidate the harness naturally.
    globals_=source[:source.index('void FeedbackAppend(')]
    bundle_path=root/'src/nr_diagnostic_bundle.inl'
    if a.old_capacity_negative:
        declaration='char report[nr_feedback_limits::kMetadataBytes]'
        if globals_.count(declaration)!=1:raise ValueError('Old-capacity collector anchor changed')
        globals_=globals_.replace(declaration,'char report[32768]')
        bundled=bundle_path.read_text(encoding='utf-8-sig')
        declaration='constexpr unsigned int kMetadataBytes = nr_feedback_limits::kMetadataBytes;'
        if bundled.count(declaration)!=1:raise ValueError('Old-capacity packager anchor changed')
        bundle_path=out/'diagnostic-bundle-old-capacity.inl'
        bundle_path.write_text(bundled.replace(declaration,'constexpr unsigned int kMetadataBytes = 32768;'),encoding='utf-8')
    functions='\n\n'.join(extract_function(source,f) +
        ('\n#include "nr_compatibility_verdict_feedback.inl"' if f == 'void FeedbackAppend(' else '')
        for f in FUNCTIONS)
    template=(root/'tests/feedback_capture_harness.cpp.in').read_text(encoding='utf-8')
    generated=template.replace('// @production_globals',globals_).replace('// @production_functions',functions)
    ngx_source=(root/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    generated=generated.replace('// @production_rr_guard',extract_function(ngx_source,'bool ShouldGuardFeature1ForRayReconstruction('))
    generated=generated.replace('@bundle_helper_path@',bundle_path.as_posix())
    cpp=out/'feedback-capture-harness.cpp';cpp.write_text(generated,encoding='utf-8')
    compiler=Path(a.toolchain_root)/'bin/x86_64-w64-mingw32-clang++.exe'
    exe=out/'feedback-capture-harness.exe'
    flags=['-DNR_FEEDBACK_OLD_CAPACITY_NEGATIVE'] if a.old_capacity_negative else []
    built=subprocess.run([str(compiler),'-std=c++20','-O2','-Wall','-Wextra','-Wpedantic',
        '-static','-municode','-I'+str(root/'src'),*flags,str(cpp),'-o',str(exe)],capture_output=True,text=True)
    (out/'build.log').write_text(built.stdout+built.stderr,encoding='utf-8')
    if built.returncode:
        print(built.stderr);raise SystemExit(built.returncode)
    fixtures=out/'fixtures';fixtures.mkdir()
    png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aXioAAAAASUVORK5CYII=')
    (fixtures/'normalization-source.png').write_bytes(png)
    ran=subprocess.run([str(exe),str(fixtures)],capture_output=True,text=True,timeout=15)
    (out/'native.log').write_text(ran.stdout+ran.stderr,encoding='utf-8')
    zip_verified=False
    expected_negative=a.old_capacity_negative and ran.returncode==42 and 'EXPECTED_OLD_FEEDBACK_CAPACITY_REJECTION' in ran.stdout
    expanded_metadata_verified=False
    if ran.returncode==0 and not a.old_capacity_negative:
        with zipfile.ZipFile(fixtures/'normalized-zip/feedback.zip') as z:
            assert z.testzip() is None
            assert z.read('screenshots/current-0.png')==png
            manifest=json.loads(z.read('manifest.json'))
            assert manifest['required_complete'] and manifest['complete']
            item=next(i for i in manifest['entries'] if i['name']=='screenshots/current-0.png')
            assert item['status']=='included' and item['sha256']==hashlib.sha256(png).hexdigest()
            zip_verified=True
        with zipfile.ZipFile(fixtures/'expanded-metadata/feedback.zip') as z:
            assert z.testzip() is None
            metadata=z.read('capture-info.txt').decode('utf-8-sig')
            assert len(metadata.encode('utf-8'))>96*1024
            assert 'compatibility.verdict=capacity-fixture-only' in metadata
            assert 'collection.metadataTruncated=0' in metadata and 'end.nrSuccess=' in metadata
            manifest=json.loads(z.read('manifest.json'))
            assert manifest['complete'] and manifest['required_complete'] and manifest['metadata_text']==metadata
            expanded_metadata_verified=True
    passed=expected_negative if a.old_capacity_negative else ran.returncode==0
    result={'status':'PASS' if passed else 'FAIL','normalizedScreenshotZipVerified':zip_verified,
        'expandedMetadataZipVerified':expanded_metadata_verified,'oldCapacityNegativeControl':expected_negative,
        'limitsSha256':hashlib.sha256((root/'src/nr_feedback_limits.h').read_bytes()).hexdigest(),
        'collectorSha256':hashlib.sha256(source.encode('utf-8')).hexdigest(),
        'functions':list(FUNCTIONS),'native':ran.stdout.strip(),
        'limitations':['No live game, GPU or ReShade screenshot exporter is executed.',
            'Actual production callback, request, Present and worker bodies are extracted unchanged.',
            'ReShade runtime, scheduling clock, NGX snapshots, module/log enumeration and ordinary packaging IO are seams.',
            'RequestCore is a scheduling seam for the retained collector; validate-core-feedback.py separately tests the current single-TXT dispatcher.',
            'Saved-path normalization uses real GetFullPathNameW and the actual ZIP helper; fallback directory failures use OS seams.',
            'Actual ZIP helper integrity is validated separately by validate-diagnostic-bundle.py.',
            'Most scheduling is deterministic; one native export thread checks request-publication locking. This does not exhaust all thread interleavings.']}
    (out/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2));print('Evidence: '+str(out))
    raise SystemExit(0 if passed else (ran.returncode or 1))

if __name__=='__main__':main()
