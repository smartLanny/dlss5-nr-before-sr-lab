#include "nr_present_api_status.h"
#include "nr_recommended_picture.inl"
#include "nr_precision_ui.h"
#include "nr_model_controls.h"

// Pure value access for the earlier F7/F8 UI; capture implementations are fully
// defined before this settings include. No render lock or GPU call is needed.
namespace nr_compat_present_runtime {
unsigned CaptureStatus(bool fg){return fg?nr_fg_capture::status.load(std::memory_order_acquire):nr_present_capture::status.load(std::memory_order_acquire);}
unsigned CaptureWorkers(bool fg){return fg?nr_fg_capture::workers.load(std::memory_order_acquire):nr_dual_capture::workers.load(std::memory_order_acquire);}
bool CaptureUsesFg(){return nr_fg_runtime::CapturePreferred();}
}

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
    if(SettingsContentWidth()>ImGui::GetFontSize()*28.f) ImGui::SameLine();
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

#include "nr_light_style_ui.inl"
#include "nr_layers_ui.inl"
#include "nr_dual_ui.inl"
#include "nr_face_diagnostic_ui.inl"
#include "nr_face_region_selection.inl"
#include "nr_settings_notices_ui.inl"

nr_face_diagnostic_ui::View MakeFaceDiagnosticUiView(
    reshade::api::effect_runtime *runtime, const ChainConfig &config) {
    nr_face_diagnostic_ui::View view;
    unsigned display_width = 0, display_height = 0;
    if (runtime) runtime->get_screenshot_width_and_height(&display_width, &display_height);
    view.geometry = nr_f8::SnapshotFaceDiagnosticGeometry(display_width, display_height);
    view.enabled = config.enabled;
    const auto status = nr_f8::g_status.load(std::memory_order_acquire);
    view.blocked = nr_f8::IsActive() || status == nr_f8::Status::Packaging || nr_core_feedback::Busy();
    const auto progress = nr_f8::SnapshotFaceDiagnosticProgress();
    view.progress_visible = progress.visible;
    view.active = progress.active;
    view.packaging = progress.visible && progress.status == nr_f8::Status::Packaging;
    view.complete = progress.status == nr_f8::Status::Complete;
    view.partial = progress.status == nr_f8::Status::Partial;
    view.failed = progress.status == nr_f8::Status::Failed;
    view.roi_ready = progress.roi_ready; view.screenshot_ready = progress.screenshot_ready;
    view.restored = progress.restored; view.group = progress.group; view.total = progress.total;
    return view;
}
nr_face_diagnostic_ui::View FaceSelectionView(reshade::api::effect_runtime *runtime) {
    return MakeFaceDiagnosticUiView(runtime, SnapshotOverlayConfig().config);
}
void FocusFaceDiagnosticPage() { nr_f8::RequestSettingsFeedbackPage(); }
void OnFaceSelectionRuntimeDestroyed(reshade::api::effect_runtime *runtime) {
    CleanupFaceDiagnosticSelection(runtime);
    nr_f8::RuntimeDestroyed(runtime);
}
void DrawFaceDiagnosticControls(reshade::api::effect_runtime *runtime, const ChainConfig &config) {
    const auto snapshot = SnapshotFaceRegionSelection();
    auto selection = snapshot.selection;
    auto view = MakeFaceDiagnosticUiView(runtime, config);
    view.blocked = view.blocked || snapshot.requested || snapshot.registered || snapshot.restore_panel;
    const auto requests = nr_face_diagnostic_ui::Draw(selection, view, UseChineseUi());
    if (requests.choose_region)
        selection.request_failed = !QueueFaceRegionSelection(runtime, selection);
    if (requests.cancel) nr_f8::Cancel();
    if (requests.start && runtime) {
        selection.request_failed = !nr_f8::RequestFaceDiagnostic(selection.center_x, selection.center_y,
            view.geometry.source_width, view.geometry.source_height);
        if (!selection.request_failed) {
            selection.picking = false;
            runtime->open_overlay(false, reshade::api::input_source::mouse);
        }
    }
    StoreFaceRegionPreference(selection, snapshot.generation);
}

// Publish once through the existing overlay transaction. Both group receipts
// observe the same committed generation while the recursive Win32 lock is held.
OverlayApplyResult ApplySettingsOverlay(ChainConfig next, unsigned expected_generation,
    bool picture_reset, bool light_undo_requested, nr_light_style::UndoState &light_undo,
    nr_model_controls::Action model_action, nr_model_controls::UndoState &model_undo) {
    EnterCriticalSection(&g_chain_cs);
    if (model_action == nr_model_controls::Action::Undo &&
        !model_undo.Available(expected_generation)) {
        LeaveCriticalSection(&g_chain_cs); return OverlayApplyResult::Conflict;
    }
    const auto before = nr_model_controls::Read(g_config);
    const auto result = ApplyLightStyleOverlay(next, expected_generation,
        picture_reset, light_undo_requested, light_undo);
    if (result == OverlayApplyResult::Applied) {
        const auto committed = g_config_generation.load(std::memory_order_relaxed);
        if (picture_reset || model_action == nr_model_controls::Action::Undo)
            model_undo.Clear();
        else if (model_action == nr_model_controls::Action::Reset &&
            !nr_model_controls::Same(before, nr_model_controls::Read(g_config)))
            model_undo.Record(before, committed);
        else model_undo.Observe(committed);
    } else if (result == OverlayApplyResult::Conflict) model_undo.Clear();
    LeaveCriticalSection(&g_chain_cs);
    return result;
}

bool DrawProcessingModeBlocks(ChainConfig &next, bool rr_guarded,
    bool compatible_post_locked, int effective_mode) {
    (void)rr_guarded; (void)compatible_post_locked; (void)effective_mode;
    const bool zh=UseChineseUi(); bool changed=false;
    const char* labels[]={zh?"先增强后放大":"Enhance first",
        zh?"先放大后增强":"Upscale first",zh?"自动兼容":"Compatibility"};
    const float width=std::max(1.f,(SettingsContentWidth()-2.f*ImGui::GetStyle().ItemSpacing.x)/3.f);
    const bool stacked=width<ImGui::GetFontSize()*6.f;
    ImGui::PushID("nr-processing-mode");
    for(unsigned i=0;i<3;++i){
        if(i&&!stacked)ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        const auto route=static_cast<nr_processing::Start>(i);
        const bool selected=next.processing_start==route;
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        if(ImGui::Button(labels[i],ImVec2(stacked?SettingsContentWidth():width,ImGui::GetFrameHeight()*1.25f))){
            next.processing_start=route;
            next.processing_selection=nr_processing_runtime::next_selection.fetch_add(1,std::memory_order_relaxed);
            if(i<2)next.mode=i==0?2:1;
            changed=true; // Clicking the already-selected button explicitly retries its starting point.
        }
        if(selected)ImGui::PopStyleColor();
        SettingsTooltip(i==0?(zh?"先增强游戏场景，再由原生 SR 放大。不兼容时自动尝试后置，再尝试兼容模式。":"Enhances the scene before native SR. Automatically tries post-SR and then compatibility when unavailable."):
            i==1?(zh?"原生 SR／RR 完成后增强；不兼容时自动尝试兼容模式。":"Enhances the native SR/RR result. Automatically tries compatibility when unavailable."):
            (zh?"供原生增强入口不可用时尝试：处理接近输出的整张游戏画面，可能包含字幕和界面。当前仍需近期已提交的原生 SR／RR 深度和运动数据；缺少可信数据时跳过。\n\n当前尚未完整支持 FG 开启时的最终整图增强。检测到 FG 接管呈现后，临时尝试原生 SR 后、FG 前增强，沿用后置精度；没有合法入口时不执行 NR。此时不是最终整图处理，所选模式和游戏 FG 设置保持；关闭 FG 后恢复兼容路径。直接处理 FG 输出存在真实帧与生成帧效果不一致、闪烁风险，不代表所有 FG 游戏必然闪烁。\n\n开销取决于 AI 渲染精度和层数。无效或闪烁请按 F8 收集日志；兼容模式仍以扩大可用入口为目标。":"Try this when native enhancement entries are unavailable. Processes the game image near presentation, possibly including subtitles and HUD. Currently still needs recent submitted native SR/RR depth and motion; skips without trusted inputs.\n\nFinal-image enhancement with FG is not yet fully supported. When FG ownership is detected, temporarily tries native post-SR, pre-FG NR using post-SR precision; skips NR without a valid entry. This processes the scene rather than the final image, preserving your selected mode and game FG settings. Disabling FG restores the compatibility route. Directly processing FG output risks inconsistent real/generated images and flicker; this does not mean every FG game must flicker.\n\nCost depends on precision and pass count. Press F8 if enhancement is unavailable or flickers. This mode remains intended to broaden compatibility."));
        ImGui::PopID();
    }
    ImGui::PopID();
    nr_processing::Snapshot route_snapshot{};
    const auto actual=nr_processing_runtime::TrySnapshot(route_snapshot)?route_snapshot.current:nr_processing_runtime::Effective();
    const char* names[]={zh?"先增强后放大":"Enhance first",zh?"先放大后增强":"Upscale first",zh?"自动兼容":"Compatibility"};
    ImGui::TextWrapped(zh?"当前入口（运行状态见顶部）：%s":"Current entry (activity shown above): %s",names[static_cast<unsigned>(actual)]);
    if(nr_processing_runtime::FgRedirectRequested())
        ImGui::TextWrapped("%s",zh?"FG 已接管呈现，当前在原生 SR 后、FG 前尝试增强，沿用后置精度；FG 设置和所选起点保持。":"FG owns presentation. NR uses an available post-SR, pre-FG entry with post-SR precision. FG settings and your selected start are preserved.");
    else if(actual!=next.processing_start)
        ImGui::TextWrapped("%s",zh?"已自动切换兼容路径，可能存在兼容问题。请按 F8 收集日志并提交；点击按钮可重新尝试所选位置。":"Compatibility fallback is active. A compatibility issue may exist: press F8 and submit logs. Click a route to retry it.");
    if(actual==nr_processing::Start::Present){
        nr_compat_present::Observation status{};
        if(nr_compat_present_runtime::TrySnapshot(status)){
            ImGui::TextDisabled(zh?"整链工作尺寸 %u × %u，%u 层":"Chain work size %u x %u, %u passes",status.work_width,status.work_height,status.passes);
            if(status.color_only)ImGui::TextWrapped("%s",zh?"导引不足：使用明确标记的占位深度和运动尝试增强，每帧重置历史。未取得可用结果时保留游戏原画面。":"Guides unavailable: trying NR with marked placeholder depth and motion, resetting history each frame. The game image is retained until a usable result is ready.");
        }
    }
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
    // AI colour is independent of lighting presets and their undo receipt.
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
            ImGui::TextDisabled("%s", ui(
                "SR -> NR does not apply Lighting Match. The request is saved for NR -> SR.",
                "SR -> NR 不采用光照匹配。请求值已保留，切回 NR -> SR 后可恢复。"));
    } else {
        ImGui::TextDisabled("%s", NrColorPolicyStatusText(
            color_policy_snapshot.status, use_chinese_ui));
    }
#endif
}

// Use the same recent composition policy as the global colour controls. A
// pending or old sample cannot establish the current game's colour contract.
bool DrawRegionColourStatus(ChainConfig &next, bool has_pending, bool &changed) {
    const bool zh = UseChineseUi();
    (void)changed; // The HDR permission is edited once in Developer options.
    auto snapshot = SnapshotColorPolicy();
    if (snapshot.status != NrColorPolicyStatus::Bypassed &&
        (has_pending || changed || snapshot.mode != EffectivePictureMode(next.mode)))
        snapshot.status = NrColorPolicyStatus::Pending;
    const bool current = snapshot.status == NrColorPolicyStatus::Current;
    if (next.allow_unverified_hdr_color || (current &&
        snapshot.policy.color_reason == NrColorReason::UnverifiedHdr))
        SettingsNote(zh ? "如需尝试 HDR 调色，请在开发者选项中允许实验调色。" :
            "Use Developer options to allow experimental HDR colour when needed.");
    if (next.color_strength <= 0.f) {
        ImGui::TextWrapped("%s", zh ? "AI 色彩强度为 0，区域颜色倍率暂不生效；请先提高 AI 色彩强度。" :
            "AI colour amount is 0, so region colour gains have no effect. Raise the AI colour amount first.");
        return true;
    }
    if (snapshot.status != NrColorPolicyStatus::Current) {
        ImGui::TextWrapped("%s", NrColorPolicyStatusText(snapshot.status, zh));
        return false;
    }
    if (!snapshot.policy.color_allowed) {
        ImGui::TextWrapped("%s", zh ? "当前 HDR 策略暂停 AI 颜色：区域颜色倍率不生效，明暗调节仍可用；可在开发者选项中允许实验调色后再试。" :
            "The current HDR policy pauses AI colour: region colour gains have no effect; luminance controls remain available. Allow experimental HDR colour in Developer options to try it.");
        return true;
    }
    ImGui::TextWrapped("%s", snapshot.policy.color_reason == NrColorReason::ExperimentalUnverifiedHdr
        ? (zh ? "区域颜色已获实验 HDR 许可；出现偏色或闪烁时请在开发者选项中关闭。" : "Region colour has experimental HDR permission; turn it off in Developer options if hue shifts or flicker appear.")
        : (zh ? "区域颜色可用；实际变化取决于模型是否改变了该区域的颜色。" : "Region colour is available; visible changes depend on the model's colour edits in each region."));
    return false;
}

unsigned DrawLightAdjustmentMethodSelector(ChainConfig &next, bool &changed,
    bool &apply_discrete_edit) {
    const bool zh = UseChineseUi();
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    // Generic residual has always had priority when both legacy flags were
    // loaded from an INI. Keep that effective order while presenting one
    // selector so a user cannot accidentally enable two methods.
    unsigned method = next.generic_residual ? 2u : next.region_enabled ? 1u : 0u;
    int selected = static_cast<int>(method);
    SettingsField(ui("Light adjustment method", "光影调节方式"));
    if (ImGui::Combo("##light-adjustment-method", &selected,
            ui("Normal light/dark\0Engine regions (experimental)\0Generic layers (experimental)\0",
               "普通亮暗\0引擎分区（实验）\0通用分层（实验）\0"))) {
        const unsigned requested = static_cast<unsigned>(std::clamp(selected, 0, 2));
        if (requested != method) {
            next.generic_residual = requested == 2u;
            next.region_enabled = requested == 1u;
            changed = true;
            apply_discrete_edit = true;
            method = requested;
        }
    }
    SettingsTooltip(ui(
        "Choose one light adjustment method. Normal light/dark keeps the two legacy controls; Engine regions uses current engine guidance; Generic layers groups normalized RGB/HSL residuals. Saved values in the other methods remain available when you switch back.",
        "选择一种光影调节方式。普通亮暗保留原有两套控制；引擎分区使用当前引擎导引；通用分层按归一化 RGB/HSL 改动分组。切换回来时，其他方式的已保存数值仍保留。"));
    return method;
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
        if (ImGui::Selectable(ui("Model-stage comparison: centre region",
                                  "模型前后对照：画面中心")))
            nr_f8::RequestWithOptions({});
        SettingsTooltip(ui(
            "Move a face or reflective region to the centre and keep the camera still. Saves the original, model input/output and composed result for analysis.",
            "请把人脸或反射区域放在画面中央，并保持镜头不动。保存原图、模型处理前后与最终合成结果，供对照分析。"));
        ImGui::EndCombo();
    }
    SettingsTooltip(ui(
        "Full-screen options save the entire game view at its original screenshot resolution. The seven-group comparison saves NR off, then NR before/after SR at 50%, 75% and 100%; active RR skips NR-before-SR groups. Model-stage comparison captures the centre region: put the face or reflection there and keep the camera still.",
        "全屏选项保存完整游戏画面，不裁切、不缩小。七组对比依次保存关闭 NR，以及 NR→SR、SR→NR 各 50%、75%、100% 的画面；开启 RR 时会跳过 NR→SR。模型前后对照采集画面中央区域，请把人脸或反射放在中央并保持镜头不动。"));
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
// All drawing threads observe one bounded process-counter window. The short
// CPU lock covers the reads as well as Observe, so a delayed reader cannot
// apply older time/counters after a newer observation. Drawing stays outside.
nr_ui_activity::Sample ReadUiActivity(unsigned config_generation,
                                    nr_ui_activity::ReadReceipt* receipt=nullptr) {
    auto& thread=NrUiActivityThread();
    NrUiActivityWindowGuard guard;
    auto& window=g_nr_ui_activity_window;
    nr_ui_activity::ReadReceipt observation{};
    const auto epoch=g_pipeline_stats_epoch.load(std::memory_order_acquire);
    const nr_ui_activity::Counters counters{g_carrier_evaluations.load(),
        g_nr_successful_evaluations.load(),g_nr_transfer_submitted_frames.load(),
        g_queue_affinity_unknown_bypass_frames.load()};
    const auto reason=g_last_bypass_reason.load(std::memory_order_acquire);
    const auto config_checked=g_config_generation.load(std::memory_order_acquire);
    const bool epoch_checked_known=config_checked==config_generation;
    const auto epoch_checked=epoch_checked_known?
        g_pipeline_stats_epoch.load(std::memory_order_acquire):0;
    const auto now=GetTickCount64();
    NrUiActivityReadIdentity(thread,observation,now);
    observation.config_argument=config_generation;observation.epoch_entered=epoch;
    observation.config_checked=config_checked;observation.epoch_checked=epoch_checked;
    observation.epoch_checked_known=epoch_checked_known;
    if (config_checked!=config_generation || (epoch_checked_known && epoch_checked!=epoch)) {
        auto& detail=observation.observation;
        detail.before=window.Inspect();detail.current=counters;detail.input_scope={config_generation,epoch};
        detail.now=now;detail.live_reason=reason;detail.kind=nr_ui_activity::Kind::Incoherent;
        detail.gap_known=detail.before.initialized && now>=detail.before.last_observed;
        if(detail.gap_known)detail.gap_ms=now-detail.before.last_observed;
        detail.reset_mask=config_checked!=config_generation?nr_ui_activity::ConfigReadChanged:
            nr_ui_activity::EpochReadChanged;
        // An obsolete caller must not erase a newer, already observed scope.
        // An incoherent read of the currently held scope still invalidates it.
        if(detail.before.scope.config==config_generation && detail.before.scope.epoch==epoch)
            window={};
        if(receipt)*receipt=observation;
        return {};
    }
    const auto sample=window.Observe(now,counters,{config_generation,epoch},reason,&observation.observation);
    if(receipt)*receipt=observation;
    return sample;
}

void DrawNrActivityStatus(reshade::api::effect_runtime *runtime,
                          const ChainConfig &config,unsigned generation,nr_ui_activity::Site site) {
    if(static_cast<unsigned>(site)==0u)DrawPerformanceRouteNotice(config); // D21 top-panel only
    const bool zh=UseChineseUi();
    const auto ui=[zh](const char *en,const char *cn){return zh?cn:en;};
    nr_processing::Snapshot route_snapshot{};
    const bool actual_present=nr_processing_runtime::TrySnapshot(route_snapshot)?
        route_snapshot.current==nr_processing::Start::Present:nr_processing_runtime::PresentSelected();
    if(actual_present && config.enabled && ActiveTransferStrength(config)>0.000001f){
        // This branch observes compatibility state, not the native counter
        // Window. Publish its real display with explicitly unavailable counter
        // tuples so a previous inline receipt cannot masquerade as this UI.
        nr_ui_activity::ReadReceipt receipt{};
        receipt.source=nr_ui_activity::ObservationSource::PresentSnapshot;
        receipt.config_argument=generation;
        receipt.observation.now=GetTickCount64();
        receipt.observation.input_scope.config=generation;
        receipt.observation.kind=nr_ui_activity::Kind::Snapshot;
        NrUiActivityReadIdentity(NrUiActivityThread(),receipt,receipt.observation.now);
        using Display=nr_ui_activity::Display;
        Display displayed=Display::CompatibilityBypassed;
        nr_compat_present::Observation cp{};
        if(nr_fg_runtime::RecentlyRecorded(generation)) {
            receipt.source=nr_ui_activity::ObservationSource::FgRecentRecord;
            displayed=Display::FgRecorded;
            ImGui::TextWrapped("%s",ui("Compatibility processing recorded before frame generation. Use F6 to compare the picture.","兼容模式已在帧生成前记录增强处理，按 F6 对比实际效果。"));
        } else if(!nr_compat_present_runtime::TrySnapshot(cp)) {
            displayed=Display::CompatibilityChecking;
            receipt.observation.kind=nr_ui_activity::Kind::Unavailable;
            ImGui::TextWrapped("%s",ui("Checking compatibility processing...","正在检查兼容模式……"));
        } else if(cp.status==nr_compat_present::Status::Submitted || cp.status==nr_compat_present::Status::Completed) {
            displayed=Display::PresentSubmitted;
            ImGui::TextWrapped("%s",ui("Compatibility processing submitted. Use F6 to compare the picture.","兼容模式已提交增强画面，按 F6 对比实际效果。"));
        } else if(cp.status==nr_compat_present::Status::Creating || cp.status==nr_compat_present::Status::Preparing ||
                cp.status==nr_compat_present::Status::Switching || cp.status==nr_compat_present::Status::Draining || cp.status==nr_compat_present::Status::Busy ||
                cp.status==nr_compat_present::Status::RetryBackoff) {
            displayed=Display::CompatibilityPreparing;
            ImGui::TextWrapped("%s",ui("Compatibility processing is preparing or waiting for in-flight work.","兼容模式正在准备，或等待已提交的工作完成。"));
        } else ImGui::TextWrapped("%s",ui("This frame could not use compatibility processing. A compatibility issue may exist: press F8 and submit logs.","当前帧未能进行兼容处理，可能存在兼容问题。请按 F8 收集日志并提交。"));
        NrPublishUiActivity(receipt,site,displayed);
        return;
    }
    nr_ui_activity::ReadReceipt receipt{};
    const auto activity=ReadUiActivity(generation,&receipt);
    using Display=nr_ui_activity::Display;
    // The platform assembler replaces the first API-warning body. Its branch
    // identity stays the same; every subsequent branch sets its own identity.
    Display displayed=Display::WrongApi;
    // A carrier can safely skip between two submitted NR frames. Keep the
    // entire summary on one window instead of contradicting it every frame.
    // The unfiltered per-carrier reason and counters remain in diagnostics/F8.
    const auto reason=static_cast<BypassReason>(activity.bypass_reason);
    if (nr_present_api_status::IsD3d11PresentRuntime(runtime))
        ImGui::TextWrapped("%s", ui(
            "This ReShade window is using Direct3D 11, so DLSS 5 NR cannot run. Switch the game to Direct3D 12, then install ReShade Full Add-on for Direct3D 12.",
            "当前 ReShade 画面使用 Direct3D 11，本插件无法运行。请把游戏切换到 Direct3D 12，并为 D3D12 安装 ReShade Full Add-on。"));
    else if (!config.enabled || ActiveTransferStrength(config) <= 0.000001f) {
        displayed=Display::Native;
        ImGui::TextWrapped("%s", ui("NR is off; game output continues.",
            "NR 已关闭，显示游戏原画面。"));
    }
    else if (reason == BypassReason::ContextReleasePending) {
        displayed=Display::Checking;
        ImGui::TextWrapped("%s", ui("Releasing the previous model before applying the new settings. Native output continues; your selected scale and route are retained.",
            "正在释放旧模型，随后应用新设置。期间继续显示游戏原图；所选比例和处理顺序不变。"));
    }
    else if (!activity.valid) {
        displayed=Display::Checking;
        ImGui::TextWrapped("%s", ui("Checking recent NR activity...", "正在检查近期 NR 处理状态……"));
    }
    else if (activity.Intermittent()) {
        displayed=Display::Intermittent;
        ImGui::TextWrapped("%s", ui(
            "NR is processing intermittently. Some evaluations are skipped while queue synchronization requirements are unmet. Use F6 to compare and F8 to report.",
            "NR 正在间歇处理；部分评估因队列同步条件未满足而跳过。按 F6 对比画面，按 F8 反馈。"));
    }
    else if (activity.Processing()) {
        displayed=Display::Observed;
        ImGui::TextWrapped("%s", ui("Processing: recent NR output submitted.",
            "处理中：近期已提交 NR 输出。"));
    }
    else if (reason == BypassReason::QueueRetirementTimeout) {
        displayed=Display::QueueTimeout;
        ImGui::TextWrapped("%s", ui("Submitted NR did not finish within the bounded wait. Native output is retained for these settings; use F8 before retrying with F6.",
            "上一轮 NR 未在等待上限内完成；当前设置保持原生输出。请先按 F8 记录，再用 F6 重新尝试。"));
    }
    else if (reason == BypassReason::RuntimeUnavailable) {
        displayed=Display::RuntimeUnavailable;
        ImGui::TextWrapped("%s", ui("NR runtime could not load or initialize. Keep your NVIDIA DLLs; install the matching nrchain_nvngx.dll and nvngx_dlssnr.dll beside the addon. Use F8 for the actual failure record.",
            "NR 运行库加载或初始化失败。保留显卡对应 NVIDIA DLL，将配套 nrchain_nvngx.dll 与 nvngx_dlssnr.dll 放在插件旁；按 F8 记录具体失败。"));
    }
    else if (reason == BypassReason::CodecUnavailable) {
        displayed=Display::CodecUnavailable;
        ImGui::TextWrapped("%s", ui("The NR conversion/composition pipeline could not initialize. Native output is preserved; use F8 for the actual failure record.",
            "NR 转换/合成管线初始化失败，保留原生输出；请用 F8 记录具体失败。"));
    }
    else if (reason == BypassReason::RayReconstructionGuard) {
        displayed=Display::RayReconstruction;
        DrawPostRouteStatus();
    }
    else if (reason == BypassReason::ShortFuseOwnsNr) {
        displayed=Display::OtherOwner;
        ImGui::TextWrapped("%s", ui("Another NR plugin is active. Disable its NR effect before using this one.",
            "另一个 NR 插件正在运行。请先关闭它的 NR 效果，再使用本插件。"));
    }
    else if (reason == BypassReason::GenericRenoDxConflict) {
        displayed=Display::Conflict;
        ImGui::TextWrapped("%s", ui(
            "Duplicate NR plugin detected: remove renodx-dlss5.addon64 and restart the game.",
            "检测到重复 NR 插件：请删除 renodx-dlss5.addon64，然后重启游戏。"));
    }
    else if (reason == BypassReason::WaitingForFeature1 || reason == BypassReason::ModuleObserverInstalling) {
        displayed=Display::WaitingForSr;
        ImGui::TextWrapped("%s", ui("Waiting for native DLSS SR. Enter gameplay and enable DLSS in the game settings.",
            "等待游戏原生 DLSS SR。请进入实际场景，并在游戏设置里开启 DLSS。"));
    }
    else if (reason == BypassReason::ModuleObserverUncertain) {
        displayed=Display::ObserverIncomplete;
        ImGui::TextWrapped("%s", ui(
            "DLSS lifecycle observation is incomplete. NR stays off to protect SR/RR and any existing FG/MFG. F8 includes the first hook failure; changing NR strength cannot repair this.",
            "DLSS 生命周期观察不完整，NR 已安全旁路以保护原生 SR/RR 和现有 FG/MFG。F8 会保留首次 Hook 失败；调高强度不能解决此问题。"));
        const auto failure = SnapshotNgxHookFailure();
        if (failure.available)
            ImGui::TextWrapped(ui("First hook failure (historical): %s / %s", "首次 Hook 失败（历史记录）：%s / %s"),
                failure.operation, failure.entry);
    }
    else if (reason == BypassReason::CommandStateUnverified) {
        displayed=Display::CommandStateUnknown;
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
    else if (reason != BypassReason::None) {
        displayed=Display::Bypassed;
        ImGui::TextWrapped(ui("NR is temporarily skipped (%s). Keep the scene and collect feedback with F8.",
            "NR 当前已跳过（%s）。请保留问题场景，按 F8 收集反馈。"), BypassReasonName(reason));
    }
    else {
        displayed=Display::NoSubmission;
        ImGui::TextWrapped("%s", ui("No new NR output submission observed in the recent interval. Enter gameplay; use F8 if this persists.",
            "近期暂未观察到新的 NR 画面输出提交。请进入实际场景；持续无效时按 F8 反馈。"));
    }
    NrPublishUiActivity(receipt,site,displayed);
    SettingsTooltip(ui("Updated about once per second from recent process counters. Submission is not proof of presentation or picture quality. Per-evaluation reasons remain in F8.",
        "约每秒根据近期处理计数更新。提交不等于已显示或画质已改善；每次评估的具体原因仍保留在 F8。"));
}

void DrawSettings(reshade::api::effect_runtime *runtime) {
    const bool use_chinese_ui = UseChineseUi();
    const auto ui = [use_chinese_ui](const char *en, const char *zh) { return use_chinese_ui ? zh : en; };
    static ChainConfig pending = {};
    static bool has_pending = false;
    static bool pending_picture_reset = false;
    static bool pending_light_undo = false;
    static nr_light_style::UndoState light_undo;
    static nr_model_controls::UndoState model_undo;
    static auto pending_model_action = nr_model_controls::Action::None;
    static unsigned pending_generation = 0;
    static bool edit_conflict = false;
    static bool migration_notice_dismissed = false;
    const auto current = SnapshotOverlayConfig();
    const auto compatibility_snapshot = SnapshotCompatibilityFallback();
    light_undo.Observe(current.generation);
    model_undo.Observe(current.generation);
    if (has_pending && pending_generation != current.generation) {
        has_pending = false; pending_picture_reset = false; pending_light_undo = false; edit_conflict = true;
        pending_model_action = nr_model_controls::Action::None;
    }
    ChainConfig next = has_pending ? pending : current.config;
    bool changed = false;
    // Local styling follows ReShade's palette and never changes another panel.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(ImGui::GetStyle().ItemSpacing.x, 7.0f));
    ImGui::SeparatorText(ui("Zhuangjizhai DLSS 5", "装机宅DLSS 5"));
    ImGui::TextDisabled("%s", kAddonVersion);
    SettingsSection(ui("About and links", "作者与链接"));
    ImGui::TextDisabled("%s", use_chinese_ui ? kAddonAuthor : kAddonAuthorAscii);
    ImGui::TextDisabled("%s", kOfficialReleaseUrl);
    ImGui::TextDisabled("%s", ui("QQ group 2: 392308850", "交流群（二群）：392308850"));
    if (kChineseUi && !use_chinese_ui)
        ImGui::TextWrapped("Chinese glyphs are unavailable in the current ReShade font, so this panel is using English instead of broken squares. In ReShade Settings > Overlay & Styling, choose zh-CN/System Default or C:\\Windows\\Fonts\\msyh.ttc, then restart the game.");
    // Keep controls stationary while live status text changes (#121).
    ImGui::BeginChild("nr-live-status", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.25f), true);
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    if (current.config.enabled && ActiveTransferStrength(current.config)>0.000001f)
        DrawGenericCompatibilityStatus();
#endif
    DrawNrActivityStatus(runtime,current.config,current.generation,nr_ui_activity::Site::Top);


    if (next.identity_use_original)
        ImGui::TextWrapped("%s", ui(
            "Diagnostic identity mode is ON; NR output is discarded. Turn it off to compare the effect.",
            "诊断原图模式已开启，NR 效果被丢弃。请关闭后再对比。"));
    if (g_config_persistence_failed.load())
        ImGui::TextWrapped("%s", ui("Settings cannot be saved. Check that the plugin folder is writable.",
            "设置无法保存，请检查插件目录是否可写。当前设置仍用于本次运行。"));

    ImGui::EndChild();
    DrawConfigPolicyMigrationNotice(SnapshotConfigPolicyMigrationStatus(),
        migration_notice_dismissed);
    bool apply_discrete_edit = false;
    const bool collecting = nr_f8::IsActive();
    if (next.light_preview || next.region_preview) {
        SettingsNote(ui("A lighting/region diagnostic preview is active; this is not the final picture.",
            "光影或分区诊断预览正在运行，当前不是最终画面。"));
        ImGui::BeginDisabled(collecting);
        if (ImGui::Button(ui("Exit active previews", "退出正在运行的预览"))) {
            next.light_preview=0;next.region_preview=0;changed=true;apply_discrete_edit=true;
        }
        ImGui::EndDisabled();
    }

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
        next.processing_start=nr_processing::Start::Before;
        next.processing_selection=nr_processing_runtime::next_selection.fetch_add(1);
        next.compat_percent=100;next.nr_full_reference=false;next.nr_full_passes=3;
        changed = true;
        pending_picture_reset = true;
        pending_model_action = nr_model_controls::Action::None;
        apply_discrete_edit = true;
    }
    SettingsTooltip(ui(
        "Restores NR before SR, 100% NR rendering for both routes, model strength 1.2, extra enhancement 1.0, lighting protection 0.2 and colour protection on. Keeps the F6 switch and game compatibility settings. Other picture experiments return to their recommended defaults.",
        "恢复前置 NR、前后置 NR 渲染比例 100%、模型强度 1.2、额外增强 1.0、光照保护 0.2，并开启色彩保护。保留 F6 总开关和游戏兼容设置；其他画质实验选项恢复推荐值。"));
    SettingsSection(ui("Processing order", "处理顺序"));
    const bool rr_guarded = IsRayReconstructionRouteObserved();
    const bool compatibility_post = !rr_guarded && next.mode == 2 &&
        compatibility_snapshot.recent && compatibility_snapshot.effective_mode == 1;
    SettingsField(ui("Processing mode", "处理模式"));
    if (DrawProcessingModeBlocks(next, rr_guarded, compatibility_post, compatibility_post ? 1 : next.mode)) {
        changed = true;
        apply_discrete_edit = true;
    }
    if (rr_guarded) DrawPostRouteStatus();
    if (compatibility_post) DrawCompatibilityFallbackNotice(compatibility_snapshot);
    // BEGIN WORKING SCALE REQUEST: edit the selected start, never live fallback.
    // Other picture controls retain their existing effective-route guards.
    const bool compatibility_mode=!nr_processing_runtime::FgRedirectRequested() &&
        (next.processing_start==nr_processing::Start::Present ||
        (!has_pending && nr_processing_runtime::PresentSelected()));
    const bool before_sr = !compatibility_mode && !rr_guarded && !compatibility_post &&
        nr_processing_runtime::Effective()==nr_processing::Start::Before;
#if NR_HAS_DETAIL_STABILITY
    const auto scale_target=nr_precision_ui::SelectedTarget(
        static_cast<unsigned>(next.processing_start),rr_guarded,
        nr_processing_runtime::FgRedirectRequested());
    const auto post_options=nr_post_settings::Read();
    const auto post_revision=nr_post_settings::revision.load(std::memory_order_acquire);
    const float saved_precision=scale_target==nr_precision_ui::Target::Present
        ? static_cast<float>(next.compat_percent)
        : scale_target==nr_precision_ui::Target::Post ? static_cast<float>(post_options.percent)
        : nr_precision_ui::LegacyEstimate(next.work_mode,next.custom_work_scale,
            g_last_dlss_input_width.load(),g_last_dlss_input_height.load(),
            g_last_dlss_output_width.load(),g_last_dlss_output_height.load());
    static nr_precision_ui::Edit scale_edit;
    float precision=scale_edit.Observe(scale_target,current.generation,post_revision,
        next.processing_selection,std::clamp(saved_precision,nr_precision_ui::MinimumPercent(scale_target),100.f));
    const auto edit_target=scale_edit.target;
    const char* request_name=edit_target==nr_precision_ui::Target::Before
        ? ui("Pre-SR request","前置请求") : edit_target==nr_precision_ui::Target::Post
        ? ui("Post-SR request","后置请求") : ui("Compatibility request","兼容请求");
    ImGui::TextDisabled(ui("Editing: %s","正在设置：%s"),request_name);
    const bool scale_changed=SettingsSliderFloat(ui("Chain working scale", "整链工作比例"),
        "##nr-processing-precision", &precision, nr_precision_ui::MinimumPercent(edit_target), 100.0f,"%.0f%%");
    if (scale_edit.Finish(precision,ImGui::IsItemActivated(),scale_changed,
            ImGui::IsItemActive(),ImGui::IsItemDeactivatedAfterEdit())) {
        if (!nr_precision_ui::SupportedPercent(scale_edit.preview,edit_target)) {
            scale_edit.conflict=true;
        } else if (edit_target==nr_precision_ui::Target::Present) {
            next.compat_percent=static_cast<unsigned>(std::lround(scale_edit.preview));
            changed=true;
        } else if (edit_target==nr_precision_ui::Target::Post) {
            // The ordinary Post request publishes separately from the overlay.
            // Keep its existing chain lock while checking the captured revision,
            // so a concurrent INI/config update cannot be overwritten after check.
            EnterCriticalSection(&g_chain_cs);
            if (g_config_generation.load(std::memory_order_relaxed)==scale_edit.generation &&
                nr_post_settings::revision.load(std::memory_order_acquire)==scale_edit.post_revision) {
                auto request=post_options;
                request.percent=static_cast<unsigned>(std::lround(scale_edit.preview));
                if (!nr_post_settings::Request(request)) scale_edit.conflict=true;
            } else scale_edit.conflict=true;
            LeaveCriticalSection(&g_chain_cs);
        } else {
            const auto request=nr_precision_ui::Resolve(scale_edit.preview,true);
            if (request.accepted) {
                next.work_mode=request.mode;next.custom_work_scale=request.scale;changed=true;
            }
        }
    }
    SettingsTooltip(ui(
        "Sets 50-100% before upscaling, or 25-100% after upscaling and in Compatibility, once for all layers. Automatic fallback does not change this saved request. RR and FG-redirected compatibility use the Post-SR request. Applied on release; game resolution stays unchanged.",
        "前置支持 50–100%，后置与兼容支持 25–100%；所有层共用且仅乘一次。自动降级不切换保存字段；RR 与 FG 重定向兼容模式使用后置请求。松手应用，游戏分辨率不变，实际路线和尺寸单独显示。"));
    if (scale_edit.conflict)
        SettingsNote(ui("The scale edit was not applied: its target or settings changed, or the request was unavailable. Release and adjust the current value again.",
            "比例编辑未应用：编辑目标或设置已变化，或请求暂不可用。请松手后按当前值重新调整。"));
    unsigned observed_base_w=current.base_width,observed_base_h=current.base_height;
    unsigned observed_work_w=current.work_width,observed_work_h=current.work_height;
    if (nr_processing_runtime::Effective()==nr_processing::Start::Present) {
        nr_compat_present::Observation dimensions{};
        if (nr_compat_present_runtime::TrySnapshot(dimensions)) {
            observed_base_w=dimensions.source_width;observed_base_h=dimensions.source_height;
            observed_work_w=dimensions.work_width;observed_work_h=dimensions.work_height;
        } else observed_base_w=observed_base_h=observed_work_w=observed_work_h=0;
    }
    if (observed_base_w&&observed_base_h&&observed_work_w&&observed_work_h)
        ImGui::TextDisabled(ui("Last NR: base %u x %u -> model %u x %u (each layer)",
            "最近运行：基准 %u × %u → 模型 %u × %u（每层）"),
            observed_base_w,observed_base_h,observed_work_w,observed_work_h);
    else SettingsNote(ui("Model dimensions appear after NR runs.","NR 运行后显示实际模型尺寸。"));
    if (scale_target==nr_precision_ui::Target::Before &&
        g_work_request_rejected.load(std::memory_order_acquire)) {
        SettingsNote(ui(
            "The INI work request was rejected. Previous valid precision is retained; 1-49% is unavailable, not silently raised to 50%.",
            "INI 工作比例请求无效，已保留上一有效精度；1–49% 尚不可用，不会暗中提高到 50%。"));
    }
#endif
    DrawUniformLayerControls(next,changed,apply_discrete_edit);
    SettingsSection(ui("Final composition", "最终合成"));
    float effect_strength = ActiveTransferStrength(next);
    if (SettingsSliderFloat(ui("Extra enhancement", "额外增强"),
            "##nr-strength-amplification", &effect_strength, 1.0f, 4.0f, "%.2f")) {
        // Only user edits use 1-4. Internal diagnostic zero keeps its native endpoint.
        effect_strength = std::clamp(FiniteOr(effect_strength, 1.0f), 1.0f, 4.0f);
        next.transfer_strength = next.post_transfer_strength = effect_strength;
        changed = true;
    }
    SettingsTooltip(ui("1 keeps the model's base amount. Higher values amplify its final brightness and detail changes and may look less natural; lower Model strength for a weaker model result.",
        "1 保持模型原量；调高会放大最终明暗和细节，也可能失真。想减弱模型效果请调低模型强度。"));
    SettingsNote(ui("1 = base amount (default).", "1 = 原量（默认）。"));

    // Colour is a common control and stays before the model's face setting.
    DrawColorControls(next, has_pending, changed);
    // END PRIMARY MODEL CONTROLS

    if (next.identity_use_original && ImGui::Button(ui("Show NR picture (leave diagnostic identity)", "显示 NR 效果（退出诊断原图）"))) {
        next.identity_use_original = false; changed = true;
    }
    if (ImGui::CollapsingHeader(ui("Light adjustment", "光影微调"))) {
    if (ImGui::CollapsingHeader(ui("Model input brightness", "模型输入亮度"))) {
        float reference_white=1.f/next.proxy_scale;
        if (SettingsSliderFloat(ui("Reference white brightness", "参考白亮度"),
                "##nr-reference-white",&reference_white,.5f,4.f,"%.2fx")) {
            next.proxy_scale=1.f/reference_white;changed=true;
        }
        SettingsTooltip(ui(
            "Relative white reference for the model's HDR input. 1x keeps the existing mapping. Raising it makes the HDR image seen by NR darker and can change its texture and colour decisions. Uses the existing ProxyScale once; it does not control the display or an external HDR plugin. SDR routes that bypass HDR mapping do not use this setting.",
            "模型理解 HDR 画面时使用的相对白亮度，1 倍沿用原有映射。调高会让模型看到的 HDR 输入变暗，可能改变纹理和颜色效果。与原 ProxyScale 是同一设置，只作用一次；它不调节显示器或外部 HDR 插件。不经过 HDR 映射的 SDR 路径不使用此项。"));
        if (ImGui::Button(ui("Reset reference white", "重置参考白亮度"))) {
            next.proxy_scale=1.f;changed=true;apply_discrete_edit=true;
        }
    }
    const unsigned light_method = DrawLightAdjustmentMethodSelector(next, changed, apply_discrete_edit);
    if (light_method == 2u) {
        SettingsNote(ui("Groups model residuals by luminance and colour without material masks. The engine-guided and legacy values remain saved for comparison.",
            "按明暗和颜色对模型改动分组，不识别材质；引擎导引和原有数值仍会保存，方便切换对照。"));
        const char* en[]={"Total residual","Reflection / glow (bright RGB)","Shadow / structure (dark RGB)","Saturation change","Lightness change"};
        const char* cn[]={"总改动倍率","反射 / 辉光（通用亮变化）","阴影 / 结构（通用暗变化）","饱和度改动","明度改动"};
        for(unsigned i=0;i<5;i++){ImGui::PushID(static_cast<int>(i));changed |= SettingsSliderFloat(ui(en[i],cn[i]),"##generic-gain",&next.generic_gains[i],i==0?1.f:0.f,2.f,"%.2fx");ImGui::PopID();}
        SettingsNote(ui("Neutral gains preserve D's current NR image. Final extra enhancement and colour follow still apply; set them to 1x for comparison. Values outside the normalized model domain retain D's existing composition.",
            "全部 1 倍保留 D 当前 NR 画面。最终额外增强与颜色跟随仍生效，比较时建议设为 1 倍；超出归一化模型域的像素保留 D 原有合成。"));
        if(ImGui::Button(ui("Reset generic gains", "重置通用倍率"))){std::fill(next.generic_gains,next.generic_gains+5,1.f);changed=true;}
    } else if (light_method == 1u) {
        SettingsNote(ui("Choose regions from the original NR change; 1x keeps the current change and 0x removes it. This keeps the older light values saved for comparison.",
            "按 NR 原始改动固定分区；1 倍保留当前改动，0 倍去除该区改动。原有光影数值仍会保存，方便切换对照。"));
        SettingsNote(ui("Broad and fine describe the spatial extent of NR's luminance change. Same-direction neighbouring changes form broad regions; image edges and available depth/normals limit mixing across surfaces.",
            "宽窄按 NR 明暗改动的空间范围区分：周围也同向变化，归入宽范围；局部孤立变化，归入细节。原图边缘及可用深度、法线限制跨表面混合。"));
        const bool region_colour_disabled = DrawRegionColourStatus(next,has_pending,changed);
        const char* namesEn[]={"Broad brightening","Fine brightening","Broad darkening","Fine darkening","Engine specular region"};
        const char* namesZh[]={"宽范围增亮","细节增亮","宽范围压暗","细节压暗","引擎镜面区域"};
        const unsigned regionStatus=nr_regions::status.load(std::memory_order_acquire);
        for(unsigned i=0;i<4;i++) {
            ImGui::PushID(static_cast<int>(i));ImGui::TextUnformatted(ui(namesEn[i],namesZh[i]));
            changed |= SettingsSliderFloat(ui("Luminance", "明暗"),"##region-luma",&next.region_gains[i*2],0.f,4.f,"%.2fx");
            ImGui::BeginDisabled(region_colour_disabled);
            changed |= SettingsSliderFloat(ui("Colour", "颜色"),"##region-colour",&next.region_gains[i*2+1],0.f,4.f,"%.2fx");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (regionStatus == 3u) {
            const unsigned i=4;
            ImGui::PushID(static_cast<int>(i));ImGui::TextUnformatted(ui(namesEn[i],namesZh[i]));
            changed |= SettingsSliderFloat(ui("Luminance", "明暗"),"##region-luma",&next.region_gains[i*2],0.f,4.f,"%.2fx");
            ImGui::BeginDisabled(region_colour_disabled);
            changed |= SettingsSliderFloat(ui("Colour", "颜色"),"##region-colour",&next.region_gains[i*2+1],0.f,4.f,"%.2fx");
            ImGui::EndDisabled();
            ImGui::PopID();
        } else {
            // Do not expose controls whose guide is absent. In particular,
            // image/depth availability is not evidence of a reflection mask.
            SettingsNote(ui("The game did not provide reflection-region data.",
                "游戏未提供反射区域数据"));
        }
        ImGui::BeginDisabled(region_colour_disabled);
        changed |= SettingsSliderFloat(ui("Colour-only changes", "纯颜色改动"),"##region-pure-colour",&next.region_gains[10],0.f,4.f,"%.2fx");
        ImGui::EndDisabled();
        SettingsNote(ui("Colour gains refine the current colour-follow amount. Specular guidance identifies edits on reflective surfaces; it is not a separately rendered reflection image.",
            "颜色倍率以当前颜色跟随量为基础。镜面导引限定反射表面上的模型改动，并非引擎独立渲染的反射图层。"));
        SettingsNote(regionStatus==4?ui("Region resources unavailable; existing NR composition retained.","分区资源暂不可用；保留现有 NR 合成。"):
            regionStatus==3?ui("Engine depth and specular guidance connected.","引擎镜面导引已接入。"):
            regionStatus==2?ui("Depth guidance connected; specular inputs unavailable.","深度导引已接入；未提供可用镜面导引。"):
            regionStatus==1?ui("Image edges only; engine guidance unavailable.","当前仅使用图像边缘；引擎导引不可用。"):
            ui("Waiting for a supported NR frame.","等待可用 NR 帧。"));
        const char* previewEn[]={"Off","All regions","Broad brightening","Fine brightening","Broad darkening","Fine darkening","Specular region","Colour-only"};
        const char* previewZh[]={"关闭","全部分区","宽范围增亮","细节增亮","宽范围压暗","细节压暗","镜面区域","纯颜色"};
        SettingsField(ui("Preview", "选区预览"));int preview=static_cast<int>(next.region_preview);
        if(ImGui::Combo("##region-preview",&preview,UseChineseUi()?previewZh:previewEn,8)){next.region_preview=static_cast<unsigned>(preview);changed=true;}
        if(ImGui::Button(ui("Reset region gains", "重置分区倍率"))){std::fill(next.region_gains,next.region_gains+11,1.f);changed=true;}
    } else {
        DrawLightStyleControls(next,before_sr,current.generation,light_undo,
            has_pending,pending_light_undo,changed,apply_discrete_edit);
    }
    }
    if (ImGui::CollapsingHeader(ui("Picture safeguards", "画质保护（高级）"))) {

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
        ImGui::SeparatorText(ui("Advanced picture safeguards", "高级画质保护"));
        changed |= ImGui::Checkbox(ui("Highlight mapping", "高光映射"), &next.experimental_peak_shoulder);
        SettingsTooltip(ui("Unknown HDR defaults to luminance only. Storage, exposure and ownership safety remain active.",
            "改变强光送入 AI 前的压缩方式，尽量保留灯光、天空等亮部的层次。此项仍在验证，出现亮度异常时关闭；它不会替你开启游戏 HDR。"));
        changed |= SettingsSliderFloat(ui("Maximum luminance ratio", "明暗变化上限"),
            "##nr-max-luma-ratio", &next.max_luma_ratio, 1.f, 8.f, "%.2f");
        SettingsTooltip(ui("Limits how far the AI result can brighten or darken the original. Higher values allow larger changes; they do not add detail by themselves.", "限制 AI 相对原画面最多能提亮或压暗多少。调高会允许更大的明暗变化，但不会凭空增加细节。"));
    }
        if (ImGui::CollapsingHeader(ui("Developer options", "开发者选项"))) {
        SettingsNote(ui("Experimental controls; existing saved values are retained.", "以下为实验调节，通常保持默认即可；收起菜单不会改变已保存的设置。"));
    changed |= ImGui::Checkbox(ui("Colour protection (default on)", "色彩保护（默认开启）"), &next.color_protection);
    SettingsTooltip(ui("Limits extra colour amplification when Extra enhancement exceeds 1, helping avoid overly strong colours. Recommended on. Turning it off only removes this colour limit; HDR and invalid-value protections remain.",
        "当“额外增强”超过 1 时，限制颜色被过度放大，减少颜色过浓的情况。建议保持开启；关闭仅取消这项限制，其他 HDR 和异常数值保护仍然有效。"));
    if (ImGui::CollapsingHeader(ui("Algorithm comparisons", "算法对照"))) {
    changed |= ImGui::Checkbox(ui("Independent luma / chroma composition (experimental)",
        "分开控制 AI 明暗与颜色（实验）"), &next.independent_chroma_composition);
    SettingsTooltip(ui("Uses the shared candidate before or after SR. Final strength controls luminance while colour follow controls chroma; no extra model call. GPU cost and game quality await validation.",
        "前置和后置均可使用此候选。最终强度控制明暗，颜色跟随控制色彩，不增加模型调用；GPU 开销与游戏画质尚待验证。"));
    changed |= ImGui::Checkbox(ui("Low-grid luminance edit field (experimental)",
        "低分辨率亮度合成（实验）"), &next.low_grid_edit_field);
    SettingsTooltip(ui("Available before or after SR/RR with Colour follow zero; selects shared composition. Computes a luma edit at NR size, then restores it; this changes the interpolation order and may change edges. Source protection remains at full resolution. Adds one optional pass and bounded storage; unavailable resources retain the existing composition. GPU benefit and game quality are unverified.",
        "前后置均可用，需颜色跟随为零；启用会选择共用合成。在 NR 尺寸计算亮度编辑后恢复，会改变插值顺序及可能的边缘观感。原图保护仍在全分辨率。增加一次可选处理和有界存储，资源不可用则保留原合成；GPU 收益和游戏画质未验证。"));
    ImGui::BeginDisabled(next.low_grid_color_edit_field || next.color_strength > 0.000001f);
    changed |= ImGui::Checkbox(ui("Catmull-Rom edit restore (experimental; off = BilinearSafe)",
        "更锐利的细节恢复（实验）"), &next.catmull_edit_restore);
    SettingsTooltip(ui("Requires low-grid luminance editing and zero colour follow. Uses a bounded FP16 horizontal plane; vertical restoration stays in final composition. Invalid footprints or unavailable resources retain valid BilinearSafe. No speed or quality claim.",
        "需低网格亮度编辑且颜色跟随为零。使用有界FP16横向中间面，纵向恢复在最终合成内完成。无效邻域或资源不可用则退有效双线性；不保证速度或画质收益。"));
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox(ui("Low-grid stable colour edit field (experimental)",
        "低分辨率色彩合成（实验）"), &next.low_grid_color_edit_field);
    SettingsTooltip(ui("Available before or after SR/RR. Stores log luminance plus normalized opponent-colour change at NR size, with explicit near-black and finite validity. Interpolation happens before full-resolution colour reconstruction; source edge, HDR headroom, ratio, gamut and finite guards remain final. This is not a face mask and does not add another NR call. Off keeps the existing RG32F luminance field and composition paths.",
        "前后置路线均可用。在 NR 尺寸保存对数亮度及归一化对手色变化，并显式标记近黑和有限性。先插值、再在全分辨率重建色彩；原图边缘、HDR 余量、倍率、色域及有限值保护仍在最终阶段。这不是人脸遮罩，也不增加 NR 调用。关闭时保留现有 RG32F 亮度场与合成路径。"));
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
    }
    if (ImGui::CollapsingHeader(ui("Independent face protection (experimental)",
            "独立人脸保护（实验）")))
        nr_face_runtime::DrawUi(next, changed);
#if NR_HAS_DETAIL_STABILITY
        int curve = static_cast<int>(next.proxy_curve);
        if (ImGui::Combo(ui("HDR mapping", "HDR 映射"), &curve,
                ui("Legacy shoulder\0Hybrid\0Reversible (experimental)\0", "柔和压缩\0混合压缩\0可逆压缩（实验）\0"))) {
            next.proxy_curve = static_cast<unsigned>(curve); changed = true;
        }
#endif
        changed |= ImGui::Checkbox(ui("Allow experimental HDR colour", "允许实验 HDR 调色"), &next.allow_unverified_hdr_color);
        SettingsTooltip(ui("Allows colour edits for an unverified HDR input. Turn off if colour shifts or flicker appear; the AI colour slider never changes this permission.", "当前 HDR 颜色无法确认时，允许尝试 AI 调色。出现偏色或闪烁时关闭；调整 AI 色彩强度不会自动开启此项。"));
        changed |= ImGui::Checkbox(ui("Low-overhead descriptor mode (experimental)",
            "描述符低开销记录（推荐）"), &next.descriptor_low_overhead);
        SettingsTooltip(ui(
            "Only reduces the shader-visible descriptor diagnostic mirror. CPU format restoration and real API calls stay unchanged. It may reduce troubleshooting evidence and does not guarantee a speedup. Off by default; restart the game to take effect.",
            "只减少 shader-visible 描述符诊断镜像；CPU 格式恢复与真实 API 照常。可能降低问题定位证据，不保证提速。默认关闭，重启游戏生效。"));
        const bool descriptor_low_active=NrDescriptorLowOverheadActive();
        ImGui::TextWrapped("%s",UseChineseUi()?
            "默认使用低开销描述符记录。关闭此选项会在下次启动启用完整 GPU 描述符诊断；它不是画质档位，会增加常驻开销。两种方式均保留原生调用和必要 CPU 视图／生命周期记录。":
            "Low-overhead descriptor tracking is the default. Unchecking enables full GPU descriptor diagnostics on the next launch; this is not a quality setting and adds resident overhead. Native calls and required CPU-view/lifetime tracking remain in both modes.");
        SettingsNote(next.descriptor_low_overhead!=descriptor_low_active
            ? ui("Descriptor preference changed; restart the game to apply it.","描述符设置已更改，需重启游戏生效。")
            : descriptor_low_active?ui("Active: low-overhead descriptor mode.","当前生效：描述符低开销模式。")
            : ui("Active: full descriptor mirror.","当前生效：完整描述符镜像。"));
        int present_mv_compensation = static_cast<int>(next.present_mv_compensation);
        if (ImGui::Combo(ui("Motion Vector size compensation",
                "Motion Vector 尺寸补偿"), &present_mv_compensation,
                ui("Conservative automatic\0Force compensation\0",
                   "保守自动\0强制补偿\0"))) {
            next.present_mv_compensation = present_mv_compensation == 1 ? 1u : 0u;
            changed = true;
        }
        SettingsTooltip(ui(
            "Applies to compatibility mode. Conservative automatic preserves the required native-output-to-Present conversion and existing per-layer motion units. Force additionally scales motion by each NR layer's size relative to Present. Compare static views and camera turns: ghosting or flicker may improve or worsen. Both use real motion vectors. A change waits for current work to finish and rebuilds NR state; no game restart is needed.",
            "只影响兼容模式。保守自动保留必要的原生输出→Present 转换及现有每层运动向量单位；强制补偿额外按每层 NR 相对 Present 的尺寸比例缩放。两项都使用真实运动向量。请对照静止画面与转动视角，拖影或闪烁可能改善，也可能加重。切换会等待当前处理完成并重新建立 NR 状态，无需重启游戏。"));
        SettingsNote(ui("Descriptor mode needs a game restart. Motion compensation changes during the current session.",
            "描述符模式需重启游戏生效；Motion Vector 补偿可在本次运行中切换。"));
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
            SettingsTooltip(ui("Experimental game-pixel jitter forwarding, off by default. You may enable it manually. Native SR jitter is unchanged. Switching rebuilds NR history.",
                "实验性游戏像素抖动传递，默认关闭，可手动开启。游戏原生 SR 抖动保持不变，切换后会重建 NR 历史。"));
            const auto jitter_migration = SnapshotConfigPolicyMigrationStatus();
            if (jitter_migration.visible && jitter_migration.jitter_changed &&
                !jitter_migration.failed && next.jitter_mode == NrJitterMode::Legacy)
                ImGui::TextWrapped("%s", ui("This upgrade turned experimental NR jitter forwarding off. You can enable it again here.",
                    "本次升级已关闭实验性 NR 抖动传递，可在此重新开启。"));
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
    if (ImGui::BeginTabItem(ui("Experiments###nr-experiments", "实验###nr-experiments"))) {
        ImGui::BeginDisabled(collecting);
        SettingsNote(ui("Optional experiments. Saved choices are retained; closing this page does not disable them.",
            "可选实验。保留已保存的选择；关闭此页不会停用正在运行的实验。"));
        DrawEnergyGateOptions();
        ImGui::EndDisabled();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ui("Feedback and diagnostics###nr-feedback-diagnostics", "反馈与诊断###nr-feedback-diagnostics"),
            nullptr, feedback_tab_flags)) {
        DrawSimpleFeedbackControls();
        nr_dual_runtime::DrawDetailsUi(current.config.nr_passes == 2, has_pending);
        nr_replay::DrawUi();
        ImGui::BeginDisabled(collecting);
        SettingsSection(ui("Flicker comparison", "闪烁对照"));
        int probe_mode = static_cast<int>(NrProbePolicySnapshot().mode);
        if (ImGui::Combo(ui("Automatic GPU sampling", "自动诊断采样"), &probe_mode,
                ui("Normal sampling\0Pause sampling\0Output state transitions only\0", "正常采样\0暂停采样\0仅切换输出状态\0")))
            SetNrProbePolicy(static_cast<nr_probe_policy::Mode>(probe_mode));
        ImGui::TextWrapped("%s", ui(
            "This session only. NR keeps processing. Switching does not save settings or reset model history. Compare the same scene without taking image captures; already submitted readbacks may still finish. F8 text remains available.",
            "仅本次运行有效，NR 继续处理。切换不保存设置、不重置模型历史。请在同一场景对比，期间不要进行图像采集；此前已提交的读回仍会完成。仍可按 F8 获取文字反馈。"));
        if (probe_mode==static_cast<int>(nr_probe_policy::Mode::OutputTransitionsOnly))
            ImGui::TextWrapped("%s",ui(
                "At diagnostic checkpoints, keep only the output UAV/read-state/UAV barrier sequence. No new automatic sample shader or readback is recorded. Compare flicker and visible enhancement separately; this does not certify a picture fix.",
                "在诊断采样点仅保留输出状态切换，不新增自动采样着色器或读回。请分别记录是否闪烁、增强是否持续可见；该对照不代表画面已修复。"));
#if NR_HAS_TIMING
        SettingsSection(ui("Stutter comparison", "卡顿对照"));
        const auto timing_policy=NrTimingPolicySnapshot();
        int wait_mode=static_cast<int>(timing_policy.wait.mode);
        if(ImGui::Combo(ui("Waiting strategy", "完成等待方式"),&wait_mode,
                ui("Event (default)\0Legacy polling (comparison)\0","事件等待（默认）\0旧轮询（对照）\0")))
            SetNrWaitComparison(static_cast<unsigned>(wait_mode));
        if(ImGui::Button(ui("Start a new timing segment", "开始新计时段")))
            SetNrWaitComparison(static_cast<unsigned>(wait_mode),true);
        ImGui::TextWrapped("%s",ui(
            "This session only; the completion checks and timeout stay the same. Start a new segment, close the menu and observe the same scene for 30 seconds, then press F8. Change one option at a time. CPU timings do not measure total input delay.",
            "仅本次运行有效，完成条件和等待上限保持相同。开始新计时段后关闭菜单，在同一场景观察 30 秒，再按 F8。每次只改一个选项。CPU 耗时不等于完整输入延迟。"));
#endif
        ImGui::EndDisabled();
        SettingsSection(ui("Image evidence", "图像证据"));
        DrawSimpleImageCaptureControls();
        DrawFaceDiagnosticControls(runtime, current.config);
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
        const auto result = ApplySettingsOverlay(pending,pending_generation,
            pending_picture_reset,pending_light_undo,light_undo,pending_model_action,model_undo);
        if (result != OverlayApplyResult::Busy) {
            has_pending = false; pending_picture_reset = false; pending_light_undo = false;
            pending_model_action = nr_model_controls::Action::None;
        }
        if (result == OverlayApplyResult::Conflict) edit_conflict = true;
    }
    if (edit_conflict)
        ImGui::TextWrapped("%s", ui(
            "Settings changed elsewhere. The stale edit was discarded; adjust again using the current values.",
            "设置已被快捷键、配置重载或采集恢复更新；旧编辑未覆盖新值，请在当前值上重新调整。"));
    ImGui::PopStyleVar(2);
}
