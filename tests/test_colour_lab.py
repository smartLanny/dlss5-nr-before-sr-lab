"""Source-transform unit checks. --repo additionally checks actual pinned inputs.
Host unit tests are not a Windows/NGX/game validation.
"""
import argparse
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patch', ROOT/'scripts/patch-colour-lab.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
REPO = None

class PatchTests(unittest.TestCase):
    def test_unique_anchors(self):
        self.assertEqual(patch.once('abc', 'b', 'B', 'test'), 'aBc')
        for text in ('ac', 'abbc'):
            with self.assertRaises(ValueError):
                patch.once(text, 'b', 'B', 'test')
    def test_runtime_fixture(self):
        original = '\n'.join([
            '#include "nr_layer_config.h"', 'struct ChainConfig {',
            'bool SameConfig(const ChainConfig &a, const ChainConfig &b) {\n    return a.enabled == b.enabled;',
            '    next.allow_unverified_hdr_color = GetPrivateProfileIntW(\n        L"NRBeforeSR", L"AllowUnverifiedHdrColor", 0, g_ini_path) != 0;',
            '    record_write(WriteIniInt(L"AllowUnverifiedHdrColor",\n                             config.allow_unverified_hdr_color ? 1 : 0));',
            'constexpr const char *kAddonVersion = "beta0.5-dline21-uniform3";',
            'constexpr const char *kBuildIdentity = "beta0.5-dline21-uniform3";'])
        modified = patch.patch_runtime(original)
        self.assertEqual(modified.count('L"ColourLabMode"'), 2)
        self.assertIn('nr_colour_lab::kDefaultMode', modified)
        with self.assertRaises(ValueError):
            patch.patch_runtime(modified)
    def test_policy_fixture(self):
        original = '\n'.join([
            '    ExperimentalUnverifiedHdr,',
            '    if (allow && policy.color_reason == NrColorReason::UnverifiedHdr) {',
            '        policy.color_reason = NrColorReason::ExperimentalUnverifiedHdr;',
            '    case NrColorReason::UnverifiedHdr: return "unverified-hdr-luminance-only";'])
        modified = patch.patch_policy(original)
        self.assertIn('nr_colour_lab::AllowUnverified(lab_mode, allow)', modified)
        self.assertIn('policy.color_reason == NrColorReason::UnverifiedHdr', modified)
        self.assertIn('colour-lab-priority-unverified-hdr', modified)
        with self.assertRaises(ValueError):
            patch.patch_policy(modified)
    def test_ui_transaction(self):
        self.assertIn('DrawColourLabControls(next, generation, has_pending, changed, apply_discrete_edit);',
                      patch.patch_ui('    (void)before_sr;'))
        ui = (ROOT/'src/nr_colour_lab_ui.inl').read_text(encoding='utf-8')
        self.assertNotIn('ApplyConfig(', ui)
        self.assertNotIn('WritePrivateProfile', ui)
        self.assertIn('sample.status != NrColorPolicyStatus::Current', ui)
    def test_actual_sources(self):
        if REPO is None:
            self.skipTest('supply --repo for actual repository source checks')
        for path, transform in [('nr_before_sr.cpp', patch.patch_runtime),
                                ('nr_color_policy.inl', patch.patch_policy),
                                ('nr_light_style_ui.inl', patch.patch_ui)]:
            text = (REPO/'src'/path).read_text(encoding='utf-8')
            modified = transform(text)
            self.assertNotEqual(text, modified)
            if path == 'nr_before_sr.cpp':
                # The embedded shader body must remain byte-identical in M1.
                import re
                bodies = re.findall(r'R"([A-Za-z_]*)\((.*?)\)\1"', text, re.S)
                changed = re.findall(r'R"([A-Za-z_]*)\((.*?)\)\1"', modified, re.S)
                self.assertEqual(bodies, changed)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=pathlib.Path)
    args, rest = parser.parse_known_args()
    REPO = args.repo
    unittest.main(argv=['test_colour_lab.py'] + rest)
