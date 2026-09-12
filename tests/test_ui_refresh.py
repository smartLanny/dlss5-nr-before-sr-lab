import unittest
from pathlib import Path


ROOT = Path(__file__).parents[1]


class UiRefreshContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ui = (ROOT / "src" / "nr_settings_ui.inl").read_text(encoding="utf-8")
        cls.dual = (ROOT / "src" / "nr_dual_ui.inl").read_text(encoding="utf-8")
        cls.light = (ROOT / "src" / "nr_light_style_ui.inl").read_text(encoding="utf-8")
        cls.post = (ROOT / "src" / "interop" / "nr_post_ui.inl").read_text(encoding="utf-8")
        cls.render = (ROOT / "src" / "interop" / "nr_render_control.inl").read_text(encoding="utf-8")
        cls.notices = (ROOT / "src" / "nr_settings_notices_ui.inl").read_text(encoding="utf-8")

    def test_work_scale_is_one_continuous_supported_slider(self):
        self.assertIn('"##nr-processing-precision", &precision, 50.0f, 100.0f', self.ui)
        self.assertIn('ImGui::SliderInt("##post-nr-working-scale",&edit,50,100', self.post)
        self.assertNotIn("for(int percent", self.post)
        self.assertNotIn("100,75,67,50", self.post)

    def test_rr_protection_forces_effective_post_without_a_toggle(self):
        self.assertIn("IsRayReconstructionRouteObserved()", self.ui)
        self.assertIn("DrawPostRouteStatus()", self.ui)
        self.assertIn("NR 未生效", self.post)
        self.assertIn("正在等待新的 RR 评估", self.post)
        self.assertNotIn("Use RR -> NR when Ray Reconstruction is active", self.post)

    def test_feedback_is_one_button_and_leaves_clipboard_to_present(self):
        self.assertNotIn("NR_HAS_F8_SEQUENCE", self.ui)
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
                        "Shortcut", "Comparison and region options",
                        "Single-TXT feedback is unavailable"):
            self.assertNotIn(removed, self.ui)

    def test_f7_submenu_uses_fixed_backend_contracts(self):
        self.assertIn("nr_f8::RequestFullscreen();", self.ui)
        self.assertIn("nr_f8::Comparison::ShareMatrix", self.ui)
        self.assertIn("Current view: full screen, original resolution", self.ui)
        self.assertIn("Seven-group comparison: full-screen originals", self.ui)
        self.assertIn("nr_f8::Cancel();", self.ui)
        self.assertNotIn("nr_f8::Comparison::Strength", self.ui)
        self.assertNotIn("nr_f8::Comparison::Protection", self.ui)
        self.assertNotIn("nr_f8::Comparison::Jitter", self.ui)

    def test_layout_groups_brand_light_and_developer_details(self):
        self.assertIn('kSettingsOverlayName[] = "装机宅DLSS 5"', self.ui)
        self.assertIn('"交流群（二群）：392308850"', self.ui)
        self.assertIn('CollapsingHeader(ui("Developer options", "开发者选项"))', self.ui)
        self.assertIn('CollapsingHeader(ui("About and links", "作者与链接"))', self.ui)
        self.assertIn('CollapsingHeader(ui("Light adjustment", "光影微调"))', self.ui)
        self.assertIn('CollapsingHeader(ui("Algorithm comparisons", "算法对照"))', self.ui)
        self.assertIn('SettingsSection(ui("Advanced picture controls"', self.ui)
        self.assertIn("ImGui::SameLine();", self.ui)
        self.assertIn("GetTextLineHeightWithSpacing() * 6.25f", self.ui)
        self.assertEqual(self.ui.count("DrawGenericCompatibilityStatus();"), 1)
        self.assertLess(self.ui.index("DrawGenericCompatibilityStatus();"),
                        self.ui.index('ImGui::BeginTabBar("nr-main-tabs")'))

    def test_model_stage_menu_uses_existing_current_centre_capture(self):
        image_menu = self.ui[self.ui.index("void DrawSimpleImageCaptureControls()"):
                             self.ui.index("nr_ui_activity::Sample ReadUiActivity(unsigned config_generation")]
        self.assertEqual(image_menu.count("ImGui::Selectable("), 3)
        self.assertEqual(image_menu.count("nr_f8::RequestFullscreen();"), 1)
        self.assertEqual(image_menu.count("nr_f8::RequestFullscreen(nr_f8::Comparison::ShareMatrix);"), 1)
        self.assertEqual(image_menu.count("nr_f8::RequestWithOptions({});"), 1)
        self.assertIn('ui("Model-stage comparison: centre region",\n'
                      '                                  "模型前后对照：画面中心")))\n'
                      '            nr_f8::RequestWithOptions({});', image_menu)
        self.assertIn("请把人脸或反射区域放在画面中央，并保持镜头不动", image_menu)
        self.assertLess(image_menu.index("if (blocked) ImGui::BeginDisabled();"),
                        image_menu.index("nr_f8::RequestWithOptions({});"))
        self.assertLess(image_menu.index("nr_f8::RequestWithOptions({});"),
                        image_menu.index("if (blocked) ImGui::EndDisabled();"))
        sequence = (ROOT / "src" / "nr_f8_sequence.inl").read_text(encoding="utf-8")
        options = sequence[sequence.index("struct Options {"):sequence.index("struct Options {") + 180]
        self.assertIn("Comparison comparison = Comparison::Current", options)
        self.assertIn("bool custom_roi = false", options)
        self.assertIn("bool full_screen = false", options)

    def test_face_diagnostic_is_separate_and_uses_validated_runtime_geometry(self):
        panel = (ROOT / "src" / "nr_face_diagnostic_ui.inl").read_text(encoding="utf-8")
        self.assertIn('"人脸自动对照诊断###nr-face-diagnostic"', panel)
        self.assertIn('"Automatic face comparison###nr-face-diagnostic"', panel)
        self.assertIn('"开始自动对照###nr-face-start"', panel)
        self.assertIn('"取消并恢复设置###nr-face-cancel"', panel)
        self.assertIn('ImGui::CollapsingHeader(', panel)
        self.assertIn('if (!view.enabled || !viewport.valid) ImGui::BeginDisabled();', panel)
        self.assertIn('if (view.blocked) ImGui::BeginDisabled();', panel)
        self.assertIn('nr_face_diagnostic::ResolveRoi(g.source_width, g.source_height, center_x, center_y)', panel)
        self.assertIn('nr_imgui_abi::CallHostVec2(imgui_function_table_instance()->GetMousePos)', panel)
        self.assertIn('get_screenshot_width_and_height(&display_width, &display_height)', self.ui)
        self.assertIn('nr_f8::SnapshotFaceDiagnosticGeometry(display_width, display_height)', self.ui)
        self.assertIn('nr_f8::RequestFaceDiagnostic(selection.center_x, selection.center_y,\n            view.geometry.source_width, view.geometry.source_height)', self.ui)
        self.assertIn('QueueFaceRegionSelection(runtime, selection)', self.ui)
        self.assertIn('DrawFaceDiagnosticControls(runtime, current.config);', self.ui)
        self.assertIn('CleanupFaceDiagnosticSelection(runtime);\n    nr_f8::RuntimeDestroyed(runtime);', self.ui)
        self.assertNotIn('F8CHK004', panel)

    def test_feedback_and_diagnostics_are_one_complete_tab(self):
        marker = '"Feedback and diagnostics###nr-feedback-diagnostics"'
        self.assertEqual(self.ui.count(marker), 1)
        combined = self.ui[self.ui.index(marker):self.ui.index("ImGui::EndTabBar()")]
        self.assertIn("DrawSimpleFeedbackControls();", combined)
        self.assertIn("DrawSimpleImageCaptureControls();", combined)
        self.assertIn("DrawTechnicalDiagnostics(runtime)", combined)
        self.assertIn("nr_f8::TakeSettingsFeedbackPageRequest()", self.ui)
        self.assertIn("ImGuiTabItemFlags_SetSelected", self.ui)
        self.assertIn("nullptr, feedback_tab_flags", combined)
        begin_tabs = self.ui.index('ImGui::BeginTabBar("nr-main-tabs")')
        take_request = self.ui.index("nr_f8::TakeSettingsFeedbackPageRequest()")
        picture = self.ui.index('"Picture###nr-picture"', begin_tabs)
        self.assertLess(begin_tabs, take_request)
        self.assertLess(take_request, picture)
        self.assertNotIn('"Diagnostics###nr-diagnostics"', self.ui)
        self.assertEqual(self.ui.count("nr_f8::RequestCore();"), 1)

    def test_labels_separate_overall_effect_from_tone_follow(self):
        self.assertIn('ui("Extra enhancement", "额外增强")', self.ui)
        self.assertIn('ui("AI colour amount", "AI 色彩强度")', self.ui)
        self.assertIn("amplify its final brightness and detail changes", self.ui)
        self.assertIn("Controls only NR hue and saturation follow", self.ui)
        self.assertIn("If the model changes only luminance, this may have little effect.", self.ui)
        self.assertIn("Applies after releasing the slider.", self.ui)

    def test_extra_enhancement_is_under_model_strength_and_only_amplifies(self):
        model = self.ui.index('"##nr-model-strength"')
        extra = self.ui.index('"##nr-strength-amplification"')
        tone = self.ui.index('"##nr-local-tone"')
        advanced = self.ui.index('SettingsSection(ui("Advanced picture controls"')
        self.assertLess(model, extra)
        self.assertLess(extra, tone)
        self.assertLess(tone, advanced)
        self.assertEqual(self.ui.count('"##nr-strength-amplification"'), 1)
        control = self.ui[extra:tone]
        self.assertIn('1.0f, 4.0f, "%.2f"', control)
        self.assertIn('std::clamp(FiniteOr(effect_strength, 1.0f), 1.0f, 4.0f)', control)
        self.assertIn('next.transfer_strength = next.post_transfer_strength = effect_strength;', control)
        self.assertNotIn('NR_HAS_DETAIL_STABILITY', control)
        self.assertIn('1 = 原量（默认）。', control)

    def test_face_controls_keep_model_meaning_without_claiming_composition_isolation(self):
        self.assertIn('"##nr-skin-detail", &next.skin_structure, 0.f, 2.f', self.ui)
        self.assertIn('设为 0 也不会恢复整张脸原图', self.ui)
        self.assertIn('Both AI passes use these model face parameters', self.ui)
        reset = (ROOT / 'src' / 'nr_model_controls.h').read_text(encoding='utf-8')
        self.assertNotIn('transfer_strength', reset)
        self.assertIn('保留额外增强、光影控制', self.ui)

    def test_compatibility_ui_consumes_one_snapshot_and_keeps_requested_choice_editable(self):
        self.assertEqual(self.ui.count('SnapshotCompatibilityFallback()'), 1)
        self.assertIn('compatibility_snapshot.recent && compatibility_snapshot.effective_mode == 1', self.ui)
        self.assertIn('DrawCompatibilityFallbackNotice(compatibility_snapshot)', self.ui)
        self.assertNotIn('IsCompatibilityPostObserved()', self.ui)
        self.assertNotIn('g_compat_fallback_reason.load', self.ui + self.notices)
        mode = self.ui[self.ui.index('bool DrawProcessingModeBlocks('):
                       self.ui.index('void DrawColorControls(')]
        self.assertIn('bool DrawProcessingModeBlocks(ChainConfig &next, bool rr_guarded,\n'
                      '    bool compatible_post_locked, int effective_mode)', mode)
        self.assertIn('for(unsigned i=0;i<3;++i)', mode)
        self.assertIn('const bool selected=next.processing_start==route;', mode)
        self.assertIn('next.processing_start=route;', mode)
        self.assertIn('next.processing_selection=nr_processing_runtime::next_selection.fetch_add(1', mode)
        self.assertIn('const auto actual=nr_processing_runtime::Effective();', mode)
        self.assertIn('if(actual!=next.processing_start)', mode)
        self.assertNotIn('mode_guarded', mode)
        self.assertNotIn('clicked && !rr_guarded && !selected', mode)
        self.assertIn('DrawProcessingModeBlocks(next, rr_guarded, compatibility_post, compatibility_post ? 1 : next.mode)',
                      self.ui)

    def test_migration_notice_dismisses_locally_without_writing_settings(self):
        self.assertIn('DrawConfigPolicyMigrationNotice(SnapshotConfigPolicyMigrationStatus(),', self.ui)
        self.assertIn('static bool migration_notice_dismissed = false;', self.ui)
        for forbidden in ('SaveConfig(', 'ApplyConfig(', 'WriteIni', 'g_config =', '.store('):
            self.assertNotIn(forbidden, self.notices)

    def test_game_render_query_stays_experimental_and_rr_safe(self):
        self.assertIn("IsRayReconstructionRouteObserved()", self.render)
        self.assertIn('SliderInt("##game-render-scale", &edit, 50, 100', self.render)
        self.assertIn("Request on next engine query", self.render)
        self.assertIn("Game-owned on next query", self.render)
        self.assertIn("only on a later engine query", self.render)
        self.assertNotIn("IsD3d11PresentRuntime", self.render)

    def test_post_route_enables_only_original_lighting_protection(self):
        detail = self.ui.index('"##nr-detail-stability"')
        lighting = self.ui.index('"##nr-lighting-lock"')
        edge = self.ui.index('"##nr-edge-guard"')
        self.assertLess(detail, lighting)
        self.assertLess(lighting, edge)
        self.assertIn("if (!before_sr) ImGui::EndDisabled();",
                      self.ui[detail:lighting])
        self.assertIn("if (!before_sr) ImGui::BeginDisabled();",
                      self.ui[lighting:edge])
        self.assertIn("including native post-SR and RR-forced post-SR",
                      self.ui)
        self.assertIn("not guaranteed face detection", self.ui)

    def test_d13_common_controls_keep_precision_and_face_order(self):
        self.assertIn('zh?"先增强，后放大":"Enhance first"', self.ui)
        self.assertIn('zh?"先放大，后增强":"Upscale first"', self.ui)
        self.assertIn('zh?"兼容模式":"Compatibility"', self.ui)
        picture_start = self.ui.index('if (ImGui::BeginTabItem(ui("Picture###nr-picture"')
        feedback_start = self.ui.index('if (ImGui::BeginTabItem(ui("Feedback and diagnostics###nr-feedback-diagnostics"')
        picture = self.ui[picture_start:feedback_start]
        markers = [
            'ui("Enable (F6)", "启用（F6）")',
            'ui("Restore default picture settings", "一键恢复默认画质")',
            'ui("Picture style", "画面风格")',
            'ui("Processing mode", "处理模式")',
            '"##nr-processing-precision"',
            'nr_dual_runtime::DrawQuickUi(next,changed,apply_discrete_edit,has_pending);',
            '"##nr-model-strength"',
            '"##nr-strength-amplification"',
            'DrawColorControls(next, has_pending, changed);',
            'ui("Face adjustment", "人脸调节")',
        ]
        positions = [picture.index(marker) for marker in markers]
        self.assertEqual(positions, sorted(positions))
        dual_start = picture.index('nr_dual_runtime::DrawQuickUi')
        model_start = picture.index('SettingsSection(ui("Model controls"')
        dual = picture[dual_start:model_start]
        self.assertIn('25.f,100.f', self.dual)
        self.assertIn('"50%###dual-scale-50"', self.dual)
        self.assertIn('"75%###dual-scale-75"', self.dual)
        self.assertIn('"100%###dual-scale-100"', self.dual)
        self.assertIn('next.nr_second_numerator=2;next.nr_second_denominator=3;', self.dual)
        self.assertIn('nr_dual_runtime::DrawDetailsUi(current.config.nr_passes == 2, has_pending);', self.ui[feedback_start:])

    def test_d13_dual_status_uses_current_snapshot_and_hides_pending_observation(self):
        self.assertIn('nr_dual_status_runtime::SnapshotUi()', self.dual)
        self.assertIn('has_pending || changed', self.dual)
        self.assertIn('Current window (up to 500 ms):', self.dual)
        self.assertIn('eligible_primary', self.dual)
        self.assertIn('secondary_evaluate_success', self.dual)
        self.assertIn('warmup_discarded', self.dual)
        self.assertIn('final_adopted', self.dual)
        details = self.dual[self.dual.index('void DrawDetailsUi'):
                            self.dual.index('// Compatibility wrapper')]
        self.assertNotIn('second_evaluations.load', details)
        self.assertIn('Settings pending:', details)

    def test_d13_light_method_is_single_selector_and_hides_unverified_reflection(self):
        self.assertIn('next.generic_residual ? 2u : next.region_enabled ? 1u : 0u', self.ui)
        self.assertIn('"Normal light/dark\\0Engine regions (experimental)\\0Generic layers (experimental)\\0"', self.ui)
        self.assertIn('"普通亮暗\\0引擎分区（实验）\\0通用分层（实验）\\0"', self.ui)
        region = self.ui[self.ui.index('} else if (light_method == 1u) {'):
                        self.ui.index('} else {\n        DrawLightStyleControls', self.ui.index('} else if (light_method == 1u) {'))]
        self.assertIn('for(unsigned i=0;i<4;i++)', region)
        self.assertIn('if (regionStatus == 3u)', region)
        self.assertIn('游戏未提供反射区域数据', region)
        self.assertIn('nr_dual_runtime::DrawDetailsUi(current.config.nr_passes == 2, has_pending);', self.ui)
        self.assertIn('全部亮暗变化###light-mode-option', self.light)
        self.assertIn('仅细小变化###light-mode-option', self.light)


if __name__ == "__main__":
    unittest.main()
