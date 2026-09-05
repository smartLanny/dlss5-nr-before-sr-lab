import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('core_parser', ROOT / 'scripts/parse-core-feedback.py')
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)

class CoreFeedbackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def bundle(self, name='core.zip', session='DLSS5_CORE_1_2_3', image=False, extra=None, bad_hash=False,
               bad_meta=False, kind=None, missing=False):
        text = (f'format={"DLSS5-F8-v4" if image else "DLSS5-Core-v1"}\n'
                f'capture.kind={kind or ("image-supplement" if image else "core")}\n'
                f'session={"1" if image else session}\n'
                'artifact.sourceCommit=fixture-sha\nsnapshot.valid=1\nconfig.writesToIni=0\n'
                'images.requested=0\ngpu.readbackRequested=0\ncore.zipLimitBytes=8388608\n')
        if image:
            text += f'core.session={session}\ncore.complete=1\n'
        payloads = {} if image or missing else {'logs/nr-current-summary.txt': b'format=DLSS5-LogSummary-v1\nscan.complete=1\n[head]\nbuild=fixture\n'}
        if extra:
            payloads.update(extra)
        records = [{'name': n, 'kind': 'copy', 'status': 'included', 'required': True, 'bytes': len(b),
                    'sha256': '0' * 64 if bad_hash else hashlib.sha256(b).hexdigest()} for n, b in payloads.items()]
        if missing:
            records += [{'name': 'logs/nr-current-summary.txt', 'status': 'missing', 'required': True, 'reason': 'test absence'}]
        manifest = {'format': 'DLSS5-DiagnosticBundle-v1', 'metadata_entry': 'capture-info.txt',
                    'metadata_text': text + ('extra=1\n' if bad_meta else ''), 'complete': not missing,
                    'required_complete': not missing, 'entries': records}
        path = self.root / name
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for n, b in payloads.items():
                z.writestr(n, b)
            z.writestr('capture-info.txt', text)
            z.writestr('manifest.json', json.dumps(manifest))
        return path

    def test_core_without_nr_frames(self):
        value = parser.inspect_core(self.bundle())
        self.assertTrue(value['manifest_verified'])
        self.assertEqual(value['session'], 'DLSS5_CORE_1_2_3')

    def test_missing_is_explicit_partial(self):
        value = parser.inspect_core(self.bundle(missing=True))
        self.assertFalse(value['required_complete'])
        self.assertEqual(value['missing'][0]['reason'], 'test absence')

    def test_image_pair(self):
        result = parser.inspect_pair(self.bundle(), self.bundle('image.zip', image=True))
        self.assertTrue(result['pair_verified'])

    def test_wrong_session_rejected(self):
        with self.assertRaises(parser.FormatError):
            parser.inspect_pair(self.bundle(), self.bundle('wrong.zip', image=True, session='DLSS5_CORE_9_9_9'))

    def test_legacy_image_not_claimed_paired(self):
        with self.assertRaises(parser.FormatError):
            parser.inspect_pair(self.bundle(), self.bundle('legacy.zip', image=True, kind='legacy'))

    def test_hash_mismatch(self):
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(self.bundle(bad_hash=True))

    def test_metadata_mismatch(self):
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(self.bundle(bad_meta=True))

    def test_core_forbids_images_and_raw_ini(self):
        for name in ['display.png', 'stages/raw.f8c', 'settings/ReShade.ini', 'logs/full.log']:
            with self.subTest(name=name), self.assertRaises(parser.FormatError):
                parser.inspect_core(self.bundle(extra={name: b'private data'}))

    def test_path_traversal(self):
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(self.bundle(extra={'../secret.txt': b'bad'}))

    def test_expanded_budget(self):
        path = self.bundle(extra={'logs/reshade-game-summary.txt': b'x' * (parser.LIMIT + 1)})
        self.assertLess(path.stat().st_size, parser.LIMIT)
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_physical_budget(self):
        path = self.bundle()
        with path.open('ab') as f:
            f.write(b'x' * parser.LIMIT)
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_duplicate_names(self):
        path = self.bundle()
        with zipfile.ZipFile(path, 'a') as z:
            z.writestr('CAPTURE-INFO.TXT', b'duplicate case')
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_duplicate_json_keys(self):
        with self.assertRaises(parser.FormatError):
            parser.unique_json([('session', 1), ('session', 2)])

    def test_hotkey_runtime_wiring(self):
        source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        core = (ROOT / 'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
        self.assertIn('nr_f8::RequestCore();', source)
        self.assertIn('nr_core_feedback::Present(runtime);', source)
        self.assertIn('nr_feedback_text::CoreHotkey(key)', source)
        self.assertNotIn('RequestStageCapture(', core)
        self.assertNotIn('->save_screenshot(', core)
        self.assertNotIn('ApplyConfig(', core)
        self.assertNotIn('WritePrivateProfileStringW(', core)
        self.assertIn('job->archive_limit = nr_feedback_text::kCoreZipLimit', core)
        self.assertNotIn('FeedbackAddModules(*job', core)  # Do not hash every installed runtime for F8.

if __name__ == '__main__':
    unittest.main(verbosity=2)
