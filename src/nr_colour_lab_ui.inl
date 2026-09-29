// Included where the existing settings helpers and the pending transaction
// are available. All writes go through that transaction, never directly to
// g_config; the regular Apply/Save path owns locking and INI persistence.
void DrawColourLabControls(ChainConfig &next, unsigned generation,
                           bool has_pending, bool &changed, bool &apply_discrete_edit) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::PushID("nr-colour-lab-m3b");
    ImGui::Separator();
    SettingsField(ui("Colour / structure recovery [M3b experiment]", "颜色与结构实验"));
    int mode = static_cast<int>(nr_colour_lab::Normalize(next.colour_lab_mode));
    if (ImGui::Combo(ui("Colour policy###colour-lab-mode", "颜色策略###colour-lab-mode"), &mode,
        ui("Legacy: original configured policy\0Colour priority [experimental]\0Conservative: limit unverified HDR\0",
           "原设置\0颜色优先（实验）\0保守 HDR\0"))) {
        next.colour_lab_mode = mode;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Changes only colour admission, not model settings, layer count, raster, exposure or resource validation. Legacy also retains the saved legacy HDR opt-in. The recovery selector below is a separate output-only choice.",
        "仅改变 AI 色彩准入，不改模型、层数、分辨率、曝光或资源安全检查。旧版仍遵守已保存的旧 HDR 实验许可。下方恢复算法是独立的输出合成选项。"));
    int recovery = nr_colour_restore::Normalize(next.colour_restore_mode);
    if (ImGui::Combo(ui("Recovery###colour-restore", "恢复算法###colour-restore"), &recovery,
        ui("D21 baseline composition\0Matched RGB + D21 protections\0Raw model RGB edit reference [diagnostic]\0",
           "D21 基准\0RGB 恢复\0原始 RGB（诊断）\0"))) {
        next.colour_restore_mode = recovery;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Mode 1 keeps D21 lighting/detail/edge/region/face attenuation. Mode 2 is a diagnostic raw model-edit reference: it bypasses those aesthetic attenuations but keeps finite, gamut, ratio and GPU/resource safety. Neither adds an NR pass.",
        "模式 1 保留 D21 的光照/细节/边缘/区域/人脸衰减；模式 2 是诊断用原始模型修改参考，会绕过这些审美衰减，但仍保留有限值、色域、倍率及 GPU/资源安全。两者都不增加 NR 推理。"));
    if (recovery == 1 || recovery == 2) {
        SettingsNote(ui(
            "RGB replaces the final ratio/chroma consumer, not the input curve. Saved independent-chroma/opponent options are retained for baseline comparison; any existing low-grid luminance filtering remains active.",
            "RGB 替换最终倍率/色度合成，不替换输入曲线。原独立色度/低网格颜色偏好保留给基线对照；已有低网格明暗过滤仍会生效。"));
        SettingsNote(ui(
            "Negative source channels or an invalid recovery keep the original pixel. On bypass, no visible change, hue/highlight errors or flicker: collect F8 and submit a BUG with the recovery selection.",
            "原图负值通道或恢复数据异常时保留原像素。发生旁路、无变化、偏色、高光异常或闪烁，请按 F8 收集并提交 BUG，注明恢复算法。"));
    }
    if (recovery == 2)
        SettingsNote(ui(
            "Raw reference ignores output Transfer/LightingLock/DetailStability/EdgeGuard/region/face damping by design. Use it to see whether the model already created the missing character/fabric structure; it is not a recommended default.",
            "原始参考会刻意绕过输出端的整体效果/光照保护/细节稳定/边缘/区域/人脸衰减，用来判断模型是否已经生成缺失的人物/衣物结构；它不是推荐默认值。"));
    int inputCurve = nr_colour_restore::Normalize(next.colour_input_curve);
    if (ImGui::Combo(ui("HDR input curve###colour-input-curve", "HDR 输入曲线###colour-input-curve"), &inputCurve,
        ui("D21 input unchanged\0Paired N2 HDR [experimental; resets NR history]\0",
           "D21 原输入\0N2 HDR（实验）\0"))) {
        next.colour_input_curve = inputCurve;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Optional second comparison axis. Before and after SR/RR use the same bounded N2 HDR curve, and RGB recovery uses the original source's scale. SDR and unresolved model-value inputs stay unchanged. Switching input preparation recreates NR history; wait for temporal settling before comparing. Default is D21 input unchanged.",
        "第二条独立对照轴。前、后置 HDR 使用同一条有界 N2 曲线，RGB 恢复仍参考原图尺度。SDR 与未确认的模型值域保持原样。切输入准备会重建 NR 历史，请在时域稳定后对比；默认仍保留 D21 原输入。"));
    int scaledPrep = nr_colour_restore::NormalizeScaledProxy(next.colour_scaled_proxy_mode);
    if (ImGui::Combo(ui("Sub-native NR input###colour-scaled-proxy", "低于 100% 的 NR 输入###colour-scaled-proxy"), &scaledPrep,
        ui("D21: scale source, then encode\0Proxy-first exact-area [99/100 diagnostic]\0",
           "先缩放\0先编码（诊断）\0"))) {
        next.colour_scaled_proxy_mode = scaledPrep;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Pre-SR exact-area path only. The experimental mode prepares each source tap into the model proxy first, then averages that proxy. It targets the observed 99% vs 100% colour/brightness cliff and mirrors Opti's encode-then-downsample topology. Switching it resets NR history. Lanczos keeps the D21 path and reports it as not applied.",
        "仅用于前置 NR 的精确面积缩放。实验模式先把每个源采样编码为模型 Proxy，再对 Proxy 做面积平均，专门定位 99% 与 100% 的颜色/亮度断层，并与 Opti 的“先编码再缩放”拓扑对齐。切换会重建 NR 历史；Lanczos 暂时保留 D21 路径并显示未应用。"));
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
        next.colour_restore_mode = 0;
        next.colour_input_curve = 0;
        next.colour_scaled_proxy_mode = 0;
        changed = true;
        apply_discrete_edit = true;
    }
    SettingsNote(ui("AI colour and original-lighting sliders keep their values. Recovery selection does not reset NR history; changing the input curve does.",
        "AI 色彩、原始光照保护等滑块保留原值；切恢复算法不重建 NR 历史，切输入曲线会重建。"));

    const auto sample = SnapshotColorPolicy();
    // A pending UI edit must not label the preceding dispatch as its result.
    if (changed || has_pending || sample.config_generation != generation) {
        SettingsNote(ui("Candidate selected; waiting for the normal settings transaction and a matching composition sample.",
            "准备中：等待处理记录。"));
    } else if (sample.status != NrColorPolicyStatus::Current) {
        SettingsNote(NrColorPolicyStatusText(sample.status, zh));
        if (sample.status == NrColorPolicyStatus::Bypassed)
            SettingsNote(ui("The native path is retained. Collect F8 and submit a BUG before changing compatibility settings.",
                "未生效：保留游戏原画面。"));
    } else {
        ImGui::TextWrapped(ui("Recent recorded combine: requested colour %.2f; effective coefficient %.2f.",
                             "色彩记录：请求 %.2f／录制 %.2f"),
            sample.policy.requested_color_strength, sample.policy.effective_color_strength);
        ImGui::TextWrapped("%s: %s", ui("Reason", "原因"), NrColorReasonName(sample.policy.color_reason));
        ImGui::TextWrapped("%s: %s", ui("Recorded recovery", "已录制恢复算法"),
            nr_colour_restore::Name(sample.policy.recorded_restore_mode));
        ImGui::TextWrapped("%s: %s", ui("Recorded input curve", "已录制输入曲线"),
            nr_colour_restore::CurveName(sample.policy.recorded_input_curve));
        ImGui::TextWrapped("%s: %s", ui("Recorded sub-native preparation", "已录制低分辨率输入准备"),
            nr_colour_restore::ScaledProxyName(sample.policy.recorded_scaled_proxy_mode));
        if (sample.policy.requested_scaled_proxy_mode == 1 && sample.policy.recorded_scaled_proxy_mode == 0)
            SettingsNote(ui(
                "Proxy-first scaling was not applied to the recorded input (100% work size, post path, Lanczos, or missing producer receipt).",
                "本次记录未应用“先编码 Proxy 再缩放”（可能为 100% 工作尺寸、后置路径、Lanczos，或缺少匹配的输入记录）。"));
        if (sample.policy.requested_input_curve == 1 && sample.policy.recorded_input_curve == 0 && sample.policy.restore_encoding_known)
            SettingsNote(ui("The current bounded SDR/model-value contract retains its original input; HDR N2 is not applied.",
                "当前为有界 SDR/模型值域合同，保留原输入，未应用 HDR N2。"));
        if ((sample.policy.requested_restore_mode == 1 || sample.policy.requested_input_curve == 1) && !sample.policy.restore_encoding_known)
            SettingsNote(ui("No matching input-encoder record for this slot/frame; baseline composition retained. Collect F8 and submit a BUG.",
                "该槽位/帧没有匹配的输入编码记录，已保留基线合成；请按 F8 收集并提交 BUG。"));
        if (sample.policy.recorded_restore_mode == 1)
            ImGui::TextWrapped("%s", sample.policy.restore_post_encoding
                ? ui("Source reference: post-SR/RR EncodeCS", "原图参考：后置 SR/RR EncodeCS")
                : ui("Source reference: pre-SR EncodeV2CS", "原图参考：前置 EncodeV2CS"));
        SettingsNote(ui("Recorded constants are not proof of GPU completion, presentation or a visible colour difference.",
            "仅处理记录，画质尚未验证。"));
    }
    ImGui::Separator();
    ImGui::PopID();
}
