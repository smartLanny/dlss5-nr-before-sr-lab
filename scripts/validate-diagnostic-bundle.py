"""Compile actual Windows helper; independently inspect ZIP/CRC/SHA with stdlib."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess,zipfile,zlib

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--toolchain-root',default=os.environ.get('DLSS5_LLVM_MINGW_ROOT'))
    p.add_argument('--output')
    args=p.parse_args()
    if not args.toolchain_root:p.error('--toolchain-root is required')
    root=Path(__file__).resolve().parents[1]
    out=Path(args.output).resolve() if args.output else root/'test-output'/('diagnostic-bundle-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True,exist_ok=False)
    compiler=Path(args.toolchain_root)/'bin/x86_64-w64-mingw32-clang++.exe'
    exe=out/'diagnostic-bundle-harness.exe'
    command=[str(compiler),'-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-finput-charset=UTF-8',
        '-static','-municode',str(root/'tests/diagnostic_bundle_harness.cpp'),'-o',str(exe),'-lkernel32']
    built=subprocess.run(command,capture_output=True,text=True,check=True)
    (out/'build.log').write_text(built.stdout+built.stderr,encoding='utf-8')
    fixture=out/'fixtures';fixture.mkdir()
    ran=subprocess.run([str(exe),str(fixture)],capture_output=True,text=True,check=True,timeout=90)
    (out/'native.log').write_text(ran.stdout+ran.stderr,encoding='utf-8')
    reports={}
    for case in ('complete','partial','names','cancelled','entry-limit','metadata-large','metadata-limit'):
        path=fixture/case/'feedback.zip'
        with zipfile.ZipFile(path) as z:
            assert z.testzip() is None
            infos=z.infolist();assert len({v.filename for v in infos})==len(infos)
            assert all(v.compress_type==zipfile.ZIP_STORED and v.flag_bits&0x800 for v in infos)
            manifest=json.loads(z.read('manifest.json'))
            assert z.read('capture-info.txt').decode('utf-8')==manifest['metadata_text']
            for item in manifest['entries']:
                if item['status'] in ('included','included-truncated'):
                    data=z.read(item['name'])
                    assert len(data)==item['bytes']
                    assert hashlib.sha256(data).hexdigest()==item['sha256']
                    assert zlib.crc32(data)==item['crc32']
                else:assert item['name'] not in z.namelist()
            reports[case]=manifest
            if case=='complete':
                assert manifest['complete'] and manifest['required_complete']
                expected=(fixture/'source.bin').read_bytes()
                assert z.read('stage/original.pfm')==expected==z.read('截图/人物.bin')
                module=next(x for x in manifest['entries'] if x['kind']=='module-hash-only')
                assert module['sha256']==hashlib.sha256(expected).hexdigest()
                assert module['status']=='hashed' and module['name'] not in z.namelist()
            elif case=='partial':
                assert not manifest['complete'] and not manifest['required_complete']
                assert z.read('logs/live.log')==(fixture/'active.log').read_bytes()[-13:]
                reasons={x['reason'] for x in manifest['entries']}
                assert {'source-open-failed','file-size-limit','stage-timeout-with-NR-bypassed'}<=reasons
            elif case=='names':
                assert manifest['input_rejections']>=13
                assert len(z.namelist())==3 # Safe.txt and two automatic metadata files
            elif case=='cancelled':
                assert manifest['entries'][0]['reason']=='request-cancelled'
                assert z.namelist()==['capture-info.txt','manifest.json']
            elif case=='entry-limit':
                assert manifest['input_rejections']==1 and len(infos)==66
            elif case in ('metadata-large','metadata-limit'):
                raw=z.read('capture-info.txt')
                metadata=raw.decode('utf-8')
                assert manifest['complete'] and manifest['required_complete']
                assert manifest['metadata_text']==metadata
                if case=='metadata-large':
                    assert len(raw)>96*1024 and 'metadata.requiredTail=complete' in metadata
                else:
                    assert len(raw)==128*1024-1 and metadata.endswith('中文')
        assert not (path.parent/'feedback.zip.partial').exists()
    assert (fixture/'existing-partial/feedback.zip.partial').read_bytes()==b'owner-sentinel'
    assert not (fixture/'existing-partial/feedback.zip').exists()
    assert not (fixture/'busy/feedback.zip').exists()
    summary={'status':'PASS','native':ran.stdout.strip(),'zipCases':list(reports),
        'checks':['actual native helper','published SHA256 vectors','independent ZIP CRC/SHA/payload verification',
                  'UTF8 Chinese entry names and metadata','96KiB and exact 128KiB-minus-NUL metadata archives',
                  'oversized metadata and incomplete UTF8 rejection','module hash only, DLL not bundled','active writer log tail',
                  'missing and oversize reasons','duplicate/traversal/reserved/ADS/network rejection',
                  'cancellation with valid manifest','single worker guard','no final/partial overwrite','64-item cap'],
        'limitations':['No GPU or game execution; collection worker is caller-owned.',
                       'The time budget is checked between bounded local file reads; an OS disk call itself can stall.']}
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=True,indent=2))
    print('Evidence: '+str(out))

if __name__=='__main__':main()
