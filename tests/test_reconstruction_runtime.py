"""Source contracts for the maintained reconstruction route.

These checks inspect the Core passed to the build. They do not claim shader,
GPU, NGX or game validation.
"""

import argparse
from pathlib import Path
import re
import unittest

from source_contract import definition, source

ROOT = Path(__file__).resolve().parents[1]
GENERATED = None


class ReconstructionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = GENERATED or ROOT
        cls.core = source(cls.root, 'src/nr_before_sr.cpp')

    def body(self, signature):
        return definition(self.core, signature)

    def test_same_frame_paired_sampler_reaches_default_v2(self):
        combine = self.body('void CombineV2CS(')
        for token in ('if (NrReconScaled())', 'NrReconSamplePair(center,',
                      'proxy,neural,edgePairValid);',
                      'edgePairValid && ModelNeighborhoodFiniteV2(center)',
                      'proxy = SampleProxyLinearV2(center);',
                      'ColourLabPreparedReference(storedOriginal.rgb, source)'):
            self.assertIn(token, combine)

    def test_guard_after_chroma_and_before_storage(self):
        for signature, store in (('void CombineCS(', 'float3 sourceDomainResult'),
                                 ('void CombineV2CS(', 'float3 storedResult')):
            body = self.body(signature)
            self.assertLess(body.index('result=NrReconPreserveColour('), body.index(store))
            self.assertGreater(body.index('result=NrReconPreserveColour('), body.index('ColourLabRestoreRgb('))
            self.assertIn('NrReconChromaRequested() && !lightStylePreview', body)

    def test_saved_settings_and_history_contract(self):
        self.assertIn('int reconstruction_mode = 0;', self.core)
        self.assertIn('bool near_black_chroma_guard = false;', self.core)
        self.assertIn('reconstruction_mode', self.body('bool SameConfig('))
        self.assertNotIn('reconstruction_mode', self.body('bool SameHistoryConfig('))
        for key in ('ReconstructionMode', 'NearBlackChromaGuard'):
            self.assertEqual(self.core.count('L"' + key + '"'), 2)

    def test_dispatch_records_configuration_before_capture(self):
        dispatch = self.body('void DispatchCodec(')
        self.assertIn('frame_config.reconstruction_mode', dispatch)
        self.assertNotIn('g_config.reconstruction_mode', dispatch)
        self.assertLess(dispatch.index('constants.region_flags |= reconstruction.flags;'),
                        dispatch.index('nr_replay::recorder.Composition('))
        self.assertGreater(dispatch.index('constants.region_flags |= reconstruction.flags;'),
                           dispatch.index('constants.region_flags=generic_selection.flags;'))

    def test_capture_keeps_real_neural_and_same_sampler(self):
        writer = self.body('void WriteCombinedV2(uint2 coord, uint2 destinationCoord, float4 result,')
        self.assertLess(writer.index('if (!CapturePixelAddress'), writer.index('if(paired)'))
        self.assertNotIn('NrReconSamplePair(', writer)
        self.assertIn('capturedProxy', writer)
        self.assertIn('capturedNeural', writer)
        combine = self.body('void CombineV2CS(')
        self.assertEqual(combine.count('capturedProxy, capturedNeural, NrReconScaled()'), 8)
        self.assertLess(combine.index('const float3 capturedProxy'), combine.index('ApplyGenericStoredV1'))
        self.assertNotIn('NrReconSamplePair(', self.body('ColourStagePixel ColourStageReference('))

    def test_no_new_sampling_bindings(self):
        shader = source(self.root, 'src/nr_reconstruction_sampling.hlsli')
        for forbidden in ('Texture2D<', 'RWTexture', 'register(', 'Interlocked', 'CreateFeature'):
            self.assertNotIn(forbidden, shader)
        for token in ('LoadFinalNeuralStoredV2(p)', 'if(spatial>0.0)',
                      'Width!=ModelWidth||Height!=ModelHeight'):
            self.assertIn(token, shader)
        self.assertIn('kCodecRootConstantCount = 60;', self.core)

    def test_ui_only_edits_reconstruction_settings(self):
        ui = source(self.root, 'src/nr_reconstruction_ui.inl')
        edits = set(re.findall(r'next\.(\w+)\s*=(?!=)', ui)) | set(re.findall(r'&next\.(\w+)', ui))
        self.assertEqual(edits, {'reconstruction_mode', 'near_black_chroma_guard'})
        for token in ('去除暗噪', 'ReconstructionUiModeName(choice)',
                      'if(choice==2)ImGui::SameLine();', 'sample.config_generation==generation', 'F8'):
            self.assertIn(token, ui)
        for forbidden in ('ApplyConfig(', 'WritePrivateProfile', 'CreateFeature(', 'EvaluateFeature('):
            self.assertNotIn(forbidden, ui)

    def test_feedback_and_page_wiring(self):
        pages = source(self.root, 'src/nr_unified_pages.inl')
        picture = source(self.root, 'src/nr_feedback_picture.inl')
        capture = source(self.root, 'src/nr_feedback_capture.inl')
        self.assertIn('DrawReconstructionControls(next,generation,pending,changed,discrete);', pages)
        self.assertIn('requested.reconstructionMode', picture)
        self.assertIn('reconstruction.recorded', capture)
        self.assertIn('not GPU/pixel proof', picture)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--generated', type=Path)
    parser.add_argument('--repo', type=Path)
    args, rest = parser.parse_known_args()
    GENERATED = args.generated or args.repo
    unittest.main(argv=['test_reconstruction_runtime.py'] + rest)
