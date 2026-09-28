"""Run isolated native RR/ordinary NR/private Feature1 controls from one verified host.

This is synthetic real NGX evidence, never game acceptance. Temporal production
shader sequences and actual Core temporal routes have separate receipts.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, time
from private_sr_cost_report import parse_metrics, summarize_cost, compare_cost
from temporal_cost_report import summarize_temporal_cost
ROOT = Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiled', type=Path, required=True, help='Successful validate-private-sr-product-real output, built with all three runtimes')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--frames', type=int, default=32, help='Final Z outputs; default leaves at least 16 stable samples after warmup and diagnostic exclusion')
    p.add_argument('--case', help='Run one named case for a focused repair')
    a=p.parse_args()
    if not 2 <= a.frames <= 256: p.error('--frames must be 2..256')
    compiled=a.compiled.resolve(strict=True); receipt_path=compiled/'validation.json'
    receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if receipt.get('schema')!='actual-product-private-sr-v2' or receipt.get('result')!='passed':
        raise ValueError('Require a successful v2 product-host compile receipt')
    exe=compiled/'product-real.exe'
    if sha(exe)!=receipt.get('binary_sha256'): raise ValueError('Product host binary identity mismatch')
    if sha(ROOT/'tests/private_sr_product_real.cpp')!=receipt.get('fixture_sha256'):
        raise ValueError('Product host belongs to a different fixture revision')
    source=Path(receipt['generated_source']); manifest=Path(receipt['manifest_path'])
    if sha(source)!=receipt['generated_source_sha256'] or sha(manifest)!=receipt['manifest_sha256']:
        raise ValueError('Generated Core source/manifest identity mismatch')
    generated=json.loads(manifest.read_text(encoding='utf-8-sig'))
    for rel,digest in generated['files'].items():
        path=(manifest.parent/rel).resolve(strict=True)
        if not path.is_relative_to(manifest.parent.resolve()) or sha(path)!=digest:
            raise ValueError('Generated source closure mismatch: '+rel)
    runtimes=receipt['runtime_sha256']
    for name in ('nrchain_nvngx.dll','nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx_dlssd.dll'):
        if name not in runtimes: raise ValueError('Missing pinned matrix runtime: '+name)
    for name,digest in runtimes.items():
        if Path(name).name!=name or sha(compiled/name)!=digest: raise ValueError('Runtime identity mismatch: '+name)
    cases=[dict(name='rr-only',native_rr=True,layers=0,percent=50,private=False,temporal='off'),
           dict(name='rr-zero-edit-private',native_rr=True,layers=0,percent=50,private=True,temporal='off')]
    for percent in (50,25):
        for layers in (1,3,5):
            for private in (False,True):
                label='private' if private else 'spatial'
                cases.append(dict(name=f'rr-{label}-layers{layers}-p{percent}',native_rr=True,layers=layers,percent=percent,private=private,temporal='off'))
    for private in (False,True):
        label='private' if private else 'spatial'
        cases.append(dict(name=f'sr-control-{label}',native_rr=False,layers=1,percent=50,private=private,temporal='off'))
    for layers in (1,3,5):
        for temporal in ('amd-of','amd-of-low'):
            cases.append(dict(name=f'rr-{temporal}-layers{layers}-p50',native_rr=True,layers=layers,percent=50,private=False,temporal=temporal))
    cases.append(dict(name='rr-native-mv-refusal-layers1-p50',native_rr=True,layers=1,percent=50,private=False,temporal='native-mv-refusal'))
    if a.case:
        cases=[case for case in cases if case['name']==a.case]
        if not cases: p.error('Unknown matrix case')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report=dict(schema='post-native-rr-matrix-v1',result='running',game=False,
        scope='Synthetic actual native Feature13/Feature1 and Feature18; private Feature1 independently disabled/enabled. Not private Feature13, not cross-native-RR transport, not game quality acceptance.',
        source_commit=receipt['source_commit'],compiled_receipt=str(receipt_path),compiled_receipt_sha256=sha(receipt_path),
        binary_sha256=sha(exe),runtime_sha256=runtimes,frames=a.frames,cases=[],
        temporal_scope='Actual generated Core PostRr AMDOF whole-chain adaptive/low-frequency, same native RR and real Feature18 source/list/fence. NativeMotion rejected by product route contract. Later-layer remains untested because this host lacks decoded P1 and production binding/lifetime proof.',
        coverage={'executed_supported_cases_passed':False,'user_matrix_complete':False,
                  'post_rr_amdoF_later_layers':{'status':'not-tested-harness-missing-P1','reason':'real Feature18 layer-one neural exists, but this host has no production decoded P1, slot.dual_selected descriptor packet, or exact lifetime binding'},
                  'post_rr_native_mv':{'status':'unsupported-rejected','reason':'product PostRr route contract requires AMD optical flow; native RR MV lacks proven motion input'}},
        limitations=['synthetic scene and real runtimes, not game quality acceptance',
                     'later-layer PostRr temporal quality remains pending; top-level pass covers executed cases only'])
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save();failed=False
    for case in cases:
        folder=out/case['name'];folder.mkdir()
        shutil.copy2(exe,folder/exe.name)
        for name in runtimes: shutil.copy2(compiled/name,folder/name)
        command=[str(folder/exe.name),str(folder/'nrchain_nvngx.dll'),str(case['layers']),str(case['percent']),
                 str(int(case['native_rr'])),str(a.frames),'0',str(int(case['private'])),
                 str({'off':0,'amd-of':1,'amd-of-low':2,'native-mv-refusal':3}[case['temporal']])]
        row=dict(case,command=command,result='running',admission='unsupported-rejected' if case['temporal']=='native-mv-refusal' else 'supported');report['cases'].append(row);save();started=time.monotonic()
        try:
            r=subprocess.run(command,cwd=folder,capture_output=True,timeout=240)
            (folder/'run.log').write_bytes(r.stdout+r.stderr)
            row.update(exit_code=r.returncode,seconds=time.monotonic()-started,result='passed' if r.returncode==0 else 'failed')
            if r.returncode: failed=True
            else:
                capture=folder/'actual-core-same-frame.json'
                data=json.loads(capture.read_text());row['capture']=data
                row['capture_manifest_sha256']=sha(capture)
                expected=6 if case['private'] else 4 if case['temporal'] in ('amd-of','amd-of-low') else 3
                if len(data['planes'])!=expected or data['native_feature']!=(13 if case['native_rr'] else 1) or data['private_sr']!=case['private'] or data['temporal_case']!={'off':0,'amd-of':1,'amd-of-low':2,'native-mv-refusal':3}[case['temporal']]:
                    raise ValueError('Same-frame evidence does not match requested control')
                row['captures_sha256']={plane['file']:sha(folder/plane['file']) for plane in data['planes']}
                log=(r.stdout+r.stderr).decode('utf-8','replace')
                row['metrics']=parse_metrics(log)
                row['cost']=summarize_cost(row['metrics'],a.frames,case['layers'],case['private'])
                row['temporal_cost']=summarize_temporal_cost(row['metrics'],a.frames,case['temporal'])
        except subprocess.TimeoutExpired as exc:
            (folder/'run.log').write_bytes((exc.stdout or b'')+(exc.stderr or b''))
            row.update(result='failed',error='bounded host timeout',seconds=time.monotonic()-started);failed=True
        except Exception as exc: row.update(result='failed',error=str(exc));failed=True
        save();print(case['name']+': '+row['result'],flush=True)
        if failed: break  # Preserve the first failure; no blind GPU retries.
    by_case={(row['native_rr'],row['layers'],row['percent'],row['private']):row for row in report['cases'] if row['temporal']=='off'}
    report['cost_comparisons']=[]
    for case in cases:
        if case['private'] or case['layers']==0 or case['temporal']!='off': continue
        key=(case['native_rr'],case['layers'],case['percent'])
        ordinary=by_case.get((*key,False));private=by_case.get((*key,True))
        if ordinary and private and ordinary.get('result')=='passed' and private.get('result')=='passed':
            identity=('source_stream','source_epoch','source_sequence')
            same_source=all(ordinary['capture'][k]==private['capture'][k] for k in identity)
            same_bz=all(ordinary['captures_sha256'][p]==private['captures_sha256'][p]
                        for p in ('same-frame-B.fp16','same-frame-Z.fp16'))
            comparison=(compare_cost(ordinary['cost'],private['cost']) if same_source and same_bz
                        else {'status':'non_same_input','reason':'final same-frame native B or real Feature18 Z bytes/source identity differ; no causal pixel or paired-cost comparison'})
            comparison['same_source_identity']=same_source;comparison['same_raw_bz_sha256']=same_bz
        else: comparison={'status':'insufficient','reason':'matched ordinary/private controls did not both complete'}
        report['cost_comparisons'].append({'native_feature':13 if case['native_rr'] else 1,
            'layers':case['layers'],'percent':case['percent'],'frames':a.frames,
            'ordinary_case':case['name'],'private_case':next((c['name'] for c in cases if c['native_rr']==case['native_rr'] and c['layers']==case['layers'] and c['percent']==case['percent'] and c['private']),None),
            **comparison})
    report['result']='failed' if failed else 'passed';report['requested_cases']=len(cases)
    report['coverage']['executed_supported_cases_passed']=not failed and all(
        row['result']=='passed' for row in report['cases'] if row['admission']=='supported')
    save();return int(failed)
if __name__=='__main__': raise SystemExit(main())
