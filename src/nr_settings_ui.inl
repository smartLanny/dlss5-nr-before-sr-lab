#include "nr_present_api_status.h"
#include "nr_recommended_picture.inl"
#include "nr_precision_ui.h"

// A named overlay is an independent ReShade-owned window. nullptr registers
// inside the add-on settings area, where long panels can displace other add-ons.
// Keep the name identical across languages, versions and both cleanup paths.
constexpr char kSettingsOverlayName[] = "装机宅DLSS 5";

void SettingsTooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Hidden widget IDs remain stable when the display language changes.
void SettingsField(const char *label) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
}

void SettingsSliderLabel(const char *label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
}

bool SettingsSliderFloat(const char *label, const char *id, float *value,
                         float minimum, float maximum, const char *format) {
    SettingsSliderLabel(label);
    return ImGui::SliderFloat(id, value, minimum, maximum, format);
}

bool SettingsSliderInt(const char *label, const char *id, int *value,
                       int minimum, int maximum, const char *format) {
    SettingsSliderLabel(label);
    return ImGui::SliderInt(id, value, minimum, maximum, format);
}

void SettingsSection(const char *title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

void SettingsNote(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
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
    const bool color_follow_changed = SettingsSliderFloat(
        ui("Colour enhancement (NR follow)", "色彩强化（NR 跟随）"), "##nr-colour-enhancement",
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
        if (policy.amplification_color_limited)
            ImGui::TextDisabled("%s", ui("Colour protection is active above 1x.",
                "强度超过 1 倍，颜色保护已生效。"));
        if (!policy.color_allowed)
            ImGui::TextWrapped("%s", ui(
                "NR colour follow is paused for the current HDR contract; luminance NR can continue.",
                "当前 HDR 合同暂停 NR 色彩跟随；明暗 NR 仍可继续。"));
        else
            ImGui::TextDisabled("%s", policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr
                ? ui("NR colour follow is experimental for this HDR contract.",
                     "当前 HDR 合同中的 NR 色彩跟随属于实验功能。")
                : ui("NR colour follow is available for the current input.",
                     "当前输入可使用 NR 色彩跟随。"));
        SettingsTooltip(ui(
            "The saved request can differ from the effective bounded coefficient. Above 1x, colour amplification is budgeted separately from luminance; this status is not a measured visible-strength result.",
            "保存的请求值可能与受限后的实际系数不同。强度超过 1 倍时，颜色放大会与明暗分开限制；此状态不是实测画面强度。"));
    } else {
        ImGui::TextDisabled("%s", NrColorPolicyStatusText(
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
            ImGui::TextDisabled("%s", ui(
                "SR -> NR does not apply Lighting Match. The request is saved for NR -> SR.",
                "SR -> NR 不采用光照匹配。请求值已保留，切回 NR -> SR 后可恢复。"));
    } else {
        ImGui::TextDisabled("%s", NrColorPolicyStatusText(
            color_policy_snapshot.status, use_chinese_ui));
    }
#endif
}

#if defined(NR_HAS_F8_SEQUENCE)
void DrawSimpleFeedbackControls() {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const auto image_status = nr_f8::g_status.load(std::memory_order_acquire);
    const bool busy = nr_f8::IsActive() || image_status == nr_f8::Status::Packaging ||
        nr_core_feedback::Busy();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button(ui("Save feedback TXT + copy path (F8)", "生成报告并复制路径（F8）")))
        nr_f8::RequestCore();
    SettingsTooltip(ui(
        "Saves one bounded diagnostic TXT without changing picture settings. The runtime prepares its path for one automatic clipboard copy.",
        "保存一个有大小上限的诊断 TXT，不改变画质设置；运行时会准备文件路径并自动复制一次。"));
    if (busy) ImGui::EndDisabled();

    const auto status = g_feedback_status.load(std::memory_order_acquire);
    if (status == 1 || status == 2) {
        ImGui::TextDisabled("%s", ui("Collecting the TXT...", "正在生成 TXT……"));
        return;
    }
    if (status < 3) return;

    DWORD error = 0;
    bool complete = false;
    bool path_ready = false;
    AcquireSRWLockShared(&g_feedback_lock);
    path_ready = nr_core_feedback::g_report_path[0] != 0;
    error = nr_core_feedback::g_report_error;
    complete = nr_core_feedback::g_report_complete;
    ReleaseSRWLockShared(&g_feedback_lock);
    if (!path_ready) {
        if (error) {
            ImGui::TextWrapped(ui(
                "TXT save failed (Win32 %lu); no previous path was reused.",
                "TXT 保存失败（Win32 %lu），没有复用旧路径。"), error);
        }
        return;
    }

    using ClipboardStatus = nr_core_feedback::ReportClipboardStatus;
    const auto clipboard = nr_core_feedback::GetReportClipboardStatus();
    if (clipboard == ClipboardStatus::Copied)
        ImGui::TextWrapped("%s", complete
            ? ui("TXT saved; its path was copied to the clipboard.",
                 "TXT 已保存，文件路径已复制到剪贴板。")
            : ui("TXT saved with explicit missing evidence; its path was copied.",
                 "TXT 已保存，缺失证据已注明；文件路径已复制。"));
    else if (clipboard == ClipboardStatus::Failed)
        ImGui::TextWrapped("%s", ui(
            "TXT saved, but its path could not be copied automatically.",
            "TXT 已保存，但文件路径自动复制失败。"));
    else
        ImGui::TextDisabled("%s", ui(
            "TXT saved; its path is ready for automatic clipboard delivery.",
            "TXT 已保存，路径已准备好由运行时自动复制。"));
}

void DrawSimpleImageCaptureControls() {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const auto state = nr_f8::g_status.load(std::memory_order_acquire);
    const bool active = nr_f8::IsActive();
    const bool packaging = state == nr_f8::Status::Packaging;
    const bool blocked = active || packaging || nr_core_feedback::Busy();
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::BeginCombo(ui("Image supplement (F7)", "图像补充（F7）"),
                          ui("Choose capture", "选择采集"))) {
        if (ImGui::Selectable(ui("Current view: 512 x 512, 3 frames",
                                  "当前画面：512 × 512，3 帧")))
            nr_f8::RequestWithOptions({nr_f8::Comparison::Current, false, 0, 0});
        if (ImGui::Selectable(ui("One-click share matrix: 7 groups, 2 frames each",
                                  "一键分享矩阵：7 组，每组 2 帧")))
            nr_f8::RequestWithOptions({nr_f8::Comparison::ShareMatrix, false, 0, 0});
        ImGui::EndCombo();
    }
    SettingsTooltip(ui(
        "Current captures three 512 x 512 ROI frames. The matrix captures native plus pre/post NR at 50%, 75% and 100%; RR protection skips pre-NR groups without applying them.",
        "当前画面采集 3 帧 512 × 512 ROI；矩阵依次采集原生及前/后置 NR 的 50%、75%、100%，RR 保护中会跳过前置组且不应用其设置。"));
    if (blocked) ImGui::EndDisabled();
    ImGui::TextDisabled("%s", ui("Keep the camera fixed; groups are captured in order.",
        "请保持视角不动，系统会按顺序采集。"));
    if (active) {
        ImGui::TextDisabled("%s", ui("Image capture is running...", "正在采集图像……"));
        ImGui::SameLine();
        if (ImGui::Button(ui("Cancel capture", "取消采集"))) nr_f8::Cancel();
    } else if (packaging) {
        ImGui::TextDisabled("%s", ui("Packaging image evidence...", "正在整理图像证据……"));
    } else if (state == nr_f8::Status::Complete) {
        ImGui::TextDisabled("%s", ui("Image evidence saved.", "图像证据已保存。"));
    } else if (state == nr_f8::Status::Partial) {
        ImGui::TextDisabled("%s", ui("Image evidence saved with explicit missing groups.",
            "图像证据已保存，缺失组已明确记录。"));
    } else if (state == nr_f8::Status::Failed) {
        ImGui::TextDisabled("%s", ui("Image capture did not complete.", "图像采集未完成。"));
    }
}
#endif

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
    // Local styling follows ReShade's palette and never changes another panel.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 7.0f));
    ImGui::SeparatorText(ui("Zhuangjizhai DLSS 5", "装机宅DLSS 5"));
    ImGui::TextDisabled("%s | %s", kAddonVersion,
                        use_chinese_ui ? kAddonAuthor : kAddonAuthorAscii);
    ImGui::TextDisabled("%s", kOfficialReleaseUrl);
    ImGui::TextDisabled("%s", ui("QQ group 2: 392308850", "交流群（二群）：392308850"));
    if (kChineseUi && !use_chinese_ui)
        ImGui::TextWrapped("Chinese glyphs are unavailable in the current ReShade font, so this panel is using English instead of broken squares. In ReShade Settings > Overlay & Styling, choose zh-CN/System Default or C:\\Windows\\Fonts\\msyh.ttc, then restart the game.");
    // Keep controls stationary while live status text changes (#121).
    ImGui::BeginChild("nr-live-status", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4.25f), true);
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
        ImGui::TextWrapped("%s", ui("NR runtime could not load or initialize. Keep your NVIDIA DLLs; install the matching nrchain_nvngx.dll and nvngx_dlssnr.dll from this OTA beside the addon (game directory is a fallback). F8 TXT distinguishes file-absent from Win32 126 dependency-missing.",
            "NR 运行库加载或初始化失败。保留显卡对应 NVIDIA DLL，把本 OTA 配套 nrchain_nvngx.dll 与 nvngx_dlssnr.dll 放在插件旁（游戏目录仅作回退）。F8 TXT 会区分文件缺失与 Win32 126 依赖缺失。"));
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
    else if (reason == BypassReason::RayReconstructionGuard) {
        const auto at = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
        const auto now = GetTickCount64();
        const bool recent = at && now >= at && now - at <= 2000;
        ImGui::TextWrapped("%s", recent ? ui(
            "Ray Reconstruction protection is active; eligible RR is routed automatically to post-NR.",
            "光线重建保护中；符合条件的 RR 会自动进入后置 NR。") : ui(
            "Ray Reconstruction protection is active; waiting for a new RR evaluation before reporting post-NR activity.",
            "光线重建保护中；正在等待新的 RR 评估，再报告后置 NR 活动。"));
    }
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
#if defined(NR_HAS_F8_SEQUENCE)
    const bool collecting = nr_f8::IsActive();
#endif

    if (ImGui::BeginTabBar("nr-main-tabs")) {
    if (ImGui::BeginTabItem(ui("Picture###nr-picture", "画质###nr-picture"))) {
#if defined(NR_HAS_F8_SEQUENCE)
    if (collecting) ImGui::BeginDisabled();
#endif
    SettingsSection(ui("Quick settings", "快速设置"));
    apply_discrete_edit = ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);
    changed |= apply_discrete_edit;
    if (ImGui::Button(ui("Restore recommended picture settings", "一键恢复推荐画质"))) {
        RestoreRecommendedPictureSettings(next);
        changed = true;
    }
    SettingsTooltip(ui(
        "Restores Before SR, strength amplification 1.2, model strength 1.0, colour request 1.0 and the shipped picture defaults. Exits diagnostic identity and disables unverified HDR colour/peak experiments. Keeps Enable (F6), depth/jitter/R8 game contracts and feedback preferences. Uses the normal save/history rules; runtime safety faults are not cleared.",
        "恢复前置 NR、强度放大 1.2、模型强度 1.0、颜色请求 1.0 等随包推荐画质；退出诊断原图，关闭未验证 HDR 调色与高光实验。保留 F6 总开关、深度/Jitter/R8 游戏兼容设置和反馈偏好；按正常规则保存及更新历史，不清除运行时安全故障。"));
    SettingsSection(ui("Processing", "处理方式"));
    const bool rr_guarded = IsRayReconstructionRouteObserved();
    const auto rr_last_attempt = nr_post_settings::last_rr_tick.load(std::memory_order_acquire);
    const auto rr_now = GetTickCount64();
    const bool rr_recent = rr_last_attempt != 0 && rr_now >= rr_last_attempt &&
        rr_now - rr_last_attempt <= 2000;
    int mode = rr_guarded ? 1 : (next.mode == 2 ? 0 : 1);
    SettingsField(ui("Processing mode", "处理模式"));
    if (rr_guarded) ImGui::BeginDisabled();
    if (ImGui::Combo("##nr-processing-mode", &mode,
            ui("Before SR: NR -> SR\0After SR: SR -> NR\0",
               "前置 NR → SR\0后置 SR → NR\0"))) {
        next.mode = mode == 0 ? 2 : 1; changed = true;
    }
    if (rr_guarded) ImGui::EndDisabled();
    SettingsTooltip(ui("Before SR usually costs less. After SR processes the reconstructed output. The game controls its own DLSS render scale; this selector does not force DLAA.",
        "前置通常较省性能，后置处理重建后的输出。游戏自行控制 DLSS 真实渲染比例；此选项不会强制 DLAA。"));
    if (rr_guarded) ImGui::TextWrapped("%s", rr_recent ? ui(
        "Ray Reconstruction is active: SR/RR -> NR is selected automatically; NR -> SR is unavailable.",
        "光线重建生效，强制后置 NR：当前为 SR/RR → NR，NR → SR 暂不可选。") : ui(
        "Ray Reconstruction resources are present, so NR -> SR stays protected. Waiting for current RR activity; prior counters may be historical.",
        "检测到光线重建资源，已保护性禁用 NR → SR；正在等待本次 RR 活动，既有计数可能是历史累计。"));
#if NR_HAS_DETAIL_STABILITY
    const bool before_sr = !rr_guarded && next.mode == 2;
    const bool effective_post = !before_sr;
    // These atomics are historical observations, NOT a coherent frame tuple.
    // They only seed a visibly labelled estimate; no config is migrated until
    // the user edits the slider. The GPU resolver still checks the real tuple.
    auto post_options = nr_post_settings::Read();
    static float post_precision = 100.0f;
    static unsigned observed_post_precision = 0;
    if (observed_post_precision != post_options.percent) {
        post_precision = static_cast<float>(post_options.percent);
        observed_post_precision = post_options.percent;
    }
    float precision = effective_post ? post_precision :
        nr_precision_ui::LegacyEstimate(next.work_mode, next.custom_work_scale,
            g_last_dlss_input_width.load(), g_last_dlss_input_height.load(),
            g_last_dlss_output_width.load(), g_last_dlss_output_height.load());
    precision = std::clamp(precision, 50.0f, 100.0f);
    if (SettingsSliderFloat(ui("NR working scale", "NR 工作比例"),
            "##nr-processing-precision", &precision, 50.0f, 100.0f, "%.0f%%")) {
        if (effective_post) {
            post_precision = precision;
        } else {
            const auto request = nr_precision_ui::Resolve(precision, true);
            if (request.accepted) {
                next.work_mode = request.mode;
                next.custom_work_scale = request.scale;
                changed = true;
            }
        }
    }
    if (effective_post && ImGui::IsItemDeactivatedAfterEdit()) {
        post_options.percent = static_cast<unsigned>(std::lround(post_precision));
        if (nr_post_settings::Request(post_options))
            observed_post_precision = post_options.percent;
    }
    SettingsTooltip(ui(
        "Sets only NR work from 50% to 100% for the effective pre/post route. It does not change the game's render resolution. 100% uses the route's full input.",
        "仅设置当前前置/后置路线的 NR 工作量，可在 50%–100% 连续调整；不改变游戏真实渲染分辨率，100% 使用该路线的完整输入。"));
#endif
    int style = static_cast<int>(next.style);
    SettingsSection(ui("Model details", "模型细节"));
    SettingsField(ui("Style", "风格"));
    if (ImGui::Combo("##nr-model-style", &style,
            ui("Default\0Natural\0Cinematic\0", "默认\0自然\0电影\0"))) {
        next.style = static_cast<unsigned>(style); changed = true;
    }

        changed |= SettingsSliderFloat(ui("Model strength", "模型强度"),
            "##nr-model-strength", &next.intensity, 0.f, 2.f, "%.2f");
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
        changed |= SettingsSliderFloat(ui("Local tone", "局部影调"),
            "##nr-local-tone", &next.local_tone, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Changes local contrast inside the NR model. It is separate from final blend strength.", "调整 NR 模型内部的局部明暗对比，与最终混合强度不同。"));
        changed |= SettingsSliderFloat(ui("Local structure", "局部结构"),
            "##nr-local-structure", &next.local_structure, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Adjusts the model local structure parameter; stronger values can amplify unstable fine detail.", "调整模型内部局部结构参数；过高也可能放大不稳定细节。"));
        if (ImGui::Checkbox(ui("Face mask", "人脸遮罩"), &next.auto_mask)) {
            if (next.auto_mask && next.skin_structure < 0.f)
                next.skin_structure = 0.f;
            changed = true;
        }
        SettingsTooltip(ui("Requests the model automatic face-mask parameter, not a mask generated by this plugin. Its effect depends on the model and input. Off sends skin strength -1 to follow Local structure; the last independent value is retained for re-enabling.", "请求模型内置的自动人脸遮罩，并非插件额外生成人脸识别图；效果取决于模型和输入。关闭时向模型发送皮肤强度 -1，跟随局部结构；上次独立设置会保留，重新开启时恢复。"));
        if (next.auto_mask) {
            changed |= SettingsSliderFloat(ui("Skin detail strength", "皮肤细节强度"),
                "##nr-skin-detail", &next.skin_structure, 0.f, 2.f, "%.2f");
            SettingsTooltip(ui("Independent skin structure request while Face mask is enabled. Legacy negative values start at 0; existing nonnegative values are retained. This is not guaranteed face detection or a measured picture-strength value.", "仅在人脸遮罩开启时请求独立皮肤结构强度。旧版负值从 0 开始，已有非负值保留；这不保证人脸识别成功，数值也不代表测得的画面效果强度。"));
        } else {
            SettingsNote(ui("Enable Face mask to adjust skin detail; currently follows Local structure.", "开启人脸遮罩后可调整皮肤细节；当前跟随局部结构。"));
        }
        changed |= ImGui::Checkbox(ui("UI correction", "UI 结构保护"), &next.ui_correction);
        SettingsTooltip(ui("Requests the model UI-correction parameter with its unsigned type. This plugin does not supply a separate HUD mask here; preservation of text or HUD is not guaranteed. New installations enable it; existing explicit settings are kept.", "按无符号类型请求模型内置 UI 结构保护。本插件在此没有额外提供 HUD 遮罩，不保证字幕或界面完全不变；新装默认开启，保留已有明确设置。"));
        // END PRIMARY MODEL CONTROLS

    if (next.identity_use_original && ImGui::Button(ui("Show NR picture (leave diagnostic identity)", "显示 NR 效果（退出诊断原图）"))) {
        next.identity_use_original = false; changed = true;
    }
    SettingsSection(ui("Advanced picture controls", "高级画质"));
#if NR_HAS_DETAIL_STABILITY
    constexpr float maximum_strength = 4.0f;
#else
    constexpr float maximum_strength = 2.0f;
#endif
    float effect_strength = ActiveTransferStrength(next);
    if (SettingsSliderFloat(ui("Strength amplification", "强度放大"),
            "##nr-strength-amplification", &effect_strength,
            0.0f, maximum_strength, "%.2f")) {
        next.transfer_strength = next.post_transfer_strength = effect_strength; changed = true;
    }
    SettingsTooltip(ui("Final NR blend strength. 0 keeps native SR; 1.0 is the unscaled effect. New installations start at 1.2. Existing settings, including 0, are retained. The public range is 0-4; older requests above 4 are clamped. Higher values can amplify low-resolution noise. Mandatory colour budgeting limits extra chroma amplification above 1x; this is not the model's internal strength.",
        "控制最终 NR 混合强度。0 保留原生 SR，1.0 是不额外放大的效果；新装默认 1.2。已有设置（包括 0）会保留。范围为 0–4，旧配置超过 4 会限制到 4。较高数值仍可能放大低分辨率噪点。超过 1 倍自动限制额外颜色放大，不改变模型强度；这不保证消除模型自身色偏。"));

#if NR_HAS_DETAIL_STABILITY
        if (!before_sr) ImGui::BeginDisabled();
        changed |= SettingsSliderFloat(ui("Detail stability", "细节稳定保护"),
            "##nr-detail-stability", &next.detail_stability, 0.f, 1.f, "%.2f");
        if (!before_sr) ImGui::EndDisabled();
        SettingsTooltip(ui("Available only before SR; same-frame depth history is not established for motion protection.",
            "仅在前置模式可用；尚未建立可靠的同帧深度历史，因此没有运动保护。"));
        changed |= SettingsSliderFloat(ui("Original lighting protection", "原始光照保护"),
            "##nr-lighting-lock", &next.lighting_lock, 0.f, 1.f, "%.2f");
        SettingsTooltip(ui("Applies to the actual composition route, including native post-SR and RR-forced post-SR. 0 disables it; 1 broadly preserves original lighting. This is local lighting matching, not guaranteed face detection.",
            "作用于实际合成路线，包括原生后置与 RR 强制后置。0 为关闭，1 为广泛保留原始光照；这是局部光照匹配，不保证人脸识别。"));
        if (!before_sr) ImGui::BeginDisabled();
        changed |= SettingsSliderFloat(ui("Edge protection", "边缘保护"),
            "##nr-edge-guard", &next.edge_guard, 0.f, 1.f, "%.2f");
        if (!before_sr) ImGui::EndDisabled();
        SettingsTooltip(ui("Available only before SR; it reduces transfer across strong local edges.",
            "仅在前置模式可用；用于减少强局部边缘两侧的效果串扰。"));
#endif
        ImGui::SeparatorText(ui("Colour and HDR", "色彩与 HDR"));
        DrawColorControls(next, has_pending, changed);
        changed |= ImGui::Checkbox(ui("Experimental highlight mapping", "实验高光映射"), &next.experimental_peak_shoulder);
        SettingsTooltip(ui("Unknown HDR defaults to luminance only. Storage, exposure and ownership safety remain active.",
            "未知 HDR 默认仅处理明暗；存储、曝光和所有权安全检查始终保留。"));
        changed |= SettingsSliderFloat(ui("Maximum luminance ratio", "最大亮度倍率"),
            "##nr-max-luma-ratio", &next.max_luma_ratio, 1.f, 8.f, "%.2f");
#if NR_HAS_DETAIL_STABILITY
        int curve = static_cast<int>(next.proxy_curve);
        if (ImGui::Combo(ui("HDR mapping", "HDR 映射"), &curve,
                ui("Legacy shoulder\0Hybrid\0Reversible (experimental)\0", "旧版压肩\0混合\0可逆（实验）\0"))) {
            next.proxy_curve = static_cast<unsigned>(curve); changed = true;
        }
#endif
#if defined(NR_HAS_F8_SEQUENCE)
    if (collecting) ImGui::EndDisabled();
#endif
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ui("Experimental###nr-experimental", "实验###nr-experimental"))) {
#if defined(NR_HAS_F8_SEQUENCE)
        if (collecting) ImGui::BeginDisabled();
#endif
        SettingsSection(ui("Observed game render scale", "游戏真实渲染比例"));
        const unsigned render_width = g_last_dlss_input_width.load(std::memory_order_relaxed);
        const unsigned render_height = g_last_dlss_input_height.load(std::memory_order_relaxed);
        const unsigned output_width = g_last_dlss_output_width.load(std::memory_order_relaxed);
        const unsigned output_height = g_last_dlss_output_height.load(std::memory_order_relaxed);
        if (render_width && render_height && output_width && output_height) {
            const double render_scale = 100.0 * std::min(
                static_cast<double>(render_width) / output_width,
                static_cast<double>(render_height) / output_height);
            ImGui::Text(ui("Game DLSS: %u x %u -> %u x %u (about %.0f%%)",
                           "游戏 DLSS：%u x %u → %u x %u（约 %.0f%%）"),
                render_width, render_height, output_width, output_height, render_scale);
        } else {
            ImGui::TextDisabled("%s", ui("Waiting for game DLSS resolution data.",
                "正在等待游戏 DLSS 分辨率数据。"));
        }
        SettingsTooltip(ui(
            "Read-only observation. Change the game's actual render scale in its DLSS quality settings; NR working scale is independent.",
            "这是只读观测。游戏真实渲染比例由游戏内 DLSS 质量档位调整，与 NR 工作比例相互独立。"));

        if (ImGui::CollapsingHeader(ui("Developer options", "开发者选项"))) {
            int jitter = static_cast<int>(next.jitter_mode);
            if (ImGui::Combo(ui("NR jitter input", "NR jitter 输入"), &jitter,
                    ui("Do not forward\0Forward verified game pixel coordinates\0", "不传递\0传递已验证的游戏像素坐标\0"))) {
                next.jitter_mode = static_cast<NrJitterMode>(jitter); changed = true;
            }
            SettingsTooltip(ui("Experimental input forwarding; native SR jitter is unchanged. Switching rebuilds NR history.",
                "实验性输入传递；不改变原生 SR jitter，切换后会重建 NR 历史。"));
            auto jitter_snapshot = SnapshotJitterStatus();
            using JitterStatus = nr_jitter_status::Status;
            if (jitter_snapshot.status != JitterStatus::Bypassed &&
                (has_pending || changed)) jitter_snapshot.status = JitterStatus::Pending;
            const char *jitter_text = ui("Jitter: no current successful NR binding; saved request retained.",
                "Jitter：暂无当前配置的成功 NR 绑定，已保留请求。");
            if (next.jitter_mode == NrJitterMode::Legacy)
                jitter_text = ui("Jitter: forwarding is disabled.", "Jitter：未开启传递。");
            else if (jitter_snapshot.status == JitterStatus::Bound)
                jitter_text = ui("Jitter: written for a recent successful NR evaluation; model use is unverified.",
                    "Jitter：近期成功 NR 调用已写入参数；模型是否使用仍未验证。");
            else if (jitter_snapshot.status == JitterStatus::Unknown)
                jitter_text = ui("Jitter: current game coordinates are missing or invalid.",
                    "Jitter：当前游戏坐标缺失或无效。");
            else if (jitter_snapshot.status == JitterStatus::Bypassed)
                jitter_text = ui("Jitter: NR is currently bypassed.", "Jitter：当前 NR 已旁路。");
            else if (jitter_snapshot.status == JitterStatus::Pending)
                jitter_text = ui("Jitter: waiting for the requested settings to apply.", "Jitter：等待请求的设置生效。");
            ImGui::TextDisabled("%s", jitter_text);
            changed |= ImGui::Checkbox(ui("Retry original SR on failure", "失败时重试原始 SR"), &next.retry_original_on_sr_failure);
            changed |= ImGui::Checkbox(ui("Run NR but discard its picture", "运行 NR 但丢弃其画面（诊断）"), &next.identity_use_original);
            int warmup = static_cast<int>(next.warmup_after_create);
            if (SettingsSliderInt(ui("Warm-up frames", "预热帧数"),
                    "##nr-warmup-frames", &warmup, 0, 30, "%d")) {
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
        DrawSimpleFeedbackControls();
        SettingsSection(ui("Image evidence", "图像证据"));
        DrawSimpleImageCaptureControls();
#else
        ImGui::TextDisabled("%s", ui("Single-TXT feedback is unavailable in this build.",
            "此构建未启用单 TXT 反馈。"));
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
    ImGui::PopStyleVar(2);
}
