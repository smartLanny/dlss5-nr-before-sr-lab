"""Independent CPU-only IEEE binary16 verification; never launches the host."""
import hashlib,json,math,pathlib,re,struct,sys
root=pathlib.Path(sys.argv[1]); target=root/'cpu-validation.json'
if target.exists():raise FileExistsError(target)
reports=[];source_hashes=set()
for case in ('320x184','320x176','256x144','160x88'):
    w,h=map(int,case.split('x')); process=json.loads((root/(case+'.process.json')).read_text(encoding='utf-8-sig'))
    assert process['exit_code']==0 and process['exited'] and not process['timeout']
    log=(root/(case+'.stdout.txt')).read_text();assert not (root/(case+'.stderr.txt')).read_bytes()
    planes={}
    for name,label in [('source-640x360','source'),('input','input'),('output','output'),('input-after','input_after')]:
        data=(root/case/(name+'.rgba16f')).read_bytes();n=640*360 if label=='source' else w*h
        assert len(data)==n*8
        digest=hashlib.sha256(data).hexdigest();receipt=re.search(r'^'+label+r' bytes=(\d+) sha256=([a-f0-9]+)$',log,re.M)
        assert receipt and int(receipt[1])==len(data) and receipt[2]==digest
        values=[v[0] for v in struct.iter_unpack('<e',data)];assert all(map(math.isfinite,values))
        alpha=values[3::4]
        planes[name]={'sha256':digest,'bytes':len(data),'finite':len(values),'nonfinite':0,'alpha_min':min(alpha),'alpha_max':max(alpha)}
    assert (root/case/'input.rgba16f').read_bytes()==(root/case/'input-after.rgba16f').read_bytes()
    source_hashes.add(planes['source-640x360']['sha256'])
    modules=[]
    for path in re.findall(r'^(?:factory|loaded)=(.+)$',log,re.M):
        p=pathlib.Path(path.strip());modules.append({'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    assert len(modules)==3
    reports.append({'case':case,'process':process,'planes':planes,'P0_unchanged':True,'loaded_modules':modules})
assert len(source_hashes)==1
for f in json.loads((root/'pre-run-lock.json').read_text(encoding='utf-8-sig')):
    assert hashlib.sha256(pathlib.Path(f['path']).read_bytes()).hexdigest().lower()==f['sha256'].lower()
result={'source_commit':'0fb7064f9569f9687c5152413aab205551f804a6','cases':reports,'same_source_all_cases':True,'prelock_unchanged':True,'gpu_rerun':False,'product_admission':False}
with target.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2)
print('PASS CPU-only: 4 cases, 16 raw planes; SHA receipts, IEEE half finite/alpha, P0 invariance, common source and module locks verified')
for r in reports:print(r['case'],r['planes']['output'])
