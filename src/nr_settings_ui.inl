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

float SettingsContentWidth() {
    // Preserve the real content bounds (including scrollbars/columns) while
    // crossing the host's aggregate-return ABI explicitly.
    return nr_imgui_abi::CallHostVec2(
        imgui_function_table_instance()->GetContentRegionAvail).x;
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

bool DrawProcessingModeBlocks(ChainConfig &next, bool rr_guarded) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const char *labels[] = {
        ui("Before: NR -> SR###route-option", "前置 NR → SR###route-option"),
        ui("After: SR -> NR###route-option", "后置 SR → NR###route-option")};
    const float width = std::max(1.0f,
        (SettingsContentWidth() - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
    bool changed = false;
    ImGui::PushID("nr-processing-mode");
    if (rr_guarded) ImGui::BeginDisabled();
    for (int option = 0; option < 2; ++option) {
        if (option != 0) ImGui::SameLine();
        ImGui::PushID(option);
        const int requested_mode = option == 0 ? 2 : 1;
        const int shown_mode = rr_guarded ? 1 : next.mode;
        const bool selected = shown_mode == requested_mode;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked = ImGui::Button(labels[option],
            ImVec2(width, ImGui::GetFrameHeight() * 1.25f));
        if (selected) ImGui::PopStyleColor();
        if (clicked && !rr_guarded && !selected) {
            next.mode = requested_mode;
            changed = true;
        }
        SettingsTooltip(option == 0 ? ui(
            "AI rendering first, then game upscaling. Usually costs less. Ray Reconstruction (RR) automatically uses the right-hand option instead.",
            "先做 AI 渲染，再由游戏放大画面，通常更省性能。开启光线重建（RR）时，会自动使用右侧的后置模式。") : ui(
            "Game upscaling first, then AI rendering. Usually costs more. RR uses this mode automatically; your choice returns when RR is off.",
            "先由游戏放大画面，再做 AI 渲染，通常更耗性能。光线重建（RR）开启时自动使用此模式；关闭 RR 后恢复你自己的选择。"));
        ImGui::PopID();
    }
    if (rr_guarded) ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

void DrawColorControls(ChainConfig &next, bool has_pending, bool &changed) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    NrColorPolicySnapshot color_policy_snapshot = SnapshotColorPolicy();
    if (color_policy_snapshot.status != NrColorPolicyStatus::Bypassed &&
        (has_pending || changed || color_policy_snapshot.mode != EffectivePictureMode(next.mode)))
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    const bool current_color_policy =
        color_policy_snapshot.status == NrColorPolicyStatus::Current;
    const bool color_follow_changed = SettingsSliderFloat(
        ui("AI colour amount", "AI 色彩强度"), "##nr-colour-enhancement",
        &next.color_strength, 0.0f, 1.0f, "%.2f");
    SettingsTooltip(ui("Controls only NR hue and saturation follow, not brightness, detail or the overall mix. If the model changes only luminance, this may have little effect. Applies after releasing the slider. 0 keeps the game colour. Experimental HDR permission is a separate developer option.",
        "决定画面采用多少 AI 生成的颜色。0 保留游戏原色，1 使用允许范围内的完整 AI 色彩；它不增加细节，也不单独调整明暗。AI 没有明显改变颜色时，对比也会很小。松开滑块后生效。"));
    changed |= color_follow_changed;
    if (color_follow_changed &&
        color_policy_snapshot.status != NrColorPolicyStatus::Bypassed)
        color_policy_snapshot.status = NrColorPolicyStatus::Pending;
    if (next.allow_unverified_hdr_color && current_color_policy &&
        color_policy_snapshot.policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr)
        ImGui::TextWrapped("%s", ui("WARNING: unverified HDR colour may shift hue or flicker. Set colour to 0 or restore recommended settings to undo.", "警告：未验证 HDR 色彩可能偏色或闪烁；可拉回 0 或一键恢复推荐画质。"));
    if (color_policy_snapshot.status == NrColorPolicyStatus::Current) {
        const NrColorPolicy &policy = color_policy_snapshot.policy;
        if (policy.amplification_color_limited)
            ImGui::TextDisabled("%s", ui("Colour protection is active above 1x.",
                "强度超过 1 倍，颜色保护已生效。"));
        if (!policy.color_allowed)
            ImGui::TextWrapped("%s", ui(
                "NR colour follow is paused for the current HDR contract; luminance NR can continue.",
                "当前 HDR 画面暂不调整 AI 颜色，明暗和细节仍可处理。实验兼容开关在开发者选项。"));
        else
            ImGui::TextDisabled("%s", policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr
                ? ui("NR colour follow is experimental for this HDR contract.",
                     "正在使用实验 HDR 调色。出现偏色时可在开发者选项关闭。")
                : ui("NR colour follow is available for the current input.",
                     "AI 色彩已参与最近的画面处理；实际变化取决于场景。"));
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
        if (ImGui::Selectable(ui("Current view: full screen, original resolution",
                                  "当前画面：全屏原图")))
            nr_f8::RequestFullscreen();
        if (ImGui::Selectable(ui("Seven-group comparison: full-screen originals",
                                  "七组画面对比：全屏原图")))
            nr_f8::RequestFullscreen(nr_f8::Comparison::ShareMatrix);
        ImGui::EndCombo();
    }
    SettingsTooltip(ui(
        "Save the entire game view at its original screenshot resolution. The seven-group comparison saves NR off, then NR before/after SR at 50%, 75% and 100%. Keep the camera still; active RR skips NR-before-SR groups.",
        "保存完整游戏画面，不裁切、不缩小。七组对比依次保存关闭 NR，以及 NR→SR、SR→NR 各 50%、75%、100% 的画面。请保持镜头不动；开启 RR 时会跳过 NR→SR。"));
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

// The two maintained editions share the same small home screen. Technical
// readouts remain available without putting implementation knobs on the home.
// Each drawing thread keeps its own bounded window. Multiple swapchains must
// not turn lock contention into a one-frame "checking" status.
nr_ui_activity::Sample ReadUiActivity(unsigned config_generation) {
    static thread_local nr_ui_activity::Window window;
    const auto epoch=g_pipeline_stats_epoch.load(std::memory_order_acquire);
    const nr_ui_activity::Counters counters{g_carrier_evaluations.load(),
        g_nr_successful_evaluations.load(),g_nr_transfer_submitted_frames.load(),
        g_queue_affinity_unknown_bypass_frames.load()};
    const auto reason=g_last_bypass_reason.load(std::memory_order_acquire);
    if (g_config_generation.load(std::memory_order_acquire)!=config_generation ||
        g_pipeline_stats_epoch.load(std::memory_order_acquire)!=epoch) {
        window={};
        return {};
    }
    return window.Observe(GetTickCount64(),counters,{config_generation,epoch},reason);
}

void DrawNrActivityStatus(reshade::api::effect_runtime *runtime,
                          const ChainConfig &config,unsigned generation) {
    const bool zh=UseChineseUi();
    const auto ui=[zh](const char *en,const char *cn){return zh?cn:en;};
    const auto activity=ReadUiActivity(generation);
    // A carrier can safely skip between two submitted NR frames. Keep the
    // entire summary on one window instead of contradicting it every frame.
    // The unfiltered per-carrier reason and counters remain in diagnostics/F8.
    const auto reason=static_cast<BypassReason>(activity.bypass_reason);
    if (nr_present_api_status::IsD3d11PresentRuntime(runtime))
        ImGui::TextWrapped("%s", ui(
            "This ReShade window is using Direct3D 11, so DLSS 5 NR cannot run. Switch the game to Direct3D 12, then install ReShade Full Add-on for Direct3D 12.",
            "当前 ReShade 画面使用 Direct3D 11，本插件无法运行。请把游戏切换到 Direct3D 12，并为 D3D12 安装 ReShade Full Add-on。"));
    else if (!config.enabled || ActiveTransferStrength(config) <= 0.000001f)
        ImGui::TextWrapped("%s", ui("Native game SR. Enable the plugin and raise effect strength to compare NR.",
            "当前为游戏原生 SR。启用插件并提高效果强度，即可对比 NR。"));
    else if (!activity.valid)
        ImGui::TextWrapped("%s", ui("Checking recent NR activity...", "正在检查近期 NR 处理状态……"));
    else if (activity.Intermittent())
        ImGui::TextWrapped("%s", ui(
            "NR is processing intermittently. Some evaluations are skipped while queue synchronization requirements are unmet. Use F6 to compare and F8 to report.",
            "NR 正在间歇处理；部分评估因队列同步条件未满足而跳过。按 F6 对比画面，按 F8 反馈。"));
    else if (activity.Processing())
        ImGui::TextWrapped("%s", ui("Recent NR processing and output submission observed. Use F6 to compare the picture.",
            "最近已观察到 NR 处理和画面输出提交。按 F6 对比实际效果。"));
    else if (reason == BypassReason::QueueRetirementTimeout)
        ImGui::TextWrapped("%s", ui("Submitted NR did not finish within the bounded wait. Native output is retained for these settings; use F8 before retrying with F6.",
            "上一轮 NR 未在等待上限内完成；当前设置保持原生输出。请先按 F8 记录，再用 F6 重新尝试。"));
    else if (reason == BypassReason::RuntimeUnavailable)
        ImGui::TextWrapped("%s", ui("NR runtime could not load or initialize. Keep your NVIDIA DLLs; install the matching nrchain_nvngx.dll and nvngx_dlssnr.dll beside the addon. Use F8 for the actual failure record.",
            "NR 运行库加载或初始化失败。保留显卡对应 NVIDIA DLL，将配套 nrchain_nvngx.dll 与 nvngx_dlssnr.dll 放在插件旁；按 F8 记录具体失败。"));
    else if (reason == BypassReason::CodecUnavailable)
        ImGui::TextWrapped("%s", ui("The NR conversion/composition pipeline could not initialize. Native output is preserved; use F8 for the actual failure record.",
            "NR 转换/合成管线初始化失败，保留原生输出；请用 F8 记录具体失败。"));
    else if (reason == BypassReason::RayReconstructionGuard) {
        DrawPostRouteStatus();
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
        ImGui::TextWrapped("%s", ui("No new NR output submission observed in the recent interval. Enter gameplay; use F8 if this persists.",
            "近期暂未观察到新的 NR 画面输出提交。请进入实际场景；持续无效时按 F8 反馈。"));
    SettingsTooltip(ui("Updated about once per second from recent process counters. Submission is not proof of presentation or picture quality. Per-evaluation reasons remain in F8.",
        "约每秒根据近期处理计数更新。提交不等于已显示或画质已改善；每次评估的具体原因仍保留在 F8。"));
}

void DrawSettings(reshade::api::effect_runtime *runtime) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    static ChainConfig pending = {};
    static bool has_pending = false;
    static bool pending_picture_reset = false;
    static unsigned pending_generation = 0;
    static bool edit_conflict = false;
    const auto current = SnapshotOverlayConfig();
    if (has_pending && pending_generation != current.generation) {
        has_pending = false; pending_picture_reset = false; edit_conflict = true;
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
    DrawNrActivityStatus(runtime,current.config,current.generation);


    if (next.identity_use_original)
        ImGui::TextWrapped("%s", ui(
            "Diagnostic identity mode is ON: NR may run, but its picture is deliberately discarded. Turn off 'Run NR but discard its picture' to compare the effect.",
            "诊断原图模式已开启：NR 可以运行，但效果会被主动丢弃。请关闭兼容性选项中的“运行 NR 但丢弃其画面”再对比。"));
    if (g_config_persistence_failed.load())
        ImGui::TextWrapped("%s", ui("Settings cannot be saved. Check that the plugin folder is writable.",
            "设置无法保存，请检查插件目录是否可写。当前设置仍用于本次运行。"));

    ImGui::EndChild();
    bool apply_discrete_edit = false;
    const bool collecting = nr_f8::IsActive();

    if (ImGui::BeginTabBar("nr-main-tabs")) {
    const ImGuiTabItemFlags feedback_tab_flags =
        nr_f8::TakeSettingsFeedbackPageRequest()
            ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem(ui("Picture###nr-picture", "画质###nr-picture"))) {
    if (collecting) ImGui::BeginDisabled();
    SettingsSection(ui("Quick settings", "快速设置"));
    apply_discrete_edit = ImGui::Checkbox(ui("Enable (F6)", "启用（F6）"), &next.enabled);
    changed |= apply_discrete_edit;
    if (ImGui::Button(ui("Restore default picture settings", "一键恢复默认画质"))) {
        RestoreRecommendedPictureSettings(next);
        changed = true;
        pending_picture_reset = true;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Restores NR before SR, 100% NR rendering for both routes, model strength 1.2, extra enhancement 1.0, lighting protection 0 and colour protection on. Keeps the F6 switch and game compatibility settings. Other picture experiments return to their recommended defaults.",
        "恢复前置 NR、前后置 NR 渲染比例 100%、模型强度 1.2、额外增强 1.0、光照保护 0，并开启色彩保护。保留 F6 总开关和游戏兼容设置；其他画质实验选项恢复推荐值。"));
    SettingsSection(ui("Processing", "处理方式"));
    const bool rr_guarded = IsRayReconstructionRouteObserved();
    SettingsField(ui("Processing mode", "处理模式"));
    if (DrawProcessingModeBlocks(next, rr_guarded)) {
        changed = true;
        apply_discrete_edit = true;
    }
    if (rr_guarded) DrawPostRouteStatus();
    const bool compatibility_post = !rr_guarded && next.mode==2 && IsCompatibilityPostObserved();
    if (compatibility_post) DrawGenericCompatibilityStatus();
    const bool before_sr = !rr_guarded && !compatibility_post && next.mode == 2;
#if NR_HAS_DETAIL_STABILITY
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
    if (SettingsSliderFloat(ui("NR rendering scale", "NR 渲染比例"),
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
        "Sets only NR work from 50% to 100% for the effective pre/post route. It does not change the game's render resolution. New installs use pre-SR 100% (N=R); existing presets/custom requests are retained. 100% uses the route's full input.",
        "控制 DLSS5 NR AI 渲染的分辨率。数值越高，通常细节越完整，但会占用更多显卡性能，帧率可能降低。此处只调整 NR，游戏本身的分辨率不变。"));
    if (before_sr &&
        g_work_request_rejected.load(std::memory_order_acquire)) {
        SettingsNote(ui(
            "The INI work request was rejected. Previous valid precision is retained; 1-49% is unavailable, not silently raised to 50%.",
            "INI 工作比例请求无效，已保留上一有效精度；1–49% 尚不可用，不会暗中提高到 50%。"));
    }
#endif
    SettingsSection(ui("Model details", "模型细节"));
    SettingsField(ui("Style", "风格"));
    const char *style_labels[] = {ui("Default###style-option", "默认###style-option"),
        ui("Natural###style-option", "自然###style-option"),
        ui("Cinematic###style-option", "电影###style-option")};
    const float style_width = std::max(1.0f,
        (SettingsContentWidth() - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f);
    ImGui::PushID("nr-model-style");
    for (unsigned option = 0; option < 3; ++option) {
        if (option != 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(option));
        const bool selected = next.style == option;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_Header));
        const bool clicked = ImGui::Button(style_labels[option],
            ImVec2(style_width, ImGui::GetFrameHeight() * 1.25f));
        if (selected) ImGui::PopStyleColor();
        if (clicked && !selected) {
            next.style = option;
            changed = true;
            apply_discrete_edit = true;
        }
        SettingsTooltip(ui("Choose the AI picture style. The selected option stays highlighted; switching briefly reloads the model.",
            "选择 AI 画面风格。当前选择会高亮显示；切换后模型会短暂重新加载。"));
        ImGui::PopID();
    }
    ImGui::PopID();

        changed |= SettingsSliderFloat(ui("Model strength", "模型强度"),
            "##nr-model-strength", &next.intensity, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Tunes the model itself; recommended 1.2. Extra enhancement controls the final mix separately. Higher values may look less natural.",
            "控制 AI 生成画面时的处理力度，推荐 1.2。调高可能改变更多细节，也可能更不自然。它与“额外增强”不同；调整后模型会短暂重新加载。"));
        if (next.intensity != 1.2f) {
            ImGui::TextDisabled("%s", ui("Recommended model strength is 1.2; your saved value is shown above.",
                "推荐模型强度为 1.2；上方显示的是已保存的设置。"));
            if (ImGui::Button(ui("Restore model strength 1.2 only###nr-model-default", "仅恢复模型强度 1.2###nr-model-default"))) {
                next.intensity = 1.2f;
                changed = true;
                apply_discrete_edit = true;
            }
        }
        changed |= SettingsSliderFloat(ui("Local contrast", "局部明暗对比"),
            "##nr-local-tone", &next.local_tone, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Changes local contrast inside the NR model. It is separate from final blend strength.", "控制 AI 对相邻区域明暗差异的处理。调高通常对比更明显；不是屏幕亮度开关。"));
        changed |= SettingsSliderFloat(ui("Overall detail", "整体细节强度"),
            "##nr-local-structure", &next.local_structure, 0.f, 2.f, "%.2f");
        SettingsTooltip(ui("Adjusts the model local structure parameter; stronger values can amplify unstable fine detail.", "控制 AI 对全画面纹理和轮廓的处理。调高通常细节更突出，过高也可能带来闪烁或不自然的纹理。"));
        if (ImGui::Checkbox(ui("Face adjustment", "人脸调节"), &next.auto_mask)) {
            if (next.auto_mask && next.skin_structure < 0.f)
                next.skin_structure = 0.f;
            changed = true;
        }
        SettingsTooltip(ui("Requests the model's built-in face/skin structure control, not a final-composition face exemption or a mask generated by this plugin. Its effect depends on the model and input. Off sends skin strength -1 to follow Local structure; the last independent value is retained for re-enabling.", "让 AI 单独调整它识别到的人脸细节。开启后可用下方滑块调整人脸细节；关闭后随整体细节一起变化。它不能锁定人脸，也不能保证每个角色都被识别。其他画质设置仍会影响人脸。"));
        if (next.auto_mask) {
            changed |= SettingsSliderFloat(ui("Face detail strength", "人脸细节强度"),
                "##nr-skin-detail", &next.skin_structure, 0.f, 2.f, "%.2f");
            SettingsTooltip(ui("Independent skin structure request while Face adjustment is enabled. Legacy negative values start at 0; existing nonnegative values are retained. This is not guaranteed face detection or a measured picture-strength value.", "控制 AI 对已识别皮肤的细节处理。数值越高通常越强调纹理；设为 0 也不会恢复整张脸的原图，因为整体明暗、颜色和效果强度仍会作用于人脸。调整后模型会短暂重新加载。"));
        } else {
            SettingsNote(ui("Enable Face adjustment to adjust face detail; currently follows Local structure.", "人脸当前随整体细节一起调整；开启上方选项后才使用独立强度。"));
        }
        changed |= ImGui::Checkbox(ui("UI correction", "文字与界面保护"), &next.ui_correction);
        SettingsTooltip(ui("Requests the model UI-correction parameter with its unsigned type. This plugin does not supply a separate HUD mask here; preservation of text or HUD is not guaranteed. New installations enable it; existing explicit settings are kept.", "请求 AI 尽量保留文字和游戏界面的结构。建议开启，但不能保证所有字幕和界面元素都不变。"));
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
    if (SettingsSliderFloat(ui("Extra enhancement", "额外增强"),
            "##nr-strength-amplification", &effect_strength,
            0.0f, maximum_strength, "%.2f")) {
        next.transfer_strength = next.post_transfer_strength = effect_strength; changed = true;
    }
    SettingsTooltip(ui("Controls the overall final NR mix, including visible brightness and detail. 0 keeps native SR; 1 is the base effect. NR tone follow separately controls only hue and saturation.",
        "控制最终画面保留多少 AI 效果，包含明暗和细节。0 为游戏原图，1 为正常效果；超过 1 会放大变化，也可能失真。颜色可用“AI 色彩强度”另外调整。"));

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
        changed |= ImGui::Checkbox(ui("Highlight mapping", "高光映射"), &next.experimental_peak_shoulder);
        SettingsTooltip(ui("Unknown HDR defaults to luminance only. Storage, exposure and ownership safety remain active.",
            "改变强光送入 AI 前的压缩方式，尽量保留灯光、天空等亮部的层次。此项仍在验证，出现亮度异常时关闭；它不会替你开启游戏 HDR。"));
        changed |= SettingsSliderFloat(ui("Maximum luminance ratio", "明暗变化上限"),
            "##nr-max-luma-ratio", &next.max_luma_ratio, 1.f, 8.f, "%.2f");
        SettingsTooltip(ui("Limits how far the AI result can brighten or darken the original. Higher values allow larger changes; they do not add detail by themselves.", "限制 AI 相对原画面最多能提亮或压暗多少。调高会允许更大的明暗变化，但不会凭空增加细节。"));
        if (ImGui::CollapsingHeader(ui("Developer options", "开发者选项"))) {
        SettingsNote(ui("Experimental controls; existing saved values are retained.", "以下为实验调节，通常保持默认即可；收起菜单不会改变已保存的设置。"));
    changed |= ImGui::Checkbox(ui("Colour protection (default on)", "色彩保护（默认开启）"), &next.color_protection);
    SettingsTooltip(ui("Limits extra colour amplification when Extra enhancement exceeds 1, helping avoid overly strong colours. Recommended on. Turning it off only removes this colour limit; HDR and invalid-value protections remain.",
        "当“额外增强”超过 1 时，限制颜色被过度放大，减少颜色过浓的情况。建议保持开启；关闭仅取消这项限制，其他 HDR 和异常数值保护仍然有效。"));
    changed |= ImGui::Checkbox(ui("Independent luma / chroma composition (experimental)",
        "分开控制 AI 明暗与颜色（实验）"), &next.independent_chroma_composition);
    SettingsTooltip(ui("Uses the shared candidate before or after SR. Nonzero final strength controls luminance; Colour follow independently controls chroma with a common colour budget. Zero final strength still bypasses all edits. Off restores the original route. HDR colour permission is unchanged; no extra model call. GPU cost and game quality await validation.",
        "前置和后置均可使用此候选。非零最终强度控制亮度，颜色跟随独立控制色度并使用共同颜色预算；最终强度为零仍关闭全部修改。关闭恢复原路线。HDR 颜色准入不变，不增加模型调用；GPU 开销与游戏画质尚待验证。"));
    ImGui::BeginDisabled(!before_sr && !next.independent_chroma_composition);
    changed |= ImGui::Checkbox(ui("Low-grid luminance edit field (experimental)",
        "低分辨率亮度合成（实验）"), &next.low_grid_edit_field);
    SettingsTooltip(ui("Effective only before SR with Colour follow zero. Computes a luma edit at NR size, then restores it; this changes the interpolation order and may change edges. Source protection remains at full resolution. Adds one optional pass and bounded storage; unavailable resources retain the existing composition. GPU benefit and game quality are unverified.",
        "仅前置且颜色跟随为零时生效。在 NR 尺寸计算亮度编辑后恢复，会改变插值顺序及可能的边缘观感。原图保护仍在全分辨率。增加一次可选处理和有界存储，资源不可用则保留原合成；GPU 收益和游戏画质未验证。"));
    ImGui::BeginDisabled(next.low_grid_color_edit_field || next.color_strength > 0.000001f);
    changed |= ImGui::Checkbox(ui("Catmull-Rom edit restore (experimental; off = BilinearSafe)",
        "更锐利的细节恢复（实验）"), &next.catmull_edit_restore);
    SettingsTooltip(ui("Requires low-grid luminance editing and zero colour follow. Uses a bounded FP16 horizontal plane; vertical restoration stays in final composition. Invalid footprints or unavailable resources retain valid BilinearSafe. No speed or quality claim.",
        "需低网格亮度编辑且颜色跟随为零。使用有界FP16横向中间面，纵向恢复在最终合成内完成。无效邻域或资源不可用则退有效双线性；不保证速度或画质收益。"));
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox(ui("Low-grid stable colour edit field (experimental)",
        "低分辨率色彩合成（实验）"), &next.low_grid_color_edit_field);
    SettingsTooltip(ui("Before SR only. Stores log luminance plus normalized opponent-colour change at NR size, with explicit near-black and finite validity. Interpolation happens before full-resolution colour reconstruction; source edge, HDR headroom, ratio, gamut and finite guards remain final. This is not a face mask and does not add another NR call. Off keeps the existing RG32F luminance field and composition paths.",
        "仅用于前置路线。在 NR 尺寸保存对数亮度及归一化对手色变化，并显式标记近黑和有限性。先插值、再在全分辨率重建色彩；原图边缘、HDR 余量、倍率、色域及有限值保护仍在最终阶段。这不是人脸遮罩，也不增加 NR 调用。关闭时保留现有 RG32F 亮度场与合成路径。"));
    ImGui::BeginDisabled(!before_sr);
    int input_filter=static_cast<int>(next.nr_input_filter);
    if(ImGui::Combo(ui("NR input filter", "NR 输入滤波"),&input_filter,
        ui("Exact Area\0Lanczos-2 AA (experimental)\0", "精确面积\0Lanczos-2 抗混叠（实验）\0"))) {
        next.nr_input_filter=static_cast<unsigned int>(input_filter);changed=true;
    }
    SettingsTooltip(ui("Only changes pre-SR model input downsampling. 1:1 retains Exact Area. Lanczos preserves signed intermediate lobes, then bounds the final model input; preparation failure keeps Area. Changing the input retires old history. This adds a pass and memory, with unverified game quality and GPU cost.",
        "只改变前置模型输入的降采样；1:1 保留精确面积。Lanczos 在中间面保留有符号负瓣，最后限制模型输入范围；准备失败保留面积核。切换会退休旧历史。增加处理步骤与内存，游戏画质和 GPU 成本尚未验证。"));
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox(ui("High-strength protection (experimental)",
        "高强度颜色与亮度保护（默认开启）"), &next.high_strength_protection);
    SettingsTooltip(ui("Above 1x effective strength, soft-limits extra luminance; SDR uses remaining headroom. Applies before SR and to the optional shared post candidate. Legacy pre composition also soft-limits extra chroma; independent chroma uses its own colour budget. This does not fix an already clipped 1x result or guarantee stable pictures at 4x.",
        "有效强度高于 1 时软限制额外明暗增强，SDR 参考剩余空间；适用于前置及可选的共用后置候选。旧前置同时软限制额外色彩，独立色度候选采用自己的颜色预算。不能修复 1 倍已经剪裁的结果，也不保证 4 倍画面稳定。"));
    ImGui::EndDisabled();
    if (ImGui::CollapsingHeader(ui("Layered composition (one NR)", "分层强化（单次 NR）"))) {
        ImGui::BeginDisabled(!before_sr && !next.independent_chroma_composition);
        changed |= ImGui::Checkbox(ui("Enable layered composition", "启用分层强化"), &next.layered_enhancement);
        ImGui::BeginDisabled(!next.layered_enhancement);
        changed |= ImGui::SliderFloat(ui("Broad tone contribution", "大范围明暗强度"), &next.layer_tone, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Structure contribution", "细节强度"), &next.layer_structure, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Brightening contribution", "亮部增强"), &next.layer_bright, 0.f, 2.f, "%.2f");
        changed |= ImGui::SliderFloat(ui("Darkening contribution", "暗部增强"), &next.layer_shadow, 0.f, 2.f, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::Button(ui("Reset only these layers", "仅恢复分层默认"))) {
            next.layered_enhancement = false;
            next.layer_tone = next.layer_structure = next.layer_bright = next.layer_shadow = 1.f;
            changed = true;
        }
        ImGui::TextWrapped("%s", ui("Experimental pre-SR or shared post-candidate composition, not repeated model inference. 1 is neutral. Combined luminance strength cannot exceed 4. Colour follow remains independent under HDR and colour. The split is not a face/material mask and cannot recover detail absent from the model. No extra NR pass; GPU cost still needs measurement.",
            "实验前置或共用后置候选的合成，不是多次运行模型。1 为中性，组合后的有效明暗强度最多 4。颜色跟随仍在 HDR 与颜色中独立控制。这不是人脸或材质遮罩，不能恢复模型没有的细节。不增加 NR 次数；实际 GPU 开销仍需测量。"));
        ImGui::EndDisabled();
    }
#if NR_HAS_DETAIL_STABILITY
        int curve = static_cast<int>(next.proxy_curve);
        if (ImGui::Combo(ui("HDR mapping", "HDR 映射"), &curve,
                ui("Legacy shoulder\0Hybrid\0Reversible (experimental)\0", "柔和压缩\0混合压缩\0可逆压缩（实验）\0"))) {
            next.proxy_curve = static_cast<unsigned>(curve); changed = true;
        }
#endif
        changed |= ImGui::Checkbox(ui("Allow experimental HDR colour", "允许实验 HDR 调色"), &next.allow_unverified_hdr_color);
        SettingsTooltip(ui("Allows colour edits for an unverified HDR input. Turn off if colour shifts or flicker appear; the AI colour slider never changes this permission.", "当前 HDR 颜色无法确认时，允许尝试 AI 调色。出现偏色或闪烁时关闭；调整 AI 色彩强度不会自动开启此项。"));
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
            "这是只读观测。游戏真实渲染比例由游戏内 DLSS 质量档位调整，与 NR 渲染比例相互独立。"));

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
        if (collecting) ImGui::EndDisabled();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ui("Feedback and diagnostics###nr-feedback-diagnostics", "反馈与诊断###nr-feedback-diagnostics"),
            nullptr, feedback_tab_flags)) {
        DrawSimpleFeedbackControls();
        SettingsSection(ui("Image evidence", "图像证据"));
        DrawSimpleImageCaptureControls();
        SettingsSection(ui("Runtime observations", "运行诊断"));
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
        const auto result = pending_picture_reset
            ? ApplyPictureReset(pending, pending_generation)
            : ApplyOverlayConfig(pending, pending_generation);
        if (result != OverlayApplyResult::Busy) { has_pending = false; pending_picture_reset = false; }
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;
    }
    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已被快捷键、配置重载或采集恢复更新；旧编辑未覆盖新值，请在当前值上重新调整。"));
    ImGui::PopStyleVar(2);
}
