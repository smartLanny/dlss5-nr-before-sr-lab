"""Contracts across the production settings transaction and Beta 13 pages."""
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]


class UiRefreshContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def source(name):
            return (ROOT / "src" / name).read_text(encoding="utf-8-sig")

        cls.core = source("nr_before_sr.cpp")
        cls.helpers = source("nr_settings_ui.inl")
        cls.settings = source("nr_unified_settings.inl")
        cls.pages = source("nr_unified_pages.inl")
        cls.layers = source("nr_layers_ui.inl")
        cls.dual = source("nr_dual_ui.inl")
        cls.light = source("nr_light_style_ui.inl")
        cls.post = source("interop/nr_post_ui.inl")
        cls.render = source("interop/nr_render_control.inl")
        cls.notices = source("nr_settings_notices_ui.inl")

    def test_work_scale_stays_one_supported_request(self):
        self.assertIn('"##nr-processing-precision", &precision, nr_precision_ui::MinimumPercent(edit_target), 100.0f', self.settings)
        self.assertIn('ImGui::SliderInt("##post-nr-working-scale",&edit,25,100', self.post)
        self.assertIn('if (!nr_post_settings::Request(request)) scale_edit.conflict=true;', self.settings)
        self.assertIn('next.compat_percent=static_cast<unsigned>', self.settings)
        self.assertIn('next.work_mode=request.mode;next.custom_work_scale=request.scale;', self.settings)
        self.assertNotIn('for(int percent', self.post)

    def test_rr_and_compatibility_use_observed_route_without_overwriting_request(self):
        self.assertIn('const auto compatibility_snapshot = SnapshotCompatibilityFallback();', self.settings)
        self.assertIn('const bool rr_guarded=IsRayReconstructionRouteObserved();', self.settings)
        self.assertIn('compatibility_snapshot.recent&&compatibility_snapshot.effective_mode==1', self.settings)
        self.assertIn('DrawProcessingModeBlocks(next,rr_guarded,compatibility_post,compatibility_post?1:next.mode)', self.settings)
        self.assertIn('const bool selected=next.processing_start==route;', self.helpers)
        self.assertIn('next.processing_start=route;', self.helpers)
        self.assertIn('DrawPostRouteStatus();', self.helpers)
        self.assertNotIn('g_compat_fallback_reason.load', self.settings + self.notices)

    def test_picture_page_keeps_single_transaction_and_reset_scope(self):
        picture = self.settings.split('if(ImGui::BeginTabItem(ui("Picture###nr-picture"', 1)[1].split(
            'if(ImGui::BeginTabItem(ui("Lighting###nr-lighting"', 1)[0]
        ordered = ('"##final-colour"', 'DrawUnifiedColourState(', 'DrawProcessingModeBlocks(',
                   '"##nr-processing-precision"', 'DrawUniformLayerCount(', '"##final-gain"',
                   'DrawUniformLayerControls(', 'RestoreRecommendedPictureSettings(next)')
        positions = [picture.index(marker) for marker in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(picture.count('DrawUniformLayerControls('), 1)
        self.assertIn('next.transfer_strength=next.post_transfer_strength=', picture)
        self.assertIn('next.processing_start=nr_processing::Start::Before;', picture)
        self.assertIn('pending_picture_reset=true;', picture)
        self.assertIn('ApplySettingsOverlay(pending,pending_generation,', self.settings)
        self.assertIn('ApplyLightStyleOverlay(next, expected_generation,', self.helpers)

    def test_light_method_and_advanced_group_remain_independent(self):
        self.assertIn('if(ImGui::BeginTabItem(ui("Lighting###nr-lighting"', self.settings)
        self.assertIn('DrawUnifiedLight(next,before_sr,current.generation,', self.settings)
        self.assertIn('NrChoice("light-method",method,methods,3)', self.pages)
        self.assertIn('nr_unified_ui::SetLightMethod(next,method)', self.pages)
        self.assertIn('if(method==0){', self.pages)
        self.assertIn('nr_unified_ui::SetBroadBright(next,broad)', self.pages)
        self.assertIn('nr_light_style::MarkCustom(next)', self.pages)
        self.assertIn('##nr-light-reflection', self.light)
        self.assertIn('##nr-light-broad', self.light)
        self.assertIn('NR 修改前后差', self.light)
        self.assertNotIn('next.mode=', self.light)

    def test_layers_and_model_reset_keep_shared_composition(self):
        self.assertIn('for(unsigned i=0;i<count;++i)', self.layers)
        self.assertIn('const auto count=nr_layers::Active(next.nr_layers);', self.layers)
        self.assertIn('nr_layers::WritePrimary(next,model)', self.layers)
        self.assertIn('"##skin",&model.skin_structure', self.layers)
        self.assertIn('static_cast<nr_layers::Model&>(next.nr_layers[i-1])=model', self.layers)
        self.assertNotIn('SaveConfig(', self.layers)
        reset = (ROOT / 'src' / 'nr_model_controls.h').read_text(encoding='utf-8')
        self.assertNotIn('transfer_strength', reset)

    def test_feedback_is_one_page_and_clipboard_is_present_owned(self):
        feedback = self.settings.split('if(ImGui::BeginTabItem(ui("Feedback###nr-feedback-diagnostics"', 1)[1]
        self.assertIn('DrawUnifiedFeedback(runtime,current.config,has_pending);', feedback)
        self.assertIn('DrawConfigPolicyMigrationNotice(SnapshotConfigPolicyMigrationStatus(),migration_notice_dismissed);', feedback)
        self.assertIn('if(ImGui::Button(report_label))nr_f8::RequestCore();', self.pages)
        self.assertIn('if(ImGui::Button(image_label))nr_f8::RequestFullscreen();', self.pages)
        self.assertIn('nr_f8::RequestFullscreen(nr_f8::Comparison::ShareMatrix)', self.pages)
        self.assertIn('if(ImGui::Button(NrUi("Capture centre model stages"', self.pages)
        self.assertIn('nr_core_feedback::GetReportClipboardStatus()', self.pages)
        self.assertIn('PollReportPathClipboard(static_cast<HWND>(runtime->get_hwnd()), now_ms);', self.core)
        self.assertNotIn('SetClipboardText', self.pages)

    def test_feedback_diagnostics_and_face_geometry_remain_connected(self):
        self.assertIn('DrawFaceDiagnosticControls(runtime,config);', self.pages)
        self.assertIn('DrawTechnicalDiagnostics(runtime);', self.pages)
        self.assertIn('nr_f8::SnapshotFaceDiagnosticGeometry(display_width, display_height)', self.helpers)
        self.assertIn('CleanupFaceDiagnosticSelection(runtime);', self.helpers)
        self.assertIn('nr_f8::RuntimeDestroyed(runtime);', self.helpers)
        self.assertIn('nr_f8::RequestFaceDiagnostic(selection.center_x, selection.center_y,', self.helpers)

    def test_experiments_and_developer_keep_safety_limits(self):
        self.assertIn('DrawUnifiedExperiments(next,current.generation,has_pending,changed,apply_discrete_edit)', self.settings)
        self.assertIn('DrawUnifiedDeveloper(next,before_sr,changed,apply_discrete_edit,runtime)', self.settings)
        self.assertIn('ImGui::BeginDisabled(!before_sr);', self.pages)
        self.assertIn('Detail stability applies before SR only.', self.pages)
        self.assertIn('if(nr_post_settings::missing_projection_save_failed.load', self.pages)
        self.assertIn('Unsupported inputs keep native RR; your saved choice is retained.', self.pages)
        self.assertIn('nr_hotkeys::Set(next,static_cast<nr_hotkeys::Action>(i)', self.pages)
        self.assertIn('if(shortcut_conflict)SettingsNote', self.pages)

    def test_runtime_status_uses_current_snapshot(self):
        self.assertIn('nr_dual_status_runtime::SnapshotUi()', self.dual)
        self.assertIn('if (has_pending && snapshot.state != nr_dual_status_runtime::UiState::Failed)', self.dual)
        self.assertIn('Settings pending:', self.dual)
        self.assertIn('Current window (up to 500 ms):', self.dual)
        self.assertIn('DrawNrActivityStatus(runtime,current.config,current.generation,nr_ui_activity::Site::Top);', self.settings)
        self.assertIn('if (has_pending && pending_generation != current.generation)', self.settings)

    def test_render_query_is_experimental_and_rr_safe(self):
        self.assertIn('IsRayReconstructionRouteObserved()', self.render)
        self.assertIn('SliderInt("##game-render-scale",&edit,50,100', self.render)
        self.assertIn('only a later engine optimal-settings query', self.render)
        self.assertNotIn('IsD3d11PresentRuntime', self.render)


if __name__ == '__main__':
    unittest.main()
