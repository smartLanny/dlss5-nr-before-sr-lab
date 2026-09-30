"""Behavior checks for the former colour and compatibility patch chain.

The maintained source is the input. Historic transformation tests live under
tests/archive/patch-chain-beta13/ and are excluded from portable validation.
"""

from pathlib import Path
import re
import unittest

from source_contract import definition, source

ROOT = Path(__file__).resolve().parents[1]


class FoldedSourceContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = source(ROOT, 'src/nr_before_sr.cpp')

    def body(self, signature):
        return definition(self.core, signature)

    def test_color_defaults_and_history_identity(self):
        for value in ('int colour_lab_mode = 1;',
                      'int colour_restore_mode = nr_colour_restore::kDefaultMode;',
                      'int colour_input_curve = nr_colour_restore::kDefaultInputCurve;',
                      'int colour_scaled_proxy_mode = nr_colour_restore::kDefaultScaledProxyMode;',
                      'bool colour_neutral_probe = false;', 'int colour_edge_recovery = 0;'):
            self.assertTrue(value in self.core, value)
        history = self.body('bool SameHistoryConfig(')
        current = self.body('bool SameConfig(')
        for value in ('colour_input_curve', 'colour_scaled_proxy_mode'):
            self.assertIn(value, history)
        for value in ('colour_restore_mode', 'colour_neutral_probe', 'colour_edge_recovery'):
            self.assertNotIn(value, history)
            self.assertIn(value, current)

    def test_color_dispatch_records_actual_consumer(self):
        dispatch = self.body('void DispatchCodec(')
        for value in ('frame_config.colour_restore_mode',
                      'slot.colour_restore_encoding.For(slot.last_used_serial)',
                      'recorded_neutral_probe = colour_stage_flags != 0u',
                      'recorded_edge_recovery = edge_flags != 0u',
                      'restore_encoding != nr_colour_restore::Encoding::Unknown'):
            self.assertIn(value, dispatch)
        self.assertNotIn('g_config.colour_restore_mode', dispatch)
        self.assertNotIn('g_config.colour_neutral_probe', dispatch)

    def test_paired_color_and_stage_sampling(self):
        combine = self.body('void CombineV2CS(')
        for value in ('ColourStageReference(', 'ColourEdgeSamplePair(',
                      'ModelNeighborhoodFiniteV2(center)', 'proxy = SampleProxyLinearV2(center);'):
            self.assertIn(value, combine)
        raw = self.body('ColourStagePixel ColourStageReference(')
        self.assertIn('ColourEdgeSamplePair(center, prepared, ColourStageNeutral())', raw)
        self.assertIn('valid && pairValid', raw)
        self.assertIn('TransferStrength, FrameConfidence', raw)
        stage = source(ROOT, 'src/nr_colour_stage_reference.hlsli')
        edge = source(ROOT, 'src/nr_colour_edge.hlsli')
        for shader in (stage, edge):
            self.assertNotIn('RWTexture', shader)
            self.assertNotIn('Texture2D', shader)
        self.assertIn('p.proxy += proxy * weight;', edge)
        self.assertIn('p.neural += neural * weight;', edge)

    def test_color_settings_and_feedback(self):
        ui = source(ROOT, 'src/nr_unified_pages.inl')
        quality = source(ROOT, 'src/nr_quality_ui.inl')
        feedback = source(ROOT, 'src/nr_feedback_picture.inl')
        for value in ('next.colour_restore_mode', 'next.colour_neutral_probe',
                      'next.colour_edge_recovery'):
            self.assertIn(value, ui + quality)
        for value in ('recorded_restore_mode', 'recorded_neutral_probe',
                      'recorded_edge_recovery'):
            self.assertIn(value, feedback + quality)
        for forbidden in ('ApplyConfig(', 'WritePrivateProfile', 'CreateFeature(', 'EvaluateFeature('):
            self.assertNotIn(forbidden, quality)

    def test_quality_recipe_changes_only_six_selectors(self):
        recipe = source(ROOT, 'src/nr_quality_recipe.h')
        written = set(re.findall(r'c\.(\w+)\s*=(?!=)', recipe))
        self.assertEqual(written, {'colour_lab_mode', 'colour_restore_mode',
                                   'colour_input_curve', 'colour_scaled_proxy_mode',
                                   'colour_edge_recovery', 'colour_neutral_probe'})
        self.assertIn('nr_quality_recipe::Select(next,', source(ROOT, 'src/nr_quality_ui.inl'))

    def test_quality_warnings_precede_experiment_controls(self):
        quality = source(ROOT, 'src/nr_quality_ui.inl')
        cut = quality.index('ImGui::CollapsingHeader(')
        for message in ('RGB recovery is active: owner tests reported added contour halos',
                        'ZERO-EDIT TEST ACTIVE', 'N2 NOT applied',
                        'The 99% / 100% colour difference remains unresolved'):
            self.assertLess(quality.index(message), cut)
        for setting in ('next.colour_restore_mode = recovery;',
                        'next.colour_edge_recovery = edge;',
                        'next.colour_neutral_probe = neutralProbe;'):
            self.assertGreater(quality.index(setting), cut)
        self.assertIn('sample.config_generation == generation', quality)
        self.assertIn('!current || changed || StageCaptureStatus() != 0u', quality)

    def test_compatibility_adapters_are_in_production_source(self):
        for path in ('src/interop/nr_exposure_access_d3d12.inl',
                     'src/interop/nr_ngx_parameter_abi.h',
                     'src/interop/nr_post_ui.inl'):
            self.assertTrue((ROOT / path).is_file(), path)
        self.assertIn('nr_exposure_access_d3d12.inl', self.core)
        for signature in ('bool GetResource(', 'bool GetInt('):
            # The public Core keeps the native NGX parameter ABI in the
            # maintained translation unit rather than applying a patch.
            self.assertIn(signature, self.core)


if __name__ == '__main__':
    unittest.main()
