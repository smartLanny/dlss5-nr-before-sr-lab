// Final quality panel. Only edits the existing pending transaction. Rendering
// or collapsing a section never changes saved settings or model parameters.
void DrawColourLabControls(ChainConfig &next, unsigned generation,
                           bool has_pending, bool &changed, bool &apply_discrete_edit) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::PushID("nr-quality-panel");
    SettingsField(ui("Picture recipe", "画质方案"));
    int recipe = static_cast<int>(nr_quality_recipe::Describe(next));
    if (ImGui::Combo(ui("Recipe###quality-recipe", "方案###quality-recipe"), &recipe,
        ui("Colour baseline\0N2 shadow comparison [experiment]\0Custom / research\0",
           "颜色基准：旧合成\0N2 阴影对照 [实验]\0自定义 / 研究设置\0"))) {
        if (nr_quality_recipe::Select(next, static_cast<nr_quality_recipe::Recipe>(recipe))) {
            changed = true; apply_discrete_edit = true;
        }
    }
    SettingsTooltip(ui(
        "Both recipes enable AI colour and use D21 composition. N2 changes only the input curve and resets NR history. Model/scale/layers/strength/lighting controls stay as saved. N2 plus D21 composition is a NEW comparison, not verified RenoDX equivalence.",
        "两种方案均优先采用 AI 色彩，使用 D21 合成；N2 方案只再改输入曲线，会重建 NR 历史。模型、分辨率、层数、强度与光照控制保持原值。N2 + D21 合成是待验证的新组合，不是已确认等价 RenoDX。"));
    if (nr_quality_recipe::Describe(next) == nr_quality_recipe::Recipe::Custom)
        SettingsNote(ui("Existing custom settings were preserved. Choosing a recipe clears only the colour-lab experiments.",
            "已保留旧的自定义设置；选择方案仅退出画质实验，不覆盖其他参数。"));
    if (next.colour_restore_mode != 0)
        SettingsNote(ui("RGB recovery is active: owner tests reported added contour halos and no clear fabric-detail gain. Use Colour baseline to leave this experiment.",
            "当前仍开启 RGB 恢复：实测报告新增轮廓亮边，未明显补回衣物结构。选“颜色基准”可退出该实验。"));
    if (next.colour_neutral_probe)
        SettingsNote(ui("ZERO-EDIT TEST ACTIVE: NR still runs; the consumer uses synthetic N=P. Disable this before judging quality or performance.",
            "零修改测试正在运行：NR 照常执行，但合成采用模拟 N=P。评价画质或性能前必须关闭。"));

    const auto sample = SnapshotColorPolicy();
    const bool current = !changed && !has_pending && sample.config_generation == generation &&
        sample.status == NrColorPolicyStatus::Current;
    if (changed || has_pending || sample.config_generation != generation)
        SettingsNote(ui("Waiting for committed settings and matching recorded output.", "等待设置提交与匹配的合成记录。"));
    else if (!current) {
        SettingsNote(NrColorPolicyStatusText(sample.status, zh));
        if (sample.status == NrColorPolicyStatus::Bypassed)
            SettingsNote(ui("Native output retained. Capture F8 and report the bypass as a BUG.",
                "已保留游戏原画面；请按 F8 收集并提交旁路 BUG。"));
    } else {
        ImGui::TextWrapped(ui("AI colour: requested %.2f / recorded %.2f", "AI 色彩：请求 %.2f / 已录制 %.2f"),
            sample.policy.requested_color_strength, sample.policy.effective_color_strength);
        ImGui::TextWrapped("%s: %s / %s", ui("Recorded path", "已录制路径"),
            nr_colour_restore::Name(sample.policy.recorded_restore_mode),
            nr_colour_restore::CurveName(sample.policy.recorded_input_curve));
        if (next.colour_lab_mode == 1 && sample.policy.requested_color_strength > 0.0f &&
            sample.policy.effective_color_strength == 0.0f)
            SettingsNote(ui("AI colour is still limited; inspect the reason below and capture F8.",
                "AI 色彩仍被限制；查看下方原因并收集 F8。"));
        if (sample.policy.requested_input_curve == 1 && sample.policy.recorded_input_curve != 1)
            SettingsNote(ui("N2 NOT applied to this recorded input. It is not an N2 comparison.",
                "此输入记录未采用 N2；当前不能算 N2 对照。"));
        if ((next.colour_restore_mode != 0 || next.colour_neutral_probe) && !sample.policy.restore_encoding_known)
            SettingsNote(ui("Missing matched encoder receipt: baseline retained. Capture F8 and report a BUG.",
                "缺少匹配编码记录：已保留基线。请按 F8 收集并反馈 BUG。"));
    }
    SettingsNote(ui("The 99% / 100% colour discontinuity remains unresolved, including D21 composition. EdgeGuard is not a de-ringing filter.",
        "99% / 100% 的色差仍未解决，D21 合成也受影响。“边缘保护”不是抗振铃滤镜。"));

    // Diagnostic controls remain available, but are not daily-use recommendations.
    if (ImGui::CollapsingHeader(ui("Developer colour controls###quality-research", "开发者颜色选项###quality-research"))) {
        int mode = static_cast<int>(nr_colour_lab::Normalize(next.colour_lab_mode));
        if (ImGui::Combo(ui("Colour policy###quality-policy", "颜色策略###quality-policy"), &mode,
            ui("Legacy configured\0Colour priority\0Conservative HDR\0",
               "原配置策略\0颜色优先\0保守 HDR 限制\0"))) {
            next.colour_lab_mode = mode; changed = true; apply_discrete_edit = true;
        }
        int recovery = nr_colour_restore::Normalize(next.colour_restore_mode);
        if (ImGui::Combo(ui("Recovery###quality-restore", "恢复算法###quality-restore"), &recovery,
            ui("D21 composition\0Protected RGB [halo regression]\0Raw RGB [diagnostic]\0",
               "D21 合成\0受保护 RGB [有亮边反馈]\0原始 RGB [诊断]\0"))) {
            next.colour_restore_mode = recovery; changed = true; apply_discrete_edit = true;
        }
        int curve = next.colour_input_curve == 1 ? 1 : 0;
        if (ImGui::Combo(ui("Input curve###quality-curve", "输入曲线###quality-curve"), &curve,
            ui("D21 input\0Paired N2 HDR\0", "D21 原输入\0成对 N2 HDR\0"))) {
            next.colour_input_curve = curve; changed = true; apply_discrete_edit = true;
        }
        int scaledPrep = nr_colour_restore::NormalizeScaledProxy(next.colour_scaled_proxy_mode);
        if (ImGui::Combo(ui("Scaled input###quality-scale", "低分输入顺序###quality-scale"), &scaledPrep,
            ui("Source first\0Proxy first [no improvement in tested scene]\0",
               "先缩放场景\0先编码 Proxy [本场景未见改善]\0"))) {
            next.colour_scaled_proxy_mode = scaledPrep; changed = true; apply_discrete_edit = true;
        }
        int edge = nr_colour_edge::Normalize(next.colour_edge_recovery);
        if (ImGui::Combo(ui("Edge reconstruction###colour-edge", "配对边缘恢复###colour-edge"), &edge,
            ui("D21 bilinear\0Paired weights [partial improvement only]\0",
               "D21 双线性\0配对权重 [仅部分改善]\0"))) {
            next.colour_edge_recovery = edge; changed = true; apply_discrete_edit = true;
        }
        bool neutralProbe = next.colour_neutral_probe;
        if (ImGui::Checkbox(ui("Zero-edit test###quality-neutral", "零修改对照###quality-neutral"), &neutralProbe)) {
            next.colour_neutral_probe = neutralProbe; changed = true; apply_discrete_edit = true;
        }
        if (next.colour_edge_recovery && next.colour_restore_mode == 0 && !next.colour_neutral_probe)
            SettingsNote(ui("Paired edge recovery is inactive with D21 composition.", "D21 合成不采用配对边缘恢复。"));
        SettingsNote(ui("Source/model/history are unchanged by output-only controls. Raw bypasses artistic attenuation, not GPU safety; N=P only checks zero-edit sampling. Neither is a proven quality upgrade.",
            "输出控制不改模型输入或历史。原始 RGB 绕过审美衰减，不绕过 GPU 安全；N=P 只检查零修改取样。二者均不是已验证的画质升级。"));
        SettingsNote(ui("Colour priority / N2 may alter internal HDR hue and highlights even with SDR output. Revert on errors and collect F8.",
            "即使最终输出 SDR，颜色优先 / N2 也可能影响内部 HDR 色相与高光；异常时回退并收集 F8。"));
        if (current && !changed) {
            ImGui::TextWrapped("%s: %s", ui("Reason", "原因"), NrColorReasonName(sample.policy.color_reason));
            ImGui::TextWrapped("%s: %s / %s", ui("Recorded experiments", "已录制实验"),
                nr_colour_restore::ScaledProxyName(sample.policy.recorded_scaled_proxy_mode),
                nr_colour_edge::Name(sample.policy.recorded_edge_recovery != 0));
            ImGui::TextWrapped("%s", nr_colour_stage::Name(sample.policy.recorded_neutral_probe));
            if (next.colour_edge_recovery && !sample.policy.recorded_edge_recovery)
                SettingsNote(ui("Edge option not applied: requires scaled V2 RGB and matched input receipt; unexpected bypass needs F8/BUG.",
                    "边缘选项未采用：需要低分 V2 RGB 与匹配输入记录；意外旁路请 F8 / BUG 反馈。"));
        }
        if (ImGui::Button(ui("Legacy settings###quality-legacy", "回到旧版画质策略###quality-legacy"))) {
            next.colour_lab_mode = 0; next.colour_restore_mode = 0;
            next.colour_input_curve = 0; next.colour_scaled_proxy_mode = 0;
            next.colour_edge_recovery = 0; next.colour_neutral_probe = false;
            changed = true; apply_discrete_edit = true;
        }
    }
    ImGui::BeginDisabled(!current || changed || StageCaptureStatus() != 0u);
    if (ImGui::Button(ui("Capture stages (F7)###quality-capture", "捕获处理阶段（F7）###quality-capture"))) {
        if (!RequestStageCapture())
            SettingsNote(ui("Capture not accepted; no completed capture is claimed.", "捕获未受理，并非已经完成。"));
    }
    ImGui::EndDisabled();
    SettingsTooltip(ui("F7 resets/warms NR, then exports sampled S/P/real N/E. Before SR, E is the SR INPUT, not final output. Use F8 centre ROI for fabric detail. Recorded constants are not GPU completion or a quality verdict.",
        "F7 重建/预热后导出抽样 S/P/真实 N/E；前置 E 是 SR 输入，不是最终画面。衣物细节使用 F8 中心区域。常量录制不是 GPU 完成或画质验收。"));
    ImGui::Separator();
    ImGui::PopID();
}
