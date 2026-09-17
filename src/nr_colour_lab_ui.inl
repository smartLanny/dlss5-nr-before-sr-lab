// Included where the existing settings helpers and the pending transaction
// are available. All writes go through that transaction, never directly to
// g_config; the regular Apply/Save path owns locking and INI persistence.
void DrawColourLabControls(ChainConfig &next, unsigned generation,
                           bool has_pending, bool &changed, bool &apply_discrete_edit) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::PushID("nr-colour-lab-m1");
    ImGui::Separator();
    SettingsField(ui("Colour comparison [M1 experiment]", "画质对比：AI 色彩 [M1 实验]"));
    int mode = static_cast<int>(nr_colour_lab::Normalize(next.colour_lab_mode));
    if (ImGui::Combo(ui("Colour policy###colour-lab-mode", "颜色策略###colour-lab-mode"), &mode,
        ui("Legacy: original configured policy\0Colour priority [experimental]\0Conservative: limit unverified HDR\0",
           "旧版：保留原配置策略\0颜色优先 [实验]\0保守限制：未验证 HDR 仅明暗\0"))) {
        next.colour_lab_mode = mode;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Changes only colour admission, not model settings, layer count, raster, exposure or resource validation. Legacy also retains the saved legacy HDR opt-in. RGB restoration is not implemented in M1.",
        "仅改变 AI 色彩准入，不改模型、层数、分辨率、曝光或资源安全检查。旧版仍遵守已保存的旧 HDR 实验许可。M1 尚未实现 RGB 恢复。"));
    if (mode == 1) {
        SettingsNote(ui(
            "Experimental: internal HDR/scRGB/PQ inputs may change hue or highlights, even on an SDR display. Use Conservative if the result is wrong, then collect F8 and report the game.",
            "实验警告：即使显示器为 SDR，内部 HDR/scRGB/PQ 输入也可能偏色或高光异常。出现异常请切回保守限制，按 F8 收集并反馈游戏。"));
    } else if (mode == 0 && next.allow_unverified_hdr_color) {
        SettingsNote(ui("Legacy HDR colour opt-in is enabled; Legacy is not the strict mode.",
            "旧配置已开启 HDR 试色；“旧版”不等同于保守限制。"));
    }
    if (ImGui::Button(ui("Return to legacy###colour-lab-reset", "一键回到旧版###colour-lab-reset"))) {
        next.colour_lab_mode = 0;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsNote(ui("AI colour and original-lighting sliders keep their values. This comparison does not recreate NR history.",
        "AI 色彩、原始光照保护等滑块保留原值；该对比不重建 NR 历史。"));

    const auto sample = SnapshotColorPolicy();
    // A pending UI edit must not label the preceding dispatch as its result.
    if (changed || has_pending || sample.config_generation != generation) {
        SettingsNote(ui("Candidate selected; waiting for the normal settings transaction and a matching composition sample.",
            "已选择候选，等待设置提交与对应合成记录。"));
    } else if (sample.status != NrColorPolicyStatus::Current) {
        SettingsNote(NrColorPolicyStatusText(sample.status, zh));
    } else {
        ImGui::TextWrapped(ui("Recent recorded combine: requested colour %.2f; effective coefficient %.2f.",
                             "最近录制的合成：请求色彩 %.2f；实际系数 %.2f。"),
            sample.policy.requested_color_strength, sample.policy.effective_color_strength);
        ImGui::TextWrapped("%s: %s", ui("Reason", "原因"), NrColorReasonName(sample.policy.color_reason));
        SettingsNote(ui("Recorded constants are not proof of GPU completion, presentation or a visible colour difference.",
            "这是已录制常量，不代表 GPU 已完成、已呈现或模型一定产生了可见色彩差异。"));
    }
    ImGui::Separator();
    ImGui::PopID();
}
