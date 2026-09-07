#include "nr_present_api_status.h"
#include "nr_recommended_picture.inl"
#include "nr_precision_ui.h"

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
    const bool color_follow_changed = ImGui::SliderFloat(
        ui("Colour enhancement", "色彩强化"),
        &next.color_strength, 0.0f, 1.0f, "%.2f");
    SettingsTooltip(ui("0 keeps the game's hue; 1 follows bounded NR chroma. New installations request 1.0. The saved request may differ from the effective coefficient. Editing a nonzero value explicitly permits unverified HDR colour with a warning; zero restores luminance only. This does not override resource or exposure safety. Recorded values describe settings, not measured picture strength.",
        "0 保留游戏色相；1 跟随有界 NR 色彩。新装默认请求 1.0。保存请求与实际合成系数可能不同。手动调到非零即允许尝试未验证 HDR 色彩，可能偏色；拉回 0 恢复仅明暗，不绕过资源或曝光安全检查。记录值是参数设置，不代表测得的画面变化强度。"));
    if (color_follow_changed) next.allow_unverified_hdr_color = next.color_strength > 0.0f;
    changed |= color_follow_changed;
    if (color_follow_changed &&
        color_policy_snapshot.status != NrColorPolicyStatus::Bypassed)
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    if (next.allow_unverified_hdr_color || (current_color_policy && !color_policy_snapshot.policy.color_allowed))
        ImGui::TextWrapped("%s", ui("WARNING: unverified HDR colour may shift hue or flicker. Set colour to 0 or restore recommended settings to undo.", "警告：未验证 HDR 色彩可能偏色或闪烁；可拉回 0 或一键恢复推荐画质。"));
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {
        const NrColorPolicy &policy = color_policy_snapshot.policy;
        ImGui::TextDisabled(ui("Last recorded colour: requested %.2f / shader coefficient %.2f",
            "最近颜色记录：请求 %.2f / 合成系数 %.2f"),
            static_cast<double>(policy.requested_color_strength),
            static_cast<double>(policy.effective_color_strength));
        if (policy.amplification_color_limited) ImGui::TextWrapped("%s", ui(
            "Mandatory colour protection is active above 1x. Pre-SR caps the chroma exponent at its unamplified budget; post-SR reduces the amplified colour mix. Brightness amplification is unchanged. The coefficient is not the measured visible effect.",
            "超过 1 倍的强制颜色保护已生效：前置限制色度指数的额外放大，后置降低放大后色彩的混合权重。明暗放大不变；合成系数不是实测画面效果强度。"));
        ImGui::TextWrapped("%s", !policy.color_allowed
            ? ui("This HDR colour contract is unverified, so only colour follow is paused. "
                 "Luminance NR can still be active; check the running status and submission counter. "
                 "The saved colour request can resume on a supported contract.",
                 "当前 HDR 色彩合同未验证，只暂停颜色跟随；明暗 NR 仍可运行，请查看运行状态和提交计数。"
                 "颜色请求值已保留，切到受支持的合同后可恢复。")
            : policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr
                ? ui("User-requested colour on an unverified HDR contract; not a verified colour profile.", "用户已选择尝试未验证 HDR 颜色；这不代表色彩合同已验证。")
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
// ImGui can be drawn by more than one swapchain; do not block another caller.
nr_ui_activity::Sample ReadUiActivity() {
    static SRWLOCK lock=SRWLOCK_INIT;
    static nr_ui_activity::Window window;
    if(!TryAcquireSRWLockExclusive(&lock)) return {};
    const auto result=window.Observe(GetTickCount64(),{g_carrier_evaluations.load(),
        g_nr_successful_evaluations.load(),g_nr_transfer_submitted_frames.load()});
    ReleaseSRWLockExclusive(&lock);
    return result;
}

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
    // Keep controls stationary while live status text changes (#121).
    ImGui::BeginChild("nr-live-status", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 7.0f), true);
    const auto activity=ReadUiActivity();
    if(activity.valid) {
        ImGui::TextWrapped(ui("Last %llu ms (process): SR %llu | NR %llu | submitted %llu",
            "最近 %llu 毫秒（进程）：SR %llu | NR %llu | 已提交 %llu"),
            static_cast<unsigned long long>(activity.elapsed_ms),
            static_cast<unsigned long long>(activity.delta.carriers),
            static_cast<unsigned long long>(activity.delta.nr),
            static_cast<unsigned long long>(activity.delta.submitted));
    } else ImGui::TextDisabled("%s",ui("Sampling actual activity...", "正在采样实际活动……"));
    SettingsTooltip(ui("Historical process-wide counter deltas, not FPS or proof of picture quality. Enable is your request; NR success and composition submission are separate stages. Raw per-carrier reasons remain in F8.",
        "这是历史进程计数增量，不是帧率或画质证明。开关是请求，NR 成功与合成提交是不同阶段；F8 仍保留真实的逐载体原因。"));
    const auto live_reason = static_cast<BypassReason>(g_last_bypass_reason.load(std::memory_order_acquire));
    if (live_reason == BypassReason::RuntimeUnavailable) {
        ImGui::TextWrapped("%s", ui("NR runtime could not load or initialize. Keep your NVIDIA DLLs; install the matching nrchain_nvngx.dll from this OTA, then restart. F8 TXT identifies the failed component and error.",
            "NR 运行库加载或初始化失败。保留显卡对应 NVIDIA DLL，使用本 OTA 配套 nrchain_nvngx.dll 后重启游戏；F8 TXT 会列出失败组件及错误。"));
    } else if (live_reason == BypassReason::CodecUnavailable) {
        ImGui::TextWrapped("%s", ui("The NR conversion/composition pipeline could not initialize. Native output is preserved; use F8 for the actual failure record.",
            "NR 转换/合成管线初始化失败，保留原生输出；请用 F8 记录具体失败。"));
    }
    const auto reason = static_cast<BypassReason>(g_last_bypass_reason.load());
    if (nr_present_api_status::IsD3d11PresentRuntime(runtime))
        ImGui::TextWrapped("%s", ui(
            "This ReShade window is using Direct3D 11, so DLSS 5 NR cannot run. Switch the game to Direct3D 12, then install ReShade Full Add-on for Direct3D 12.",
            "当前 ReShade 画面使用 Direct3D 11，本插件无法运行。请把游戏切换到 Direct3D 12，并为 D3D12 安装 ReShade Full Add-on。"));
    else if (!current.config.enabled || ActiveTransferStrength(current.config) <= 0.000001f)
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

    ImGui::EndChild();
    bool apply_discrete_edit = false;

    if (ImGui::BeginTabBar("nr-main-tabs")) {
    if (ImGui::BeginTabItem(ui("Picture###nr-picture", "画质###nr-picture"))) {
#if defined(NR_HAS_F8_SEQUENCE)
    const bool collecting = nr_f8::IsActive();
    if (collecting) ImGui::BeginDisabled();
#endif
    apply_discrete_edit = ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);
    changed |= apply_discrete_edit;
    if (ImGui::Button(ui("Restore recommended picture settings", "一键恢复推荐画质"))) {
        RestoreRecommendedPictureSettings(next);
        changed = true;
    }
    SettingsTooltip(ui(
        "Restores Before SR, strength amplification 1.2, model strength 1.0, colour request 1.0 and the shipped picture defaults. Exits diagnostic identity and disables unverified HDR colour/peak experiments. Keeps Enable (F6), depth/jitter/R8 game contracts and feedback preferences. Uses the normal save/history rules; runtime safety faults are not cleared.",
        "恢复前置 NR、强度放大 1.2、模型强度 1.0、颜色请求 1.0 等随包推荐画质；退出诊断原图，关闭未验证 HDR 调色与高光实验。保留 F6 总开关、深度/Jitter/R8 游戏兼容设置和反馈偏好；按正常规则保存及更新历史，不清除运行时安全故障。"));
    int mode = next.mode == 2 ? 0 : 1;
    if (ImGui::Combo(ui("Processing mode", "处理模式"), &mode,
            ui("Before SR: NR -> SR\0After SR: SR -> NR\0",
               "前置 NR → SR\0后置 SR → NR\0"))) {
        next.mode = mode == 0 ? 2 : 1; changed = true;
    }
    SettingsTooltip(ui("Before SR usually costs less; after SR processes the complete SR output for comparison. Game DLSS quality stays unchanged. Disabling restores native SR; DLAA is never forced.",
        "前置通常较省性能；后置处理完整 SR 输出，适合对照。游戏内 DLSS 档位不变，关闭插件就是原生 SR，不会强制开启 DLAA。"));
#if NR_HAS_DETAIL_STABILITY
    const bool before_sr = next.mode == 2;
    const bool legacy_preset = next.work_mode >= 1 && next.work_mode <= 4;
    // These atomics are historical observations, NOT a coherent frame tuple.
    // They only seed a visibly labelled estimate; no config is migrated until
    // the user edits the slider. The GPU resolver still checks the real tuple.
    float precision = nr_precision_ui::LegacyEstimate(next.work_mode, next.custom_work_scale,
        g_last_dlss_input_width.load(), g_last_dlss_input_height.load(),
        g_last_dlss_output_width.load(), g_last_dlss_output_height.load());
    static bool precision_rejected = false;
    if (!before_sr) ImGui::BeginDisabled();
    if (ImGui::SliderFloat(ui("NR processing precision (%)", "NR 处理精度（%）"),
            &precision, 0.0f, 100.0f, "%.1f%%")) {
        const auto request = nr_precision_ui::Resolve(precision, before_sr);
        precision_rejected = !request.accepted;
        if (request.accepted) {
            if (request.disable) {
                next.enabled = false; // F6 can re-enable the previous precision.
                apply_discrete_edit = true;
            } else {
                next.work_mode = request.mode;
                next.custom_work_scale = request.scale;
            }
            changed = true;
        }
    }
    SettingsTooltip(ui(
        "100% follows the game render input exactly (N=R). 50-99% reduces only NR work, reconstructs edits to R, then uses native SR. 0 turns NR off and retains its previous precision. This build rejects 1-49%; it never silently runs 50% for a smaller request. Moving precision does not turn Enable on.",
        "100% 精确跟随游戏渲染输入（N=R）；50–99% 只降低 NR 工作尺寸，修改回填 R 后交给原生 SR。0 关闭 NR 并保留先前精度，F6 可恢复。本版明确不支持 1–49%，不会暗中按 50% 运行；调整精度不会自动开启总开关。"));
    if (!before_sr) {
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", ui(
            "Post-SR still uses the full SR output. Reduced post-SR precision is not implemented in this build; the disabled value is your saved pre-SR request, not an applied post-SR scale.",
            "后置仍使用完整 SR 输出。本版尚未实现低精度后置；灰色数值仅为保存的前置偏好，不代表后置实际比例。"));
    } else {
        ImGui::TextDisabled("%s", ui("Supported: 0 = NR off; 50-100% = NR precision.", "可用范围：0 = 关闭 NR；50–100% = NR 处理精度。"));
        if (legacy_preset) ImGui::TextWrapped("%s", ui(
            "Existing output-relative preset retained. The slider is a historical estimate until edited; opening this panel does not change your settings. Select 100% for exact N=R.",
            "保留已有输出相关预设；滑块未编辑时只是历史尺寸估算，打开面板不会改设置。手动选择 100% 即精确 N=R。"));
    }
    if (precision_rejected) ImGui::TextWrapped("%s", ui(
        "That precision is unsupported; the previous request is unchanged. Choose 50-100%, or 0 to switch NR off.",
        "该精度暂不支持，原设置未更改。请选择 50–100%，或用 0 关闭 NR。"));
#endif
    int style = static_cast<int>(next.style);
    if (ImGui::Combo(ui("Style", "风格"), &style,
            ui("Default\0Natural\0Cinematic\0", "默认\0自然\0电影\0"))) {
        next.style = static_cast<unsigned>(style); changed = true;
    }

        changed |= ImGui::SliderFloat(ui("Model strength", "模型强度"), &next.intensity, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("This tunes the model itself, not the final effect multiplier. Saturation can limit the visible change.",
            "这项调整模型内部行为，不等于强度放大。默认 1.0，保留已有值；模型参数提交后需要重建/预热，短暂原图不是人脸识别成功的证明。"));
        if (next.intensity != 1.0f) {
            ImGui::TextDisabled("%s", ui("Model default is 1.0; the value above is your saved setting.",
                "模型默认是 1.0；上方显示的是已保存的设置。"));
            if (ImGui::Button(ui("Restore model strength 1.0 only###nr-model-default", "仅恢复模型强度 1.0###nr-model-default"))) {
                next.intensity = 1.0f;
                changed = true;
                apply_discrete_edit = true;
            }
        }
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
        changed |= ImGui::Checkbox(ui("UI correction", "UI 结构保护"), &next.ui_correction);
        ImGui::TextDisabled("%s", ui("New installations: UI protection on; your saved choice is retained.", "新装默认开启：优先保护 UI；保留你已保存的选择。"));
        SettingsTooltip(ui("Requests the model UI-correction parameter with its unsigned type. This plugin does not supply a separate HUD mask here; preservation of text or HUD is not guaranteed. New installations enable it; existing explicit settings are kept.", "按无符号类型请求模型内置 UI 结构保护。本插件在此没有额外提供 HUD 遮罩，不保证字幕或界面完全不变；新装默认开启，保留已有明确设置。"));
        // END PRIMARY MODEL CONTROLS

    if (next.identity_use_original && ImGui::Button(ui("Show NR picture (leave diagnostic identity)", "显示 NR 效果（退出诊断原图）"))) {
        next.identity_use_original = false; changed = true;
    }
    if (ImGui::CollapsingHeader(ui("Advanced picture controls", "高级画质"))) {
#if NR_HAS_DETAIL_STABILITY
    constexpr float maximum_strength = 4.0f;
#else
    constexpr float maximum_strength = 2.0f;
#endif
    float effect_strength = ActiveTransferStrength(next);
    if (ImGui::SliderFloat(ui("Strength amplification", "强度放大"),
            &effect_strength, 0.0f, maximum_strength, "%.2f")) {
        next.transfer_strength = next.post_transfer_strength = effect_strength; changed = true;
    }
    SettingsTooltip(ui("Final NR blend strength. 0 keeps native SR; 1.0 is the unscaled effect. New installations start at 1.2. Existing settings, including 0, are retained. The public range is 0-4; older requests above 4 are clamped. Higher values can amplify low-resolution noise. Mandatory colour budgeting limits extra chroma amplification above 1x; this is not the model's internal strength.",
        "控制最终 NR 混合强度。0 保留原生 SR，1.0 是不额外放大的效果；新装默认 1.2。已有设置（包括 0）会保留。范围为 0–4，旧配置超过 4 会限制到 4。较高数值仍可能放大低分辨率噪点。超过 1 倍自动限制额外颜色放大，不改变模型强度；这不保证消除模型自身色偏。"));

        if (ImGui::Button(ui("Post-SR comparison", "后置对照设置"))) {
            next.mode = 1; next.enabled = true; next.identity_use_original = false;
            next.transfer_strength = next.post_transfer_strength = 1.0f;
            next.color_strength = 1.0f; changed = true;
        }
        SettingsTooltip(ui("Sets SR -> NR with strength 1.0 and colour request 1.0 for comparison. These are comparison values, not the new installation defaults. Colour restrictions still apply.",
            "切到 SR → NR，采用强度 1.0、颜色请求 1.0 进行对照。这是对照基准，不是新装默认值；颜色准入限制仍然生效。"));
#if NR_HAS_DETAIL_STABILITY
        if (next.mode != 2) ImGui::BeginDisabled();
        changed |= ImGui::SliderFloat(ui("Detail stability (experimental)", "细节稳定保护（实验）"), &next.detail_stability, 0.f, 1.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Original lighting protection", "原始光照保护"), &next.lighting_lock, 0.f, 1.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Edge protection", "边缘保护"), &next.edge_guard, 0.f, 1.f, "%.2f");
        if (next.mode != 2) ImGui::EndDisabled();
        ImGui::TextWrapped("%s", ui("Detail stability defaults off and limits inconsistent fine NR changes. Available only before SR. Motion protection is unavailable: same-frame depth history has not been established.",
            "细节稳定保护默认关闭，只限制细碎 NR 变化，仅前置模式可用。运动保护不可用：尚未建立可靠的同帧深度历史。"));
#endif
        ImGui::SeparatorText(ui("Colour and HDR", "色彩与 HDR"));
        DrawColorControls(next, has_pending, changed);
        ImGui::TextWrapped("%s", ui("Unknown HDR uses luminance-only unless you explicitly try colour above. Storage, exposure and ownership safety still apply in both modes.",
            "未知 HDR 默认只修改明暗；上方手动试色是明确例外。两种模式仍保留存储、曝光与所有权安全检查。"));
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
    if (has_pending && (apply_discrete_edit || !ImGui::IsAnyItemActive())) {
        const auto result = ApplyOverlayConfig(pending, pending_generation);
        if (result != OverlayApplyResult::Busy) has_pending = false;
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;
    }
    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已被快捷键、配置重载或采集恢复更新；旧编辑未覆盖新值，请在当前值上重新调整。"));
}
