"""Final successor UI contracts, separate from retained historical adapter tests.

Use --generated for a real complete export. CPU interaction and Win32 persistence
are covered by test_d13_ui_compile, test_working_scale_ui and test_light_style.
"""
import argparse
from pathlib import Path
import unittest
import re
import json

ROOT = Path(__file__).resolve().parents[1]
GENERATED = None


class UnifiedUi(unittest.TestCase):
    def setUp(self):
        root = GENERATED or ROOT
        self.root = root
        self.settings = (root/'src/nr_unified_settings.inl').read_text(encoding='utf-8')
        self.pages = (root/'src/nr_unified_pages.inl').read_text(encoding='utf-8')
        self.layers = (root/'src/nr_layers_ui.inl').read_text(encoding='utf-8')
        self.maxDiff = 500

    def test_flat_navigation_and_order(self):
        self.assertEqual(self.settings.count('ImGui::BeginTabBar('), 1)
        self.assertEqual(self.settings.count('ImGui::BeginTabItem('), 5)
        self.assertEqual(self.settings.count('ImGui::BeginChild("nr-page-'),5)
        self.assertEqual(self.settings.count('ImGui::EndChild();ImGui::EndTabItem();'),5)
        self.assertLess(self.settings.index('DrawNrActivityStatus('),self.settings.index('ImGui::BeginTabBar('))
        for text in (self.settings, self.layers):
            self.assertFalse('CollapsingHeader(' in text)
        self.assertEqual(self.pages.count('CollapsingHeader('), 1)
        self.assertIn('ImGui::CollapsingHeader(NrUi("Custom shortcuts###nr-custom-shortcuts"', self.pages)
        self.assertFalse('BeginTabBar(' in self.pages+self.layers)
        markers = ['###master-nr', '##final-colour', 'DrawProcessingModeBlocks(', '##nr-processing-precision',
                   'DrawUniformLayerCount(', '##final-gain',
                   'DrawUniformLayerControls(', 'RestoreRecommendedPictureSettings(']
        positions = [self.settings.index(m) for m in markers]
        self.assertEqual(positions, sorted(positions))
        self.assertFalse('next.compat_percent=100;' in self.settings)
        self.assertTrue('ApplySettingsOverlay(pending,pending_generation,' in self.settings)

    def test_only_active_layers_without_secondary_enable_controls(self):
        self.assertTrue('const auto count=nr_layers::Active(next.nr_layers);' in self.layers)
        self.assertTrue('i<count' in self.layers)
        self.assertFalse('Enable layer' in self.layers)
        self.assertTrue('BeginTable("layer-fields",2' in self.layers)
        self.assertTrue('GetFontSize()*25.f' in self.layers)
        for control in ('##intensity','##structure','##tone','##skin','##native-mask','##ui-correction'):
            self.assertEqual(self.layers.count('"'+control+'"'),1)

    def test_one_colour_permission_and_feedback_owner(self):
        self.assertEqual(self.pages.count('&next.allow_unverified_hdr_color'),0)
        self.assertTrue('nr_colour_preferences::SetRange(next,next.color_strength)' in self.settings)
        self.assertNotIn('NrChoice("colour-policy"',self.settings)
        self.assertGreater(self.settings.index('##final-colour'),self.settings.index('ImGui::BeginTabBar('))
        self.assertLess(self.settings.index('ImGui::TextWrapped("DLSS 5  %s",kAddonVersion)'),self.settings.index('ImGui::BeginTabBar('))
        self.assertFalse('&next.allow_unverified_hdr_color' in self.settings)
        self.assertEqual(self.pages.count('nr_f8::RequestCore();'),1)
        self.assertNotIn('RequestStageCapture();',self.pages)
        self.assertEqual(self.pages.count('nr_f8::RequestFullscreen();'),1)
        self.assertEqual(self.pages.count('nr_f8::RequestWithOptions({});'),1)
        self.assertIn('nr_hotkeys::Key(nr_hotkeys::Image)',self.pages)
        self.assertNotIn('SettingsSection(NrUi("Energy saving"',self.pages)
        picture = self.settings[:self.settings.index('Lighting###')]
        self.assertFalse('RequestStageCapture(' in picture)
        for field in ('face_protection','pause_nr_on_fullscreen_ui','guess_pause_nr'):
            self.assertFalse('next.'+field+'=' in self.settings)
        self.assertEqual(self.pages.count('DrawTemporalOptions(next,changed,discrete);'),1)

    def test_face_disabled_preview_cannot_request_work(self):
        face = (self.root/'src/nr_face_runtime.inl').read_text(encoding='utf-8')
        at = face.index('bool Requested(')
        requested = face[at:face.index('}',at)]
        self.assertTrue('g_config.face_protection' in requested)
        self.assertFalse('face_preview' in requested)
        self.assertTrue('next.face_preview=0' in face)

    def test_temporal_modes_and_private_sr_boundary_are_explicit(self):
        temporal = (self.root/'src/nr_temporal_ui.inl').read_text(encoding='utf-8')
        private_sr = (self.root/'src/nr_private_sr_ui.inl').read_text(encoding='utf-8')
        self.assertIn('next.temporal_mode==0',temporal)
        self.assertIn('next.temporal_mode==1',temporal)
        self.assertIn('A submitted frame is not proof of history adoption',temporal)
        self.assertIn('It needs proven D3D12, colour and motion inputs',private_sr)
        self.assertIn('Present, FG and multiple GPUs are unsupported',private_sr)

    def test_final_export_uses_successor_once(self):
        if GENERATED is None:
            self.skipTest('real complete export checked by the generator')
        ui = (GENERATED/'src/nr_settings_ui.inl').read_text(encoding='utf-8')
        self.assertEqual(ui.count('#include "nr_unified_settings.inl"'),1)
        self.assertFalse('void DrawSettings(' in ui)
        self.assertEqual(self.settings.count('void DrawSettings('),1)
        core = (GENERATED/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        version = re.search(r'kAddonVersion\s*=\s*"([^"]+)"',core).group(1)
        identity = re.search(r'kBuildIdentity\s*=\s*"([^"]+)"',core).group(1)
        resource = (GENERATED/'src/nr_before_sr.rc').read_text(encoding='utf-8-sig')
        self.assertIn(version,resource)
        for path in ('src/nr_before_sr.rc','scripts/build.ps1'):
            payload_identity = (GENERATED/path).read_text(encoding='utf-8-sig')
            self.assertIn(identity,payload_identity)
        product = json.loads((ROOT/'scripts/core-source-layout.json').read_text(encoding='utf-8'))['product']
        self.assertEqual(identity,product['version'])
        self.assertEqual(version,product['display_version'])
        addon_names = re.findall(r'const char \*NAME =\s*"([^"]+)"',core)
        self.assertEqual(len(addon_names),2)
        for name in addon_names:
            self.assertIn(version,name)
            self.assertNotIn('dline21-uniform3',name)
        for name in ('nr_unified_settings.inl','nr_unified_pages.inl','nr_unified_ui_policy.h'):
            # A direct-source stage copies the maintained file bytes for this run.
            self.assertEqual((GENERATED/'src'/name).read_bytes(),(ROOT/'src'/name).read_bytes())


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--repo',type=Path)  # shared final-generator invocation
    p.add_argument('--generated',type=Path)
    args,rest = p.parse_known_args()
    GENERATED = args.generated
    unittest.main(argv=['test_unified_ui.py']+rest)
