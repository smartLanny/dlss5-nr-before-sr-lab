import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
# CI can run selected negative controls against the historical production parser.
PARSER_PATH = Path(os.environ.get('NR_CORE_PARSER', ROOT / 'scripts/parse-core-feedback.py'))
spec = importlib.util.spec_from_file_location('core_parser', PARSER_PATH)
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
                f'artifact.sourceCommit={"a" * 40}\nartifact.addonSha256={"b" * 64}\n'
                'artifact.sourceDirty=0\nartifact.identityStatus=verified\n'
                'snapshot.valid=1\nrequested.config=42\ncollection.cancelled=0\ncollection.metadataTruncated=0\n'
                'config.writesToIni=0\nimages.requested=0\ngpu.readbackRequested=0\ncore.zipLimitBytes=8388608\n')
        if image:
            text += f'core.session={session}\ncore.config=42\ncore.complete=1\n'
        payloads = {} if image or missing else {'logs/nr-current-summary.txt': b'format=DLSS5-LogSummary-v1\nscan.complete=1\n[head]\nbuild=fixture\n'}
        if extra:
            payloads.update(extra)
        records = [{'name': n, 'kind': 'copy', 'status': 'included', 'required': True, 'bytes': len(b),
                    'sha256': '0' * 64 if bad_hash else hashlib.sha256(b).hexdigest()} for n, b in payloads.items()]
        if missing:
            records += [{'name': 'logs/nr-current-summary.txt', 'status': 'missing', 'required': True, 'reason': 'test absence'}]
        manifest = {'format': 'DLSS5-DiagnosticBundle-v1', 'metadata_entry': 'capture-info.txt',
                    'metadata_text': text + ('extra=1\n' if bad_meta else ''), 'complete': not missing,
                    'required_complete': not missing, 'input_rejections': 0, 'entries': records}
        path = self.root / name
        self.write(path, manifest, text, payloads)
        return path

    def write(self, path, manifest, text, payloads, compression=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(path, 'w', compression=compression) as archive:
            for name, data in payloads.items():
                archive.writestr(name, data)
            archive.writestr('capture-info.txt', text)
            archive.writestr('manifest.json', json.dumps(manifest))

    def rewrite(self, path, change):
        with zipfile.ZipFile(path) as archive:
            manifest = json.loads(archive.read('manifest.json'))
            info = parser.metadata(archive.read('capture-info.txt').decode())
            payloads = {n: archive.read(n) for n in archive.namelist() if n not in ('manifest.json', 'capture-info.txt')}
        change(manifest, info, payloads)
        text = ''.join(f'{k}={v}\n' for k, v in info.items())
        manifest['metadata_text'] = text
        self.write(path, manifest, text, payloads)
        return path

    def test_core_without_nr_frames(self):
        value = parser.inspect_core(self.bundle())
        self.assertTrue(value['manifest_verified'])
        self.assertEqual(value['session'], 'DLSS5_CORE_1_2_3')

    def test_expanded_bounded_metadata_remains_inspectable(self):
        # Memory plus retirement evidence can exceed the historical 192 KiB
        # parser gate. Exercise escaped JSON too, rather than a small fixture.
        path = self.rewrite(self.bundle(), lambda m, i, p: i.update({
            'fixture.large': '\t' * (600 * 1024)}))
        with zipfile.ZipFile(path) as archive:
            self.assertGreater(archive.getinfo('manifest.json').file_size, 512 * 1024)
            self.assertLessEqual(archive.getinfo('capture-info.txt').file_size,
                                 parser.CORE_METADATA_LIMIT)
        self.assertTrue(parser.inspect_core(path)['manifest_verified'])

    def test_metadata_and_manifest_limits_still_reject_overflow(self):
        path = self.rewrite(self.bundle(), lambda m, i, p: i.update({
            'fixture.large': 'x' * parser.CORE_METADATA_LIMIT}))
        with self.assertRaisesRegex(parser.FormatError, 'per-entry byte limit'):
            parser.inspect_core(path)
        path = self.bundle()
        with zipfile.ZipFile(path) as archive:
            payloads = {name: archive.read(name) for name in archive.namelist()}
        payloads['manifest.json'] = b' ' * (parser.META_LIMIT + 1)
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in payloads.items():
                archive.writestr(name, data)
        with self.assertRaisesRegex(parser.FormatError, 'per-entry byte limit'):
            parser.inspect_core(path)

    def test_missing_is_explicit_partial(self):
        value = parser.inspect_core(self.bundle(missing=True))
        self.assertFalse(value['required_complete'])
        self.assertEqual(value['missing'][0]['reason'], 'test absence')

    def test_image_pair(self):
        self.assertTrue(parser.inspect_pair(self.bundle(), self.bundle('image.zip', image=True))['pair_verified'])

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
        with path.open('ab') as out:
            out.write(b'x' * parser.LIMIT)
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_duplicate_names(self):
        path = self.bundle()
        with zipfile.ZipFile(path, 'a') as archive:
            archive.writestr('CAPTURE-INFO.TXT', b'duplicate case')
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_duplicate_json_keys(self):
        with self.assertRaises(parser.FormatError):
            parser.unique_json([('session', 1), ('session', 2)])

    def test_missing_required_log_declaration_rejected(self):
        def remove(manifest, info, payloads):
            payloads.clear()
            manifest['entries'] = []
        path = self.rewrite(self.bundle(), remove)
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_non_boolean_completeness_rejected(self):
        path = self.rewrite(self.bundle(), lambda m, i, p: m.update(complete='true'))
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_cancelled_marked_complete_rejected(self):
        path = self.rewrite(self.bundle(), lambda m, i, p: i.update({'collection.cancelled': '1'}))
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_same_source_different_binary_rejected(self):
        image = self.rewrite(self.bundle('image.zip', image=True),
                             lambda m, i, p: i.update({'artifact.addonSha256': 'c' * 64}))
        with self.assertRaises(parser.FormatError):
            parser.inspect_pair(self.bundle(), image)

    def test_missing_binary_identity_not_verified(self):
        image = self.rewrite(self.bundle('image.zip', image=True),
                             lambda m, i, p: i.pop('artifact.addonSha256'))
        core = self.rewrite(self.bundle(), lambda m, i, p: i.pop('artifact.addonSha256'))
        self.assertTrue(parser.inspect_core(core)['manifest_verified'])  # Identity failure must not destroy diagnostic evidence.
        with self.assertRaises(parser.FormatError):
            parser.inspect_pair(core, image)

    def test_invalid_manifest_boolean_fields(self):
        for key in ('complete', 'required_complete'):
            for value in (None, 'false', 0, 1):
                with self.subTest(key=key, value=value):
                    path = self.rewrite(self.bundle(), lambda m, i, p: m.update({key: value}))
                    with self.assertRaises(parser.FormatError):
                        parser.inspect_core(path)

    def test_missing_required_flag_rejected(self):
        for value in (None, False, 'true', 1):
            with self.subTest(value=value):
                path = self.rewrite(self.bundle(), lambda m, i, p: m['entries'][0].update(required=value))
                with self.assertRaises(parser.FormatError):
                    parser.inspect_core(path)

    def test_optional_missing_remains_valid_partial(self):
        def change(manifest, info, payloads):
            manifest['entries'].append({'name': 'logs/nr-previous-summary.txt', 'status': 'missing',
                                        'required': False, 'reason': 'no previous session'})
            manifest['complete'] = False
        value = parser.inspect_core(self.rewrite(self.bundle(), change))
        self.assertTrue(value['required_complete'])
        self.assertFalse(value['complete'])

    def test_optional_missing_cannot_claim_complete(self):
        def change(manifest, info, payloads):
            manifest['entries'].append({'name': 'logs/nr-previous-summary.txt', 'status': 'missing',
                                        'required': False, 'reason': 'no previous session'})
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(self.rewrite(self.bundle(), change))

    def test_snapshot_or_truncation_cannot_claim_required_complete(self):
        for key, value in (('snapshot.valid', '0'), ('collection.metadataTruncated', '1')):
            with self.subTest(key=key):
                path = self.rewrite(self.bundle(), lambda m, i, p: i.update({key: value}))
                with self.assertRaises(parser.FormatError):
                    parser.inspect_core(path)

    def test_cancelled_partial_stays_inspectable(self):
        def change(manifest, info, payloads):
            manifest.update(complete=False, required_complete=False)
            info['collection.cancelled'] = '1'
            manifest['entries'].append({'name': 'collection/cancelled', 'status': 'missing',
                                        'required': True, 'reason': 'cancelled by user'})
        value = parser.inspect_core(self.rewrite(self.bundle(), change))
        self.assertFalse(value['required_complete'])
        self.assertEqual(value['metadata']['collection.cancelled'], '1')

    def test_invalid_missing_reason_rejected(self):
        for reason in (True, {}, 42, '', ' '):
            with self.subTest(reason=reason):
                path = self.rewrite(self.bundle(missing=True), lambda m, i, p: m['entries'][0].update(reason=reason))
                with self.assertRaises(parser.FormatError):
                    parser.inspect_core(path)

    def test_unknown_source_identity_stays_core_only(self):
        for value in ('', 'unknown', 'fixture-not-a-commit'):
            with self.subTest(value=value):
                core = self.rewrite(self.bundle(), lambda m, i, p: i.update({'artifact.sourceCommit': value}))
                image = self.rewrite(self.bundle('image.zip', image=True), lambda m, i, p: i.update({'artifact.sourceCommit': value}))
                self.assertTrue(parser.inspect_core(core)['manifest_verified'])
                with self.assertRaises(parser.FormatError):
                    parser.inspect_pair(core, image)

    def test_pair_config_and_dirty_state_must_match(self):
        for key, value in (('core.config', '43'), ('artifact.sourceDirty', '1'), ('core.complete', '0')):
            with self.subTest(key=key):
                image = self.rewrite(self.bundle('image.zip', image=True), lambda m, i, p: i.update({key: value}))
                with self.assertRaises(parser.FormatError):
                    parser.inspect_pair(self.bundle(), image)

    def test_matching_partial_core_can_pair(self):
        core = self.bundle(missing=True)
        image = self.rewrite(self.bundle('image.zip', image=True), lambda m, i, p: i.update({'core.complete': '0'}))
        result = parser.inspect_pair(core, image)
        self.assertTrue(result['pair_verified'])
        self.assertFalse(result['core']['required_complete'])

    def test_summary_incomplete_scan_cannot_claim_required_complete(self):
        def change(manifest, info, payloads):
            name = 'logs/nr-current-summary.txt'
            data = payloads[name].replace(b'scan.complete=1', b'scan.complete=0')
            payloads[name] = data
            manifest['entries'][0].update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(self.rewrite(self.bundle(), change))

    def test_non_json_number_rejected(self):
        path = self.rewrite(self.bundle(), lambda m, i, p: m.update(unrecognized=float('nan')))
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_silently_normalized_member_rejected(self):
        original = self.bundle()
        with zipfile.ZipFile(original) as archive:
            payloads = {n: archive.read(n) for n in archive.namelist()}
        path = self.root / 'normalized.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            for name, data in payloads.items():
                archive.writestr(name + 'X' if name == 'capture-info.txt' else name, data)
        wire = path.read_bytes()
        self.assertEqual(wire.count(b'capture-info.txtX'), 2)
        path.write_bytes(wire.replace(b'capture-info.txtX', b'capture-info.txt\0'))
        with self.assertRaises(parser.FormatError):
            parser.inspect_core(path)

    def test_hotkey_runtime_wiring(self):
        source = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        core = (ROOT / 'src/nr_feedback_core.inl').read_text(encoding='utf-8-sig')
        report_key = source.index('nr_hotkeys::Key(shortcut_bindings,nr_hotkeys::Report)')
        report_edge = source.index('report_edge.Press(report_key,f8_state)', report_key)
        request = source.index('nr_f8::RequestCore();nr_f8::OpenWindow();', report_edge)
        present = source.index('nr_core_feedback::Present(runtime);', request)
        self.assertLess(report_key, report_edge)
        self.assertLess(report_edge, request)
        self.assertLess(request, present)
        self.assertNotIn('RequestStageCapture(', core)
        self.assertNotIn('->save_screenshot(', core)
        self.assertNotIn('ApplyConfig(', core)
        self.assertNotIn('WritePrivateProfileStringW(', core)
        self.assertIn('job->archive_limit = nr_feedback_text::kCoreZipLimit', core)
        self.assertNotIn('FeedbackAddModules(*job', core)


if __name__ == '__main__':
    unittest.main(verbosity=2)
