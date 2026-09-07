"""Exercise actual generator configuration with explicit Git/source-library seams.
This is not full source staging, Windows compilation, or a game test.
"""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('picture_integration', ROOT/'scripts/prepare-multi-api-beta.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PictureIntegration(unittest.TestCase):
    def library(self):
        return SimpleNamespace(patch_core=lambda text: 'bridge:' + text,
            prepare=lambda core, upstream, stage: None,
            legacy=SimpleNamespace(CORE_BLOBS={'unrelated': 'preserved'}, patch_ui=lambda text: 'ui:' + text))

    def fake_git(self, root, *args):
        self.assertEqual(args[0], 'rev-parse')
        return m.INTEGRATION_CORE_PINS[args[1].removeprefix('HEAD:')].encode()

    def test_all_three_ancestors_required(self):
        self.assertEqual(m.REQUIRED_ANCESTORS, (m.PR131, m.PR132, m.PR134, m.PR137))
        for missing in m.REQUIRED_ANCESTORS:
            with self.subTest(missing=missing), patch.object(m, 'is_ancestor', side_effect=lambda r, c: c != missing), patch.object(m, 'load') as load:
                with self.assertRaisesRegex(ValueError, 'Missing integration ancestor'):
                    m.configure(ROOT)
                load.assert_not_called()

    def test_actual_config_replaces_old_pin_and_keeps_base(self):
        library = self.library()
        library.legacy.CORE_BLOBS['src/nr_settings_ui.inl'] = 'old-ui'
        fmt = SimpleNamespace(patch_core=lambda text: 'format:' + text)
        post = SimpleNamespace(patch_core=lambda text: 'post:' + text, patch_ui=lambda text: 'post-ui:' + text)
        with patch.object(m, 'is_ancestor', return_value=True), patch.object(m, 'git', side_effect=self.fake_git), patch.object(m, 'load', side_effect=[library, fmt, post]):
            result = m.configure(ROOT)
        for path, digest in m.INTEGRATION_CORE_PINS.items():
            self.assertEqual(result.legacy.CORE_BLOBS[path], digest)
        self.assertEqual(result.legacy.CORE_BLOBS['unrelated'], 'preserved')
        self.assertEqual(result.legacy.patch_core('core'), 'post:format:bridge:core')
        self.assertEqual(result.legacy.patch_ui('panel'), 'post-ui:ui:panel')
        self.assertEqual(result.legacy.UPSTREAM, m.UPSTREAM)

    def test_each_changed_dependency_requires_review(self):
        for changed in m.INTEGRATION_CORE_PINS:
            def read(root, *args):
                path = args[1].removeprefix('HEAD:')
                return ('0'*40 if path == changed else m.INTEGRATION_CORE_PINS[path]).encode()
            with self.subTest(path=changed), patch.object(m, 'is_ancestor', return_value=True), patch.object(m, 'git', side_effect=read), patch.object(m, 'load') as load:
                with self.assertRaisesRegex(ValueError, 'Integrated source changed'):
                    m.configure(ROOT)
                load.assert_not_called()

    def test_pins_cover_actual_ui_composition_and_build_dependencies(self):
        self.assertEqual(set(m.INTEGRATION_CORE_PINS), {'src/nr_command_state.inl', 'src/nr_settings_ui.inl',
            'src/nr_color_policy.inl', 'src/nr_color_amplification.h', 'src/nr_precision_ui.h', 'scripts/build.ps1',
            'src/interop/nr_ngx_parameter_abi.h', 'src/nr_before_sr.cpp', 'src/nr_before_sr.rc', 'src/nr_feedback_capture.inl', 'src/nr_feedback_core.inl',
            'src/nr_feedback_text.h', 'src/nr_feedback_single_text.h', 'src/nr_f8_sequence.inl', 'src/nr_runtime_load_evidence.inl'})
        self.assertEqual(m.INTEGRATION_CORE_PINS['src/nr_settings_ui.inl'], '8aee10a6cf9ef52d5e8e334bf5b520dbc7c03959')
        self.assertEqual(m.INTEGRATION_CORE_PINS['src/nr_command_state.inl'], m.STATE_CURRENT)
        self.assertEqual(m.INTEGRATION_CORE_PINS['scripts/build.ps1'], 'a3ba1f85da395602c83039b1542cc04db4ca9207')

if __name__ == '__main__': unittest.main()
