"""OTA payload policy and actual Manager consumer compatibility (no game writes).
Optional consumer test:
  python tests/test_core_ota_package.py --manager-ota PATH/ota.js --node-modules PATH/node_modules --output DIR
The consumer and its local dependencies must be an unchanged Manager checkout or source snapshot.
"""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
spec = importlib.util.spec_from_file_location('core_ota', ROOT/'scripts/package-core-ota.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def fixture():
    # Deliberately inert bytes: proves ZIP consumer contracts, not PE or production identity.
    return ota.make_payload('test-core.addon64', b'format fixture only', b'bridge format fixture', '1.2.3-beta.4', 'a'*40, 'zh-CN', b'change', b'license', '1.2.3.4')


class Policy(unittest.TestCase):
    def test_no_runtime_config_or_face(self):
        payload = fixture()
        self.assertEqual(set(payload), {'test-core.addon64','nrchain_nvngx.dll','Instructions.txt','CHANGELOG.md','LICENSES.txt','ota-manifest.json'})
        m = json.loads(payload['ota-manifest.json'])
        self.assertFalse(m['includesDx11'])
        for row in m['files']:
            self.assertEqual(row['sha256'], ota.digest(payload[row['name']]))
            self.assertEqual(row['bytes'], len(payload[row['name']]))

    def test_bad_payloads(self):
        for name in ('../x','C:x','a/b','a\\b','..'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                ota.leaf(name)
        with tempfile.TemporaryDirectory() as d:
            for mutation in ('missing','duplicate'):
                p = fixture()
                if mutation == 'missing':
                    del p['nrchain_nvngx.dll']
                else:
                    p['second.addon64'] = b'x'
                with self.assertRaises(ValueError):
                    ota.write_zip(Path(d)/'invalid.zip', p)

    def test_dirty_or_wrong_checkout_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            candidate = Path(d)/'candidate.json'
            candidate.write_text(json.dumps(dict(candidate='1.2.3-beta.4',source_commit='a'*40)),encoding='utf-8')
            args = SimpleNamespace(candidate=candidate,checkout=Path(d))
            for outputs, reason in ((['b'*40+'\n'], 'Checkout/source mismatch'), (['a'*40+'\n','?? untracked\n'],'Dirty checkout')):
                with mock.patch.object(ota.subprocess,'check_output',side_effect=outputs), self.assertRaisesRegex(ValueError,reason):
                    ota.package(args)

    def test_failed_canonical_gate_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            candidate, canonical = Path(d)/'candidate.json', Path(d)/'validation.json'
            candidate.write_text(json.dumps(dict(candidate='1.2.3-beta.4',source_commit='a'*40,canonical_validation=str(canonical))),encoding='utf-8')
            canonical.write_text(json.dumps(dict(candidate='1.2.3-beta.4',source_commit='a'*40,source_verified=True,compile_verified=False,warp_verified=True)),encoding='utf-8')
            args = SimpleNamespace(candidate=candidate,checkout=Path(d))
            with mock.patch.object(ota.subprocess,'check_output',side_effect=['a'*40+'\n','']), self.assertRaisesRegex(ValueError,'Canonical gate missing: compile_verified'):
                ota.package(args)

    def test_postbuild_failures_reject_actual_package_path(self):
        # Only the git subprocess is mocked. Real receipt files flow through package().
        with tempfile.TemporaryDirectory() as d:
            base = Path(d)
            candidate, canonical = base/'candidate.json', base/'validation.json'
            source = 'a'*40
            candidate.write_text(json.dumps(dict(candidate='1.2.3-beta.4',source_commit=source,canonical_validation=str(canonical))),encoding='utf-8')
            canonical.write_text(json.dumps(dict(candidate='1.2.3-beta.4',source_commit=source,source_verified=True,compile_verified=True,warp_verified=True)),encoding='utf-8')
            generated = base/'source/core/src/nr_before_sr.cpp'
            generated.parent.mkdir(parents=True)
            generated.write_bytes(b'frozen generated source')
            valid = dict(passed=True,source_sha256=ota.digest(generated.read_bytes()),source_commit=source)
            names = ('unified8-checks','reconstruction-checks')
            for name in names:
                (base/name).mkdir()
            def reset():
                for name in names:
                    (base/name/'validation.json').write_text(json.dumps(valid),encoding='utf-8')
            reset()
            self.assertEqual(set(ota.postbuild_checks(base,source)),set(names))
            args = SimpleNamespace(candidate=candidate,checkout=base)
            for name in names:
                for field, value, error in (('passed',False,'Postbuild gate failed'),('source_sha256','0'*64,'Postbuild source hash mismatch'),('source_commit','b'*40,'Postbuild source commit mismatch')):
                    with self.subTest(gate=name,field=field):
                        reset()
                        bad = dict(valid,**{field:value})
                        (base/name/'validation.json').write_text(json.dumps(bad),encoding='utf-8')
                        with mock.patch.object(ota.subprocess,'check_output',side_effect=[source+'\n','']), self.assertRaisesRegex(ValueError,error+': '+name):
                            ota.package(args)
                reset()
                (base/name/'validation.json').unlink()
                with self.subTest(gate=name,field='missing'), mock.patch.object(ota.subprocess,'check_output',side_effect=[source+'\n','']), self.assertRaises(FileNotFoundError):
                    ota.package(args)

    def test_invalid_pe(self):
        with self.assertRaises(ValueError):
            ota.pe_check(b'not a PE')


def consumer_checks(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    valid = fixture()
    cases = {'valid': valid}
    wrong = copy.deepcopy(valid)
    wrong['test-core.addon64'] = b'X'+wrong['test-core.addon64'][1:]
    cases['tampered'] = wrong
    duplicate = copy.deepcopy(valid)
    duplicate['second.addon64'] = b'extra'
    m = json.loads(duplicate['ota-manifest.json'])
    m['files'].append(dict(name='second.addon64', bytes=5, sha256=ota.digest(b'extra')))
    duplicate['ota-manifest.json'] = json.dumps(m).encode()
    cases['multi-core'] = duplicate
    traversal = copy.deepcopy(valid)
    traversal['../escape.txt'] = b'no'
    m = json.loads(traversal['ota-manifest.json'])
    m['files'].append(dict(name='../escape.txt',bytes=2,sha256=ota.digest(b'no')))
    traversal['ota-manifest.json'] = json.dumps(m).encode()
    cases['traversal'] = traversal
    env = dict(os.environ, NODE_PATH=str(args.node_modules.resolve()))
    js = "const r=require(process.argv[1]);r.readOtaPackage(process.argv[2]).then(v=>console.log(JSON.stringify({accepted:true,version:v.manifest.version}))).catch(e=>console.log(JSON.stringify({accepted:false,error:e.message})));"
    rows = []
    for label, payload in cases.items():
        path = out/(label+'.zip')
        with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED) as z:
            for name, data in payload.items():
                z.writestr(name,data)
        run = subprocess.run(['node','-e',js,str(args.manager_ota.resolve()),str(path)],env=env,text=True,capture_output=True)
        (out/(label+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        if run.returncode:
            raise RuntimeError('Manager consumer failed to run: '+run.stderr)
        result = json.loads(run.stdout)
        ota.require(result['accepted'] == (label == 'valid'), 'Unexpected consumer result: '+label)
        rows.append(dict(case=label,archive_sha256=ota.digest(path.read_bytes()),**result))
    deps = [args.manager_ota, args.manager_ota.parent/'streamed-file-digest.js',args.manager_ota.parent/'launch-safety.js']
    result = dict(status='passed',scope='Actual Manager ZIP parser with inert fixture bytes; not production PE/source or Manager UI validation',consumer_sources={str(p.resolve()):ota.digest(p.read_bytes()) for p in deps},cases=rows)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manager-ota',type=Path)
    parser.add_argument('--node-modules',type=Path)
    parser.add_argument('--output',type=Path)
    args, remaining = parser.parse_known_args()
    if args.manager_ota:
        parser.error('--node-modules and --output required') if not args.node_modules or not args.output else None
        consumer_checks(args)
    else:
        unittest.main(argv=[sys.argv[0]]+remaining)

