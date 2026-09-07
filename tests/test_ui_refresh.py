import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]


class UiRefreshContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ui = (ROOT / "src" / "nr_settings_ui.inl").read_text(encoding="utf-8")
        cls.post = (ROOT / "src" / "interop" / "nr_post_ui.inl").read_text(encoding="utf-8")
        cls.render = (ROOT / "src" / "interop" / "nr_render_control.inl").read_text(encoding="utf-8")

    def test_work_scale_is_one_continuous_supported_slider(self):
        self.assertIn('"##nr-processing-precision", &precision, 50.0f, 100.0f', self.ui)
        self.assertIn('ImGui::SliderInt("##post-nr-working-scale",&edit,50,100', self.post)
        self.assertNotIn("for(int percent", self.post)
        self.assertNotIn("100,75,67,50", self.post)

    def test_rr_protection_forces_effective_post_without_a_toggle(self):
        self.assertIn("ShouldGuardFeature1ForRayReconstruction()", self.ui)
        self.assertIn("rr_recent", self.ui)
        self.assertIn("光线重建生效，强制后置 NR", self.ui)
        self.assertIn("正在等待新的 RR 评估", self.post)
        self.assertNotIn("Use RR -> NR when Ray Reconstruction is active", self.post)

    def test_feedback_is_one_button_and_leaves_clipboard_to_present(self):
        self.assertIn("DrawSimpleFeedbackControls();", self.ui)
        self.assertNotIn("nr_f8::DrawControls();", self.ui)
        self.assertEqual(self.ui.count("nr_f8::RequestCore();"), 1)
        self.assertIn("Save feedback TXT + copy path (F8)", self.ui)
        self.assertIn("生成报告并复制路径（F8）", self.ui)
        self.assertIn("path_ready = nr_core_feedback::g_report_path[0] != 0", self.ui)
        self.assertIn("using ClipboardStatus = nr_core_feedback::ReportClipboardStatus", self.ui)
        self.assertIn("ClipboardStatus::Copied", self.ui)
        self.assertIn("ClipboardStatus::Failed", self.ui)
        self.assertNotIn("TakeReportPathForClipboard", self.ui)
        self.assertNotIn("SetClipboardText", self.ui)
        for removed in ("Copy full TXT report", "Copy report summary", "Copy TXT path",
                        "Shortcut", "Comparison and region options"):
            self.assertNotIn(removed, self.ui)

    def test_f7_submenu_uses_fixed_backend_contracts(self):
        self.assertIn("nr_f8::Comparison::Current", self.ui)
        self.assertIn("nr_f8::Comparison::ShareMatrix", self.ui)
        self.assertIn("Current view: 512 x 512, 3 frames", self.ui)
        self.assertIn("One-click share matrix: 7 groups, 2 frames each", self.ui)
        self.assertIn("nr_f8::Cancel();", self.ui)
        self.assertNotIn("nr_f8::Comparison::Strength", self.ui)
        self.assertNotIn("nr_f8::Comparison::Protection", self.ui)
        self.assertNotIn("nr_f8::Comparison::Jitter", self.ui)

    def test_layout_keeps_only_developer_options_collapsible(self):
        self.assertIn('kSettingsOverlayName[] = "装机宅DLSS 5"', self.ui)
        self.assertIn('"交流群（二群）：392308850"', self.ui)
        self.assertIn('BeginTabItem(ui("Experimental###nr-experimental"', self.ui)
        self.assertIn('CollapsingHeader(ui("Developer options", "开发者选项"))', self.ui)
        self.assertNotIn('CollapsingHeader(ui("Advanced picture controls"', self.ui)
        self.assertIn('SettingsSection(ui("Advanced picture controls"', self.ui)
        self.assertIn("ImGui::SameLine();", self.ui)
        self.assertIn("GetTextLineHeightWithSpacing() * 4.25f", self.ui)

    def test_game_render_query_stays_experimental_and_rr_safe(self):
        self.assertIn("ShouldGuardFeature1ForRayReconstruction()", self.render)
        self.assertIn('SliderInt("##game-render-scale", &edit, 50, 100', self.render)
        self.assertIn("Request on next engine query", self.render)
        self.assertIn("Game-owned on next query", self.render)
        self.assertIn("only on a later engine query", self.render)
        self.assertNotIn("IsD3d11PresentRuntime", self.render)


if __name__ == "__main__":
    unittest.main()
