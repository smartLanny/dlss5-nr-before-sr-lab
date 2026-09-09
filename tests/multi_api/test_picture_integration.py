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

    def test_all_dline7_ancestors_required(self):
        self.assertEqual(m.REQUIRED_ANCESTORS, (m.PR131, m.PR132, m.PR134, m.PR137, 'fefd447033b21cf0ae979aa53b7dddc1a8a3e20c', 'c6389f7ea2b7cf865cc23a3bf0afb2268678e3cd', '59ab029614df6fad6ea1e95df18a0a986be58962', 'b0fd489cdfa79d271c0015e35b62186abd0efcd5', 'e071c778d10cb4f079857f128aad3093defb7b9e', '80a6379640cebbd70851d9929790ceee160ace57', '6a0ad7684683993328a7a98a9bea83ac76b01a64', 'ec338d7d9d69230f208b25e4fd457cc79d03c465', 'fa450c9ec2abf2aa48dbdd016b6164626260058b', '2b573c2beb2aa6ea1bacbc2ba4f13cdff49df2dc', '7182b3fd195c8cfb633eec5fa758f3826001a69f'))
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
        self.assertEqual(set(m.INTEGRATION_CORE_PINS), {
            'src/nr_face_capture_geometry.h',
            'src/nr_face_diagnostic.h',
            'src/nr_face_diagnostic_ui.inl',
            'src/nr_face_region_selection.inl',
            'src/nr_f8_gpu.inl',
            'src/nr_feedback_picture.inl',
            'config/nr_before_sr.ini',
            'scripts/patch-post-compat.py',
            'src/nr_codec_descriptors.inl',
            'src/nr_feedback_core.inl',
            'src/nr_feedback_text.h',
            'src/nr_feedback_single_text.h',
            'src/nr_f8_sequence.inl',
            'src/nr_command_state.inl',
            'src/nr_feedback_capture.inl',
            'src/nr_runtime_load_evidence.inl',
            'src/nr_settings_ui.inl',
            'src/nr_model_controls.h',
            'src/nr_optional_layer_probe.h',
            'src/nr_before_sr.cpp',
            'src/nr_recommended_picture.inl',
            'src/nr_work_config.h',
            'src/nr_before_sr.rc',
            'src/nr_color_policy.inl',
            'src/nr_color_amplification.h',
            'src/nr_precision_ui.h',
            'scripts/build.ps1',
            'src/interop/nr_ngx_parameter_abi.h',
            'src/interop/nr_post_settings.inl',
            'src/interop/nr_post_ui.inl',
            'src/interop/nr_post_feedback.inl',
            'src/interop/nr_rr_post_runtime.inl',
            'src/interop/nr_rr_depth_evidence.inl',
            'src/nr_f8_stream.inl',
            'src/nr_imgui_abi.h',
            'src/nr_light_style.h',
            'src/nr_light_style_ui.inl',
            'src/nr_skin_protection.h',
            'src/nr_stage_capture.inl',
            'src/nr_guide_contract.h',
            'src/nr_rr_depth_adapter.inl',
            'src/nr_rr_depth_shader.h',
            'src/nr_rr_depth_projection.h',
            'src/nr_command_creation_observer.inl',
            'src/nr_native_command_record.inl',
            'src/interop/nr_post_area.hlsl',
        })
        self.assertEqual(m.INTEGRATION_CORE_PINS['src/nr_settings_ui.inl'], 'e24f07f5b12f2ccc87bc552ae88d0796042102bf')
        self.assertEqual(m.INTEGRATION_CORE_PINS['src/nr_command_state.inl'], m.STATE_CURRENT)
        self.assertEqual(m.INTEGRATION_CORE_PINS['scripts/build.ps1'], '3d58a4cc8b92701808a62425980798a70673063b')

if __name__ == '__main__': unittest.main()
