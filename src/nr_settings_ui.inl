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

void SettingsHelp(const char *text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    SettingsTooltip(text);
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
    nr_acceptance::RuntimeDestroyed(runtime);
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
            (zh?"其他位置不可用时，尝试在接近最终画面处增强，可能包含字幕。FG 开启时可能改在生成帧之前处理；缺少可信输入会跳过。无效或闪烁请按 F8。":"Tries enhancement near the final image when other routes fail. It may include the HUD. FG may move it before frame generation; missing trusted inputs skip NR. Save F8 if unavailable or flickering."));
        ImGui::PopID();
    }
    ImGui::PopID();
    nr_processing::Snapshot route_snapshot{};
    const auto actual=nr_processing_runtime::TrySnapshot(route_snapshot)?route_snapshot.current:nr_processing_runtime::Effective();
    const char* names[]={zh?"先增强后放大":"Enhance first",zh?"先放大后增强":"Upscale first",zh?"自动兼容":"Compatibility"};
    ImGui::TextDisabled(zh?"当前位置：%s":"Current position: %s",names[static_cast<unsigned>(actual)]);
    if(nr_processing_runtime::FgRedirectRequested())
        ImGui::TextWrapped("%s",zh?"FG 开启：在生成帧前尝试增强。":"FG on: trying enhancement before generated frames.");
    else if(actual!=next.processing_start)
        ImGui::TextWrapped("%s",zh?"已切兼容位置；可点选重试，异常按 F8。":"Compatibility position active; retry or press F8 if unavailable.");
    if(actual==nr_processing::Start::Present){
        nr_compat_present::Observation status{};
        if(nr_compat_present_runtime::TrySnapshot(status)){
            ImGui::TextDisabled(zh?"整链工作尺寸 %u × %u，%u 层":"Chain work size %u x %u, %u passes",status.work_width,status.work_height,status.passes);
            if(status.color_only)ImGui::TextWrapped("%s",zh?"占位深度/运动每帧重置；按 F8。":"Placeholder depth/motion resets each frame; press F8.");
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
        ui("Pre-SR lighting match###Experimental Lighting Match (NR -> SR)",
           "前置光照匹配###实验性光照匹配（NR -> SR）"),
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
        "Choose one light adjustment method. Normal light/dark keeps the two legacy controls; Engine regions uses current engine guidance; Generic layers offers legacy HSL or an explicit continuous SDR experiment. Saved values in the other methods remain available when you switch back.",
        "选择一种光影调节方式。普通亮暗保留原有两套控制；引擎分区使用当前引擎导引；通用分层可选择旧版 HSL 或连续 SDR 实验。切换回来时，其他方式的已保存数值仍保留。"));
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
    if (nr_present_api_status::IsD3d11PresentRuntime(runtime)) {
        DrawDx11DeploymentStatus();
    }
    else if (!config.enabled || ActiveTransferStrength(config) <= 0.000001f) {
        displayed=Display::Native;
        ImGui::TextWrapped("%s", ui("NR is off; game output continues.",
            "NR 已关闭，显示游戏原画面。"));
    }
    else if (reason == BypassReason::GuideContractInvalid) {
        displayed=Display::Checking;
        ImGui::TextWrapped("%s", ui("NR skipped an invalid guide grid or converted motion scale. Native output is retained. Press F8 and submit the report; changing colour strength cannot repair this input contract.",
            "NR 已跳过无效的引导尺寸或运动向量换算值，继续保留原生输出。请按 F8 收集并提交反馈；调节色彩强度不能修复此输入问题。"));
    }
    else if (reason == BypassReason::ContextReleasePending) {
        displayed=Display::Checking;
        ImGui::TextWrapped("%s", ui("Waiting for the previous model's GPU work and safe release before applying new settings. Native output continues; your selected scale and route are retained. If this persists, press F8 and submit the report.",
            "正在等待旧模型的 GPU 工作完成并安全释放，随后应用新设置。期间继续显示游戏原图，所选比例和顺序不变。若持续停在此状态，请按 F8 提交反馈。"));
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

#include "nr_unified_settings.inl"

