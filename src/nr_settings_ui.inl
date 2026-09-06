#include "nr_present_api_status.h"

// A named overlay is an independent ReShade-owned window. nullptr registers
// inside the add-on settings area, where long panels can displace other add-ons.
// Keep the name identical across languages, versions and both cleanup paths.
constexpr char kSettingsOverlayName[] = "DLSS5 NR Before SR";

void SettingsTooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void DrawColorControls(ChainConfig &next, bool has_pending, bool &changed) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    NrColorPolicySnapshot color_policy_snapshot = SnapshotColorPolicy();
    if (color_policy_snapshot.status != NrColorPolicyStatus::Bypassed &&
        (has_pending || changed || color_policy_snapshot.mode != next.mode))
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    const bool current_color_policy =
        color_policy_snapshot.status == NrColorPolicyStatus::Current;
    ImGui::BeginDisabled(current_color_policy &&
                         !color_policy_snapshot.policy.color_allowed);
    const bool color_follow_changed = ImGui::SliderFloat(
        ui("NR Color Follow", "NR 颜色跟随强度"),
        &next.color_strength, 0.0f, 1.0f, "%.2f");
    SettingsTooltip(ui("0 keeps the game's hue; 1 follows bounded NR chroma. New installations request 0.75. Unknown HDR pauses only colour transfer; luminance NR can remain active. Recorded values describe settings, not measured picture strength.",
        "0 保留游戏色相；1 跟随有界 NR 色彩。新装默认请求 0.75。未知 HDR 只暂停颜色传递，明暗 NR 仍可继续运行；记录值是参数设置，不代表测得的画面变化强度。"));
    ImGui::EndDisabled();
    changed |= color_follow_changed;
    if (color_follow_changed &&
        color_policy_snapshot.status != NrColorPolicyStatus::Bypassed)
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {
        const NrColorPolicy &policy = color_policy_snapshot.policy;
        ImGui::TextDisabled(ui("Last recorded blend: requested %.2f / effective %.2f",
            "最近记录的合成：请求 %.2f / 实际 %.2f"),
            static_cast<double>(policy.requested_color_strength),
            static_cast<double>(policy.effective_color_strength));
        ImGui::TextWrapped("%s", !policy.color_allowed
            ? ui("This HDR colour contract is unverified, so only colour follow is paused. "
                 "Luminance NR can still be active; check the running status and submission counter. "
                 "The saved colour request can resume on a supported contract.",
                 "当前 HDR 色彩合同未验证，只暂停颜色跟随；明暗 NR 仍可运行，请查看运行状态和提交计数。"
                 "颜色请求值已保留，切到受支持的合同后可恢复。")
            : policy.color_reason == NrColorReason::PostSrFp16UnitExposure
                ? ui("Colour follows the bounded FP16, unit-exposure SR -> NR contract.",
                     "当前为 FP16、单位曝光的 SR -> NR 合同，采用有界颜色跟随。")
                : ui("Colour follow is available for this SDR or bounded value-domain input.",
                     "当前 SDR 或有界值域输入允许颜色跟随。"));
    } else {
        ImGui::TextWrapped("%s", NrColorPolicyStatusText(
            color_policy_snapshot.status, use_chinese_ui));
    }
#if !NR_HAS_DETAIL_STABILITY
    ImGui::BeginDisabled(current_color_policy &&
        color_policy_snapshot.policy.lighting_reason == NrLightingReason::OnlyNrBeforeSr);
    const bool lighting_match_changed = ImGui::Checkbox(
        ui("Experimental Lighting Match (NR -> SR)",
           "实验性光照匹配（NR -> SR）"),
        &next.preserve_lighting);
    ImGui::EndDisabled();
    changed |= lighting_match_changed;
    if (lighting_match_changed &&
        color_policy_snapshot.status != NrColorPolicyStatus::Bypassed)
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {
        const NrColorPolicy &policy = color_policy_snapshot.policy;
        ImGui::TextDisabled(ui("Last recorded lighting match: requested %s / effective %s",
            "最近记录的光照匹配：请求 %s / 实际 %s"),
            policy.requested_preserve_lighting ? ui("on", "开") : ui("off", "关"),
            policy.effective_preserve_lighting ? ui("on", "开") : ui("off", "关"));
        if (policy.lighting_reason == NrLightingReason::OnlyNrBeforeSr)
            ImGui::TextWrapped("%s", ui(
                "SR -> NR does not apply Lighting Match. The request is saved for NR -> SR.",
                "SR -> NR 不采用光照匹配。请求值已保留，切回 NR -> SR 后可恢复。"));
    } else {
        ImGui::TextWrapped("%s", NrColorPolicyStatusText(
            color_policy_snapshot.status, use_chinese_ui));
    }
#endif
}

// The two maintained editions share the same small home screen. Technical
// readouts remain available without putting implementation knobs on the home.
void DrawSettings(reshade::api::effect_runtime *runtime) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    static ChainConfig pending = {};
    static bool has_pending = false;
    static unsigned pending_generation = 0;
    static bool edit_conflict = false;
    const auto current = SnapshotOverlayConfig();
    if (has_pending && pending_generation != current.generation) {
        has_pending = false; edit_conflict = true;
    }
    ChainConfig next = has_pending ? pending : current.config;
    bool changed = false;
    ImGui::SeparatorText(ui("DLSS 5 AI Rendering", "DLSS 5 AI 渲染"));
    ImGui::TextDisabled("%s | %s", kAddonVersion,
                        use_chinese_ui ? kAddonAuthor : kAddonAuthorAscii);
    ImGui::TextDisabled("%s", kOfficialReleaseUrl);
    if (kChineseUi && !use_chinese_ui)
        ImGui::TextWrapped("Chinese glyphs are unavailable in the current ReShade font, so this panel is using English instead of broken squares. In ReShade Settings > Overlay & Styling, choose zh-CN/System Default or C:\\Windows\\Fonts\\msyh.ttc, then restart the game.");
    const auto reason = static_cast<BypassReason>(g_last_bypass_reason.load());
    if (nr_present_api_status::IsD3d11PresentRuntime(runtime))
        ImGui::TextWrapped("%s", ui(
            "This ReShade window is using Direct3D 11, so DLSS 5 NR cannot run. Switch the game to Direct3D 12, then install ReShade Full Add-on for Direct3D 12.",
            "当前 ReShade 画面使用 Direct3D 11，本插件无法运行。请把游戏切换到 Direct3D 12，并为 D3D12 安装 ReShade Full Add-on。"));
    else if (!next.enabled || ActiveTransferStrength(next) <= 0.000001f)
        ImGui::TextWrapped("%s", ui("Native game SR. Enable the plugin and raise effect strength to compare NR.",
            "当前为游戏原生 SR。启用插件并提高效果强度，即可对比 NR。"));
    else if (reason == BypassReason::RayReconstructionGuard)
        ImGui::TextWrapped("%s", ui("Ray Reconstruction owns rendering. Turn RR off in game to compare this NR path.",
            "光线重构正在接管画面。如需对比本插件，请先在游戏内关闭 RR。"));
    else if (reason == BypassReason::ShortFuseOwnsNr)
        ImGui::TextWrapped("%s", ui("Another NR plugin is active. Disable its NR effect before using this one.",
            "另一个 NR 插件正在运行。请先关闭它的 NR 效果，再使用本插件。"));
    else if (reason == BypassReason::GenericRenoDxConflict)
        ImGui::TextWrapped("%s", ui(
            "Duplicate NR plugin detected: remove renodx-dlss5.addon64 and restart the game.",
            "检测到重复 NR 插件：请删除 renodx-dlss5.addon64，然后重启游戏。"));
    else if (reason == BypassReason::WaitingForFeature1 || reason == BypassReason::ModuleObserverInstalling)
        ImGui::TextWrapped("%s", ui("Waiting for native DLSS SR. Enter gameplay and enable DLSS in the game settings.",
            "等待游戏原生 DLSS SR。请进入实际场景，并在游戏设置里开启 DLSS。"));
    else if (reason == BypassReason::ModuleObserverUncertain) {
        ImGui::TextWrapped("%s", ui(
            "DLSS lifecycle observation is incomplete. NR stays off to protect SR/RR and any existing FG/MFG. F8 includes the first hook failure; changing NR strength cannot repair this.",
            "DLSS 生命周期观察不完整，NR 已安全旁路以保护原生 SR/RR 和现有 FG/MFG。F8 会保留首次 Hook 失败；调高强度不能解决此问题。"));
        const auto failure = SnapshotNgxHookFailure();
        if (failure.available)
            ImGui::TextWrapped(ui("First hook failure (historical): %s / %s", "首次 Hook 失败（历史记录）：%s / %s"),
                failure.operation, failure.entry);
    }
    else if (reason == BypassReason::CommandStateUnverified) {
        const auto evidence = SnapshotCommandStateEvidence();
        ImGui::TextWrapped("%s", ui(
            "The command list is not yet safe for NR insertion. Native SR is preserved. The last carrier decision below is an observation, not a compatibility guarantee.",
            "当前命令列表尚未满足 NR 安全插入条件，已保留原生 SR。下方是最近一次载体判断，不代表已通过游戏兼容验证。"));
        if (evidence.available)
            ImGui::TextWrapped(ui("Last reason: %s | guard requested/covered/reset: %d/%d/%d | age: %llu ms",
                "最近原因：%s | 观察请求/覆盖/新重置：%d/%d/%d | 距今：%llu 毫秒"),
                evidence.reason, evidence.guard_requested ? 1 : 0, evidence.guard_covered ? 1 : 0,
                evidence.fresh_reset ? 1 : 0, GetTickCount64() - evidence.sampled_at);
        ImGui::TextWrapped("%s", ui("Use F8 for the structured state report; do not disable safety guards.",
            "按 F8 获取结构化状态报告；请勿通过关闭安全保护强行启用。"));
    }
    else if (reason != BypassReason::None)
        ImGui::TextWrapped(ui("NR is temporarily skipped (%s). Keep the scene and collect feedback with F8.",
            "NR 当前已跳过（%s）。请保留问题场景，按 F8 收集反馈。"), BypassReasonName(reason));
    else
        ImGui::TextWrapped("%s", ui("NR path available. Compare the picture with F6; submitted work is shown in diagnostics.",
            "NR 链路可用。按 F6 对比画面，实际提交状态可在诊断中查看。"));
    if (next.identity_use_original)
        ImGui::TextWrapped("%s", ui(
            "Diagnostic identity mode is ON: NR may run, but its picture is deliberately discarded. Turn off 'Run NR but discard its picture' to compare the effect.",
            "诊断原图模式已开启：NR 可以运行，但效果会被主动丢弃。请关闭兼容性选项中的“运行 NR 但丢弃其画面”再对比。"));
    if (g_config_persistence_failed.load())
        ImGui::TextWrapped("%s", ui("Settings cannot be saved. Check that the plugin folder is writable.",
            "设置无法保存，请检查插件目录是否可写。当前设置仍用于本次运行。"));

    if (ImGui::BeginTabBar("nr-main-tabs")) {
    if (ImGui::BeginTabItem(ui("Picture###nr-picture", "画质###nr-picture"))) {
#if defined(NR_HAS_F8_SEQUENCE)
    const bool collecting = nr_f8::IsActive();
    if (collecting) ImGui::BeginDisabled();
#endif
    changed |= ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);
    int mode = next.mode == 2 ? 0 : 1;
    if (ImGui::Combo(ui("Processing mode", "处理模式"), &mode,
            ui("Before SR: NR -> SR\0After SR: SR -> NR\0",
               "前置 NR → SR\0后置 SR → NR\0"))) {
        next.mode = mode == 0 ? 2 : 1; changed = true;
    }
    SettingsTooltip(ui("Before SR usually costs less; after SR processes the complete SR output for comparison. Game DLSS quality stays unchanged. Disabling restores native SR; DLAA is never forced.",
        "前置通常较省性能；后置处理完整 SR 输出，适合对照。游戏内 DLSS 档位不变，关闭插件就是原生 SR，不会强制开启 DLAA。"));
#if NR_HAS_DETAIL_STABILITY
    constexpr float maximum_strength = 4.0f;
#else
    constexpr float maximum_strength = 2.0f;
#endif
    float effect_strength = ActiveTransferStrength(next);
    if (ImGui::SliderFloat(ui("Final effect strength", "最终效果强度"),
            &effect_strength, 0.0f, maximum_strength, "%.2f")) {
        next.transfer_strength = next.post_transfer_strength = effect_strength; changed = true;
    }
    SettingsTooltip(ui("Final NR blend strength. 0 keeps native SR; 1.0 is the unscaled effect. New installations start at 1.2. Existing settings, including 0, are retained. The public range is 0-4; older requests above 4 are clamped. Higher values can amplify low-resolution noise and colour distortion; this is not the model's internal strength.",
        "控制最终 NR 混合强度。0 保留原生 SR，1.0 是不额外放大的效果；新装默认 1.2。已有设置（包括 0）会保留。范围为 0–4，旧配置超过 4 会限制到 4。较高数值可能放大低分辨率噪点和颜色失真；这项不是模型内部强度。"));
    ImGui::BeginDisabled(next.mode != 2);
    changed |= ImGui::Checkbox(ui("High-strength protection (experimental)",
        "高强度保护（实验）"), &next.high_strength_protection);
    SettingsTooltip(ui("Before-SR only, above 1x effective strength (including enabled layers). Soft-limits the extra brightness/chroma edit; SDR brightening uses remaining source headroom. With layers off or neutral, 0-1x is unchanged; the full-resolution after-SR reference is always unchanged. Off restores the old amplification for A/B. This does not fix an already clipped 1x model result or guarantee stable pictures at the 4x public maximum.",
        "仅前置、有效强度高于 1 时生效（含分层增益）。对额外明暗/颜色增强做软限制，SDR 提亮参考原图剩余空间；分层关闭或为中性时，0-1 倍不变；完整后置参考始终不变。关闭可对比旧算法。不能修复 1 倍已经剪裁的模型结果，也不保证上限 4 倍画面稳定。"));
    ImGui::EndDisabled();
    if (ImGui::CollapsingHeader(ui("Layered composition (one NR)", "分层强化（单次 NR）"))) {
        ImGui::BeginDisabled(next.mode != 2);
        changed |= ImGui::Checkbox(ui("Enable layered composition", "启用分层强化"), &next.layered_enhancement);
        ImGui::BeginDisabled(!next.layered_enhancement);
        changed |= ImGui::SliderFloat(ui("Broad tone contribution", "大范围影调贡献"), &next.layer_tone, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Structure contribution", "局部结构贡献"), &next.layer_structure, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Brightening contribution", "提亮贡献"), &next.layer_bright, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Darkening contribution", "压暗贡献"), &next.layer_shadow, 0.f, 2.f, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::Button(ui("Reset only these layers", "仅恢复分层默认"))) {
            next.layered_enhancement = false;
            next.layer_tone = next.layer_structure = next.layer_bright = next.layer_shadow = 1.f;
            changed = true;
        }
        ImGui::TextWrapped("%s", ui("Experimental Before-SR composition, not repeated model inference. 1 is neutral. Combined luminance strength cannot exceed 4. Colour follow remains independent under HDR and colour. The split is not a face/material mask and cannot recover detail absent from the model. No extra NR pass; GPU cost still needs measurement.",
            "实验前置合成，不是多次运行模型。1 为中性，组合后的有效明暗强度最多 4。颜色跟随仍在 HDR 与颜色中独立控制。这不是人脸或材质遮罩，不能恢复模型没有的细节。不增加 NR 次数；实际 GPU 开销仍需测量。"));
        ImGui::EndDisabled();
    }
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
    SettingsTooltip(ui(
        "Available only for Before SR. This changes Feature 18's working resolution; it does not switch to post-processing and does not change the game's DLSS quality.",
        "仅用于前置 NR → SR。这里只改变 Feature 18 的工作分辨率，不会切成后置处理，也不会改变游戏内 DLSS 档位。"));
    if (next.work_mode == 5)
        changed |= ImGui::SliderFloat(ui("NR size ratio", "NR 尺寸比例"), &next.custom_work_scale, .5f, 1.f, "%.2f");
    if (next.mode != 2) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", ui("Post-processing mode uses the full SR output size.", "后置模式使用完整 SR 输出分辨率。"));
    }
#endif

    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {
        if (ImGui::Button(ui("Post-SR comparison", "后置对照设置"))) {
            next.mode = 1; next.enabled = true;
            next.transfer_strength = next.post_transfer_strength = 1.0f;
            next.color_strength = 1.0f; changed = true;
        }
        SettingsTooltip(ui("Sets SR -> NR with strength 1.0 and colour request 1.0 for comparison. These are comparison values, not the new installation defaults. Colour restrictions still apply.",
            "切到 SR → NR，采用强度 1.0、颜色请求 1.0 进行对照。这是对照基准，不是新装默认值；颜色准入限制仍然生效。"));
        changed |= ImGui::SliderFloat(ui("Model internal strength", "模型内部强度"), &next.intensity, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("This tunes the model itself, not the final effect multiplier. Saturation can limit the visible change.",
            "这项调整模型内部行为，不等于最终效果倍率；模型饱和时，继续提高也可能看不出变化。"));
        changed |= ImGui::SliderFloat(ui("Local tone", "局部影调"), &next.local_tone, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Changes local contrast inside the NR model. It is separate from final blend strength.", "调整 NR 模型内部的局部明暗对比，与最终混合强度不同。"));
        changed |= ImGui::SliderFloat(ui("Local structure", "局部结构"), &next.local_structure, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Adjusts the model local structure parameter; stronger values can amplify unstable fine detail.", "调整模型内部局部结构参数；过高也可能放大不稳定细节。"));
        if (ImGui::Checkbox(ui("Face mask", "人脸遮罩"), &next.auto_mask)) {
            if (next.auto_mask && next.skin_structure < 0.f)
                next.skin_structure = 0.f;
            changed = true;
        }
        SettingsTooltip(ui("Requests the model automatic face-mask parameter, not a mask generated by this plugin. Its effect depends on the model and input. Off sends skin strength -1 to follow Local structure; the last independent value is retained for re-enabling.", "请求模型内置的自动人脸遮罩，并非插件额外生成人脸识别图；效果取决于模型和输入。关闭时向模型发送皮肤强度 -1，跟随局部结构；上次独立设置会保留，重新开启时恢复。"));
        if (next.auto_mask) {
            changed |= ImGui::SliderFloat(ui("Skin detail strength", "皮肤细节强度"), &next.skin_structure, 0.f, 2.f, "%.2f");
            SettingsTooltip(ui("Independent skin structure request while Face mask is enabled. Legacy negative values start at 0; existing nonnegative values are retained. This is not guaranteed face detection or a measured picture-strength value.", "仅在人脸遮罩开启时请求独立皮肤结构强度。旧版负值从 0 开始，已有非负值保留；这不保证人脸识别成功，数值也不代表测得的画面效果强度。"));
        } else {
            ImGui::TextDisabled("%s", ui("Enable Face mask to adjust skin detail; currently follows Local structure.", "开启人脸遮罩后可调整皮肤细节；当前跟随局部结构。"));
        }
        changed |= ImGui::Checkbox(ui("UI correction", "UI 校正"), &next.ui_correction);
        ImGui::TextDisabled("%s", ui("New installations: UI protection on; your saved choice is retained.", "新装默认开启：优先保护 UI；保留你已保存的选择。"));
        SettingsTooltip(ui("Sends the unsigned UI-correction value expected by DLSSNR. It can help only when the game supplies a separate UI layer; it cannot isolate subtitles or HUD already baked into SceneColor. New installations enable it; existing explicit settings are kept.", "按 DLSSNR 实际读取的无符号类型发送界面修正值。只有游戏提供独立 UI 层时才可能生效；已经混入 SceneColor 的字幕或 HUD 无法被单独识别。新装默认开启，已有明确设置会保留。"));
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
        DrawColorControls(next, has_pending, changed);
        ImGui::TextWrapped("%s", ui("Unknown HDR inputs remain luminance-only. The post-processing preset follows colour only when the live resource/exposure contract permits it.",
            "未知 HDR 输入仍只修改明暗。后置预设也只有在实际纹理和曝光条件允许时才跟随颜色。"));
        changed |= ImGui::Checkbox(ui("Experimental highlight mapping", "实验高光映射"), &next.experimental_peak_shoulder);
        changed |= ImGui::SliderFloat(ui("Maximum luminance ratio", "最大亮度倍率"), &next.max_luma_ratio, 1.f, 8.f, "%.2f");
#if NR_HAS_DETAIL_STABILITY
        int curve = static_cast<int>(next.proxy_curve);
        if (ImGui::Combo(ui("HDR mapping", "HDR 映射"), &curve,
                ui("Legacy shoulder\0Hybrid\0Reversible (experimental)\0", "旧版压肩\0混合\0可逆（实验）\0"))) {
            next.proxy_curve = static_cast<unsigned>(curve); changed = true;
        }
#endif
    }
    if (ImGui::CollapsingHeader(ui("Compatibility and developer options", "兼容性与开发者选项"))) {
        int jitter = static_cast<int>(next.jitter_mode);
        if (ImGui::Combo(ui("NR jitter input", "NR jitter 输入"), &jitter,
                ui("Do not forward\0Forward verified game pixel coordinates\0", "不传递\0传递已验证的游戏像素坐标\0"))) {
            next.jitter_mode = static_cast<NrJitterMode>(jitter); changed = true;
        }
        SettingsTooltip(ui("This is an experiment, not a confirmed jitter fix. Native SR jitter is unchanged. Rebuilds NR history when switched.",
            "此项用于实验，不代表 jitter 问题已修复；游戏原生 SR jitter 始终不变。切换后会重建 NR 历史。"));
        auto jitter_snapshot = SnapshotJitterStatus();
        using JitterStatus = nr_jitter_status::Status;
        if (jitter_snapshot.status != JitterStatus::Bypassed &&
            (has_pending || changed)) jitter_snapshot.status = JitterStatus::Pending;
        const char *jitter_text = ui("Jitter: no current successful NR binding; saved request retained.",
            "Jitter：暂无当前配置的成功 NR 绑定，已保留请求。");
        if (next.jitter_mode == NrJitterMode::Legacy)
            jitter_text = ui("Jitter: forwarding is disabled.", "Jitter：未开启传递。");
        else if (jitter_snapshot.status == JitterStatus::Bound)
            jitter_text = ui("Jitter: parameters written for a recent successful NR evaluation; model use is unverified.",
                "Jitter：近期成功 NR 调用已写入参数；模型是否使用仍未验证。");
        else if (jitter_snapshot.status == JitterStatus::Unknown)
            jitter_text = ui("Jitter: current game coordinates are missing or invalid.",
                "Jitter：当前游戏坐标缺失或无效。");
        else if (jitter_snapshot.status == JitterStatus::Bypassed)
            jitter_text = ui("Jitter: NR is currently bypassed.", "Jitter：当前 NR 已旁路。");
        else if (jitter_snapshot.status == JitterStatus::Pending)
            jitter_text = ui("Jitter: waiting for the requested settings to apply.", "Jitter：等待请求的设置生效。");
        ImGui::TextWrapped("%s", jitter_text);
        changed |= ImGui::Checkbox(ui("Retry original SR on failure", "失败时重试原始 SR"), &next.retry_original_on_sr_failure);
        changed |= ImGui::Checkbox(ui("Run NR but discard its picture", "运行 NR 但丢弃其画面（诊断）"), &next.identity_use_original);
        int warmup = static_cast<int>(next.warmup_after_create);
        if (ImGui::SliderInt(ui("Warm-up frames", "预热帧数"), &warmup, 0, 30)) {
            next.warmup_after_create = static_cast<unsigned>(warmup); changed = true;
        }
    }
#if defined(NR_HAS_F8_SEQUENCE)
    if (collecting) ImGui::EndDisabled();
#endif
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ui("Feedback###nr-feedback", "反馈与报告###nr-feedback"))) {
#if defined(NR_HAS_F8_SEQUENCE)
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
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ui("Diagnostics###nr-diagnostics", "诊断###nr-diagnostics"))) {
        ImGui::PushID("technical"); DrawTechnicalDiagnostics(runtime); ImGui::PopID();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    }
    if (changed) {
        pending = next; pending_generation = current.generation;
        has_pending = true; edit_conflict = false;
    }
    if (has_pending && !ImGui::IsAnyItemActive()) {
        const auto result = ApplyOverlayConfig(pending, pending_generation);
        if (result != OverlayApplyResult::Busy) has_pending = false;
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;
    }
    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已被快捷键、配置重载或采集恢复更新；旧编辑未覆盖新值，请在当前值上重新调整。"));
}
