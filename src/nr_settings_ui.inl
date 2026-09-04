// The two maintained editions share the same small home screen. Technical
// readouts remain available without putting implementation knobs on the home.
void DrawSettings(reshade::api::effect_runtime *runtime) {
    const auto ui = [](const char *en, const char *zh) { return kChineseUi ? zh : en; };
    static ChainConfig pending = {};
    static bool has_pending = false;
    ChainConfig next = has_pending ? pending : SnapshotConfig();
    bool changed = false;
    ImGui::SeparatorText(ui("DLSS 5 AI Rendering", "DLSS 5 AI 渲染"));
    ImGui::TextDisabled("%s | %s", kAddonVersion, kAddonAuthor);
    ImGui::TextDisabled("%s", kOfficialReleaseUrl);
    const auto reason = static_cast<BypassReason>(g_last_bypass_reason.load());
    if (!next.enabled || ActiveTransferStrength(next) <= 0.000001f)
        ImGui::TextWrapped("%s", ui("Native game SR. Enable the plugin and raise effect strength to compare NR.",
            "当前为游戏原生 SR。启用插件并提高效果强度，即可对比 NR。"));
    else if (reason == BypassReason::RayReconstructionGuard)
        ImGui::TextWrapped("%s", ui("Ray Reconstruction owns rendering. Turn RR off in game to compare this NR path.",
            "光线重构正在接管画面。如需对比本插件，请先在游戏内关闭 RR。"));
    else if (reason == BypassReason::ShortFuseOwnsNr)
        ImGui::TextWrapped("%s", ui("Another NR plugin is active. Disable its NR effect before using this one.",
            "另一个 NR 插件正在运行。请先关闭它的 NR 效果，再使用本插件。"));
    else if (reason == BypassReason::WaitingForFeature1 || reason == BypassReason::ModuleObserverInstalling)
        ImGui::TextWrapped("%s", ui("Waiting for native DLSS SR. Enter gameplay and enable DLSS in the game settings.",
            "等待游戏原生 DLSS SR。请进入实际场景，并在游戏设置里开启 DLSS。"));
    else if (reason != BypassReason::None)
        ImGui::TextWrapped(ui("NR is temporarily skipped (%s). Keep the scene and collect feedback with F8.",
            "NR 当前已跳过（%s）。请保留问题场景，按 F8 收集反馈。"), BypassReasonName(reason));
    else
        ImGui::TextWrapped("%s", ui("NR path available. Compare the picture with F6; submitted work is shown in diagnostics.",
            "NR 链路可用。按 F6 对比画面，实际提交状态可在诊断中查看。"));
    if (g_config_persistence_failed.load())
        ImGui::TextWrapped("%s", ui("Settings cannot be saved. Check that the plugin folder is writable.",
            "设置无法保存，请检查插件目录是否可写。当前设置仍用于本次运行。"));

#if defined(NR_HAS_F8_SEQUENCE)
    const bool collecting = nr_f8::IsActive();
    if (collecting) ImGui::BeginDisabled();
#endif
    changed |= ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);
    int mode = next.mode == 2 ? 0 : 1;
    if (ImGui::Combo(ui("Processing mode", "处理模式"), &mode,
            ui("Performance: NR -> SR\0Post-processing quality: SR -> NR\0",
               "性能优先：NR → SR\0后置高画质：SR → NR\0"))) {
        next.mode = mode == 0 ? 2 : 1; changed = true;
    }
    ImGui::TextWrapped("%s", ui("Game DLSS quality stays unchanged. Disabling this plugin restores native SR; DLAA is never forced.",
        "游戏内 DLSS 档位保持不变；关闭插件就是原生 SR，不会强制开启 DLAA。"));
#if NR_HAS_DETAIL_STABILITY
    constexpr float maximum_strength = 8.0f;
#else
    constexpr float maximum_strength = 2.0f;
#endif
    if (ImGui::SliderFloat(ui("Final effect strength", "最终效果强度"),
            &next.transfer_strength, 0.0f, maximum_strength, "%.2f")) {
        next.post_transfer_strength = next.transfer_strength; changed = true;
    }
    if (ImGui::Button(ui("Standard 1.0", "标准 1.0"))) {
        next.transfer_strength = next.post_transfer_strength = 1.0f; changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ui("Enhanced 1.2", "增强 1.2"))) {
        next.transfer_strength = next.post_transfer_strength = 1.2f; changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ui("Post-processing preset", "后置高画质预设"))) {
        next.mode = 1; next.enabled = true;
        next.transfer_strength = next.post_transfer_strength = 1.0f;
        next.color_strength = 1.0f; changed = true;
    }
    ImGui::TextDisabled("%s", ui("1.0 is the real default scale. More strength can expose noise in low-resolution samples.",
        "1.0 保持真实默认标尺；采样较低时，提高强度也可能放大颗粒。"));
    int style = static_cast<int>(next.style);
    if (ImGui::Combo(ui("Style", "风格"), &style,
            ui("Default\0Natural\0Cinematic\0", "默认\0自然\0电影\0"))) {
        next.style = static_cast<unsigned>(style); changed = true;
    }
#if NR_HAS_DETAIL_STABILITY
    if (next.mode != 2) ImGui::BeginDisabled();
    changed |= ImGui::Combo(ui("NR quality", "NR 画质档位"), &next.work_mode,
        ui("Reference 100%\0Quality\0Balanced\0Performance\0Ultra performance\0Custom\0",
           "参考 100%\0高画质\0平衡\0性能\0超级性能\0自定义\0"));
    if (next.work_mode == 5)
        changed |= ImGui::SliderFloat(ui("NR size ratio", "NR 尺寸比例"), &next.custom_work_scale, .5f, 1.f, "%.2f");
    if (next.mode != 2) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", ui("Post-processing mode uses the full SR output size.", "后置模式使用完整 SR 输出分辨率。"));
    }
#endif

    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {
        changed |= ImGui::SliderFloat(ui("Model internal strength", "模型内部强度"), &next.intensity, 0.f, 2.f, "%.2f");
        ImGui::TextWrapped("%s", ui("This tunes the model itself, not the final effect multiplier. Saturation can limit the visible change.",
            "这项调整模型内部行为，不等于最终效果倍率；模型饱和时，继续提高也可能看不出变化。"));
        changed |= ImGui::SliderFloat(ui("Local tone", "局部影调"), &next.local_tone, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Local structure", "局部结构"), &next.local_structure, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Skin structure (-1 follows local)", "皮肤结构（-1 跟随局部）"), &next.skin_structure, -1.f, 2.f, "%.2f");
        changed |= ImGui::Checkbox(ui("Model automatic mask", "模型自动遮罩参数"), &next.auto_mask);
        changed |= ImGui::Checkbox(ui("Model UI correction", "模型界面修正参数"), &next.ui_correction);
        ImGui::TextWrapped("%s", ui("These are existing model parameters. Separate face or HUD region recognition is unavailable.",
            "以上调用已有模型参数；当前没有独立的人脸或 HUD 区域识别。"));
#if NR_HAS_DETAIL_STABILITY
        if (next.mode != 2) ImGui::BeginDisabled();
        changed |= ImGui::SliderFloat(ui("Detail stability (experimental)", "细节稳定保护（实验）"), &next.detail_stability, 0.f, 1.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Original lighting protection", "原始光照保护"), &next.lighting_lock, 0.f, 1.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Edge protection", "边缘保护"), &next.edge_guard, 0.f, 1.f, "%.2f");
        if (next.mode != 2) ImGui::EndDisabled();
        ImGui::TextWrapped("%s", ui("Detail stability defaults off and limits inconsistent fine NR changes. Available only before SR. Motion protection is unavailable: same-frame depth history has not been established.",
            "细节稳定保护默认关闭，只限制细碎 NR 变化，仅前置模式可用。运动保护不可用：尚未建立可靠的同帧深度历史。"));
#endif
    }
    if (ImGui::CollapsingHeader(ui("HDR and colour", "HDR 与颜色"))) {
        changed |= ImGui::SliderFloat(ui("Colour follow", "颜色跟随"), &next.color_strength, 0.f, 1.f, "%.2f");
        ImGui::TextWrapped("%s", ui("Unknown HDR inputs remain luminance-only. The post-processing preset follows colour only when the live resource/exposure contract permits it.",
            "未知 HDR 输入仍只修改明暗。后置预设也只有在实际纹理和曝光条件允许时才跟随颜色。"));
        changed |= ImGui::Checkbox(ui("Experimental highlight mapping", "实验高光映射"), &next.experimental_peak_shoulder);
        changed |= ImGui::SliderFloat(ui("Maximum luminance ratio", "最大亮度倍率"), &next.max_luma_ratio, 1.f, 16.f, "%.2f");
#if NR_HAS_DETAIL_STABILITY
        int curve = static_cast<int>(next.proxy_curve);
        if (ImGui::Combo(ui("HDR mapping", "HDR 映射"), &curve,
                ui("Legacy shoulder\0Hybrid\0Reversible (experimental)\0", "旧版压肩\0混合\0可逆（实验）\0"))) {
            next.proxy_curve = static_cast<unsigned>(curve); changed = true;
        }
#else
        changed |= ImGui::Checkbox(ui("Match original lighting", "匹配原始光照"), &next.preserve_lighting);
#endif
    }
    if (ImGui::CollapsingHeader(ui("Compatibility and developer options", "兼容性与开发者选项"))) {
        int jitter = static_cast<int>(next.jitter_mode);
        if (ImGui::Combo(ui("NR jitter input", "NR jitter 输入"), &jitter,
                ui("Do not forward\0Forward verified game pixel coordinates\0", "不传递\0传递已验证的游戏像素坐标\0"))) {
            next.jitter_mode = static_cast<NrJitterMode>(jitter); changed = true;
        }
        ImGui::TextWrapped("%s", ui("This is an experiment, not a confirmed jitter fix. Native SR jitter is unchanged. Rebuilds NR history when switched.",
            "此项用于实验，不代表 jitter 问题已修复；游戏原生 SR jitter 始终不变。切换后会重建 NR 历史。"));
        changed |= ImGui::Checkbox(ui("Retry original SR on failure", "失败时重试原始 SR"), &next.retry_original_on_sr_failure);
        changed |= ImGui::Checkbox(ui("Run NR but discard its picture", "运行 NR 但丢弃其画面（诊断）"), &next.identity_use_original);
        int warmup = static_cast<int>(next.warmup_after_create);
        if (ImGui::SliderInt(ui("Warm-up frames", "预热帧数"), &warmup, 0, 30)) {
            next.warmup_after_create = static_cast<unsigned>(warmup); changed = true;
        }
    }
#if defined(NR_HAS_F8_SEQUENCE)
    if (collecting) ImGui::EndDisabled();
    nr_f8::DrawControls();
#else
    ImGui::Separator();
    if (ImGui::Button(ui("Collect current issue (F8)", "采集当前问题（F8）"))) RequestFeedbackBundle();
    ImGui::TextWrapped("%s", ui("F8 creates a local ZIP. F7 captures one set of stages; '-' captures the displayed picture. No automatic upload.",
        "F8 生成本地反馈 ZIP；F7 采集一组阶段数据，“-”截取显示画面，不自动上传。"));
    const auto status = g_feedback_status.load();
    if (status == 1 || status == 2) ImGui::TextDisabled("%s", ui("Collecting / packaging...", "正在采集 / 打包……"));
    if (status >= 3) {
        char path[4096] = {};
        AcquireSRWLockShared(&g_feedback_lock);
        WideToUtf8(g_feedback_last_zip, path, sizeof(path));
        ReleaseSRWLockShared(&g_feedback_lock);
        ImGui::TextWrapped("%s", path);
    }
#endif
    if (ImGui::CollapsingHeader(ui("Diagnostics", "诊断"))) {
        ImGui::PushID("technical"); DrawTechnicalDiagnostics(runtime); ImGui::PopID();
    }
    if (changed) { pending = next; has_pending = true; }
    if (has_pending && !ImGui::IsAnyItemActive()) {
        ApplyConfig(pending, "ReShade simplified settings"); has_pending = false;
    }
}
