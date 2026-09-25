"""Fail-closed tests for the focused Core-only acceptance packager."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package050', ROOT / 'scripts/package-050-core-acceptance.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PackageGuards(unittest.TestCase):
    def test_independent_gpu_receipts_cannot_borrow_old_or_unrun_results(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'gpu.json'
            for kind,schema in [('native-fg','real-ngx-native-fg-host-v1'),('mfg','real-ngx-mfg-all-indexed-outputs-v1')]:
                value={'schema':schema,'result':0,'compile_only':False,'production_sources_unchanged':True,
                    'source_manifest_sha256':'a'*64,'core_source_sha256':'b'*64,'production_bridge_sha256':'c'*64,
                    'scope':'protocol fixture; no GPU validation','commands':[]}
                for change in ({},{'compile_only':True},{'result':1},{'production_sources_unchanged':False},
                        {'source_manifest_sha256':'d'*64},{'core_source_sha256':'d'*64},{'production_bridge_sha256':'d'*64}):
                    path.write_text(json.dumps(dict(value,**change)))
                    with self.subTest(kind=kind,change=change),self.assertRaises(ValueError):
                        m.checked_gpu_report(path,kind,'a'*64,'b'*64,'c'*64)

    def test_real_activation_identity_and_readbacks_required(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);path=directory/'activation.json'
            value={'schema':'real-reshade-no-native-dlss-activation-v1','result':0,'compile_only':False,
                'host_unchanged':True,'production_sources_unchanged':True,'source_manifest_sha256':'a'*64,
                'core_source_sha256':'b'*64,'production_bridge_sha256':'c'*64,'host_source_sha256':'d'*64,
                'reshade_sha256':'e'*64,'scope':'protocol fixture only; no GPU validation','commands':[],'outputs':{}}
            hosts={name:m.sha((ROOT/name).read_bytes()) for name in ('tests/compat_nodlss_ngx_host.cpp','tests/compat_present_ngx_host.cpp')}
            value['host_sources_at_compile']=hosts;value['host_sources_unchanged']=True
            value['host_source_sha256']=hosts['tests/compat_nodlss_ngx_host.cpp']
            for name in ('compile-no-dlss-host','run-no-dlss-1','run-no-dlss-4'):
                data=b'PASS NO_DLSS_ACTIVATION' if name.startswith('run') else b'compile ok'
                log=directory/(name+'.log');log.write_bytes(data)
                value['commands'].append({'name':name,'exit':0,'log':str(log),'sha256':m.sha(data)})
            for passes in (1,4):
                name=f'no-dlss-p{passes}.rgba';data=bytes(640*360*4);(directory/name).write_bytes(data)
                value['outputs'][name]={'bytes':len(data),'sha256':m.sha(data)}
            def check():return m.checked_activation(path,'a'*64,'b'*64,'c'*64,hosts['tests/compat_nodlss_ngx_host.cpp'])
            path.write_text(json.dumps(value));accepted=check()
            self.assertNotIn('commands',accepted);self.assertNotIn('outputs',accepted)
            changes=[{'result':1},{'compile_only':True},{'host_unchanged':False},
                {'production_sources_unchanged':False},{'source_manifest_sha256':'f'*64},
                {'core_source_sha256':'f'*64},{'production_bridge_sha256':'f'*64},
                {'host_source_sha256':'f'*64},{'commands':value['commands'][:2]},{'outputs':{}}]
            for change in changes:
                path.write_text(json.dumps(dict(value,**change)))
                with self.subTest(change=change),self.assertRaises(ValueError):check()
            path.write_text(json.dumps(value));(directory/'no-dlss-p4.rgba').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'Hash mismatch'):check()
            with self.assertRaisesRegex(ValueError,'evidence required'):m.checked_activation(None,'a'*64,'b'*64,'c'*64,'d'*64)

    def test_verified_file_and_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'good').write_bytes(b'original')
            digest = m.sha(b'original')
            self.assertEqual(m.checked_file(root, 'good', digest), b'original')
            (root / 'good').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Hash mismatch'):
                m.checked_file(root, 'good', digest)

    def test_path_escape_and_absolute_outside_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            root = parent / 'stage'
            root.mkdir()
            external = parent / 'outside'
            external.write_bytes(b'outside')
            for name in ('../outside', str(external)):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, 'Unsafe manifest'):
                        m.checked_file(root, name, m.sha(b'outside'))

    def test_preserve_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            evidence = root / 'existing'
            evidence.mkdir()
            marker = evidence / 'original'
            marker.write_bytes(b'keep')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                m.package(root / 'missing-build', evidence)
            self.assertEqual(marker.read_bytes(), b'keep')

    def fixture(self, build):
        stage = build / 'source'
        stage.mkdir()
        commit = 'a' * 40
        manifest = {'source_commit': commit, 'integration': {'source_commit': commit, 'version': m.VERSION}}
        raw = json.dumps(manifest).encode()
        (stage / 'source-manifest.json').write_bytes(raw)
        result = {'schema': 'nr050-focused-acceptance-v1', 'success': True,
                  'source_commit': commit, 'source_manifest_sha256': m.sha(raw),
                  'warp_verified': True, 'carrier_compiled': False,
                  'post_light_style_warp_verified': True,
                  'directional_light_style_warp_verified': True,
                  'directional_baseline_source_sha256': 'c681b208ef320b1b9fbd497e3c46485bf3836296a084cd4a741a2b179f1c8208',
                  'directional_baseline_manifest_sha256': '212d863167d4b55b5cc613528a2df68600243c73d07195c9136a6fe8b43c18c6',
                  'nvidia_feature18_verified': False, 'game_runtime_verified': False,
                  'five_percent_performance_gate': 'unmeasured', 'checks': []}
        return result

    def test_capability_promotion_and_missing_checks_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            original = self.fixture(build)
            changes = ({}, {'success': False}, {'warp_verified': False},
                       {'post_light_style_warp_verified': False}, {'directional_light_style_warp_verified': False},
                       {'directional_baseline_source_sha256': '0' * 64},
                       {'directional_baseline_manifest_sha256': '0' * 64},
                       {'carrier_compiled': True}, {'game_runtime_verified': True},
                       {'nvidia_feature18_verified': True}, {'five_percent_performance_gate': 'pass'},
                       {'source_commit': 'b' * 40})
            for change in changes:
                with self.subTest(change=change):
                    (build / 'validation.json').write_text(json.dumps(dict(original, **change)), encoding='utf-8')
                    with patch.object(m, 'git', side_effect=AssertionError('No Git operation before evidence admission')):
                        with self.assertRaisesRegex(ValueError, 'evidence required|gate missing'):
                            m.package(build, build / 'new-output')
                    self.assertFalse((build / 'new-output').exists())

    def test_development_snapshot_cannot_become_final_package(self):
        for flag in ('development_snapshot','source_dirty'):
            with self.subTest(flag=flag),tempfile.TemporaryDirectory() as tmp:
                build=Path(tmp);result=self.fixture(build)
                path=build/'source/source-manifest.json'
                manifest=json.loads(path.read_text());manifest[flag]=True
                raw=json.dumps(manifest).encode();path.write_bytes(raw)
                result['source_manifest_sha256']=m.sha(raw)
                (build/'validation.json').write_text(json.dumps(result))
                with patch.object(m,'git',side_effect=AssertionError('No Git before evidence admission')):
                    with self.assertRaisesRegex(ValueError,'Exact-source focused acceptance evidence required'):
                        m.package(build,build/'new-output')
                self.assertFalse((build/'new-output').exists())

if __name__ == '__main__':
    unittest.main(verbosity=2)
