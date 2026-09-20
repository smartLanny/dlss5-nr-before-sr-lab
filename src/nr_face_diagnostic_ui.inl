#include "nr_face_diagnostic.h"
#include "nr_face_capture_geometry.h"

namespace nr_face_diagnostic_ui {
struct Selection {
    float center_x = .5f, center_y = .5f;
    float saved_x = .5f, saved_y = .5f, grab_x = 0.f, grab_y = 0.f;
    bool picking = false, show_frame = true, request_failed = false;
};
struct View {
    nr_face_capture_geometry::View geometry{};
    bool enabled = false, blocked = false, progress_visible = false;
    bool active = false, packaging = false, complete = false, partial = false, failed = false;
    bool roi_ready = false, screenshot_ready = false, restored = false;
    unsigned group = 0, total = 9;
};
struct Requests { bool start = false, cancel = false, choose_region = false; };
struct ScreenRect {
    bool valid = false;
    float x = 0.f, y = 0.f, width = 0.f, height = 0.f;
};

// UI coordinates come from the actual foreground viewport; geometry is an
// independently validated recent source/display mapping, never model-work size.
ScreenRect Viewport(const nr_face_capture_geometry::View &g, ImVec2 low, ImVec2 high) {
    if (!g.valid || !g.source_width || !g.source_height || !g.display_width || !g.display_height ||
        !g.viewport_width || !g.viewport_height || high.x <= low.x || high.y <= low.y)
        return {};
    const float sx = (high.x - low.x) / g.display_width;
    const float sy = (high.y - low.y) / g.display_height;
    return {true, low.x + g.viewport_x * sx, low.y + g.viewport_y * sy,
        g.viewport_width * sx, g.viewport_height * sy};
}
ScreenRect Region(const nr_face_capture_geometry::View &g, const ScreenRect &viewport,
    float center_x, float center_y) {
    const auto roi = nr_face_diagnostic::ResolveRoi(g.source_width, g.source_height, center_x, center_y);
    if (!viewport.valid || !roi.valid) return {};
    return {true, viewport.x + viewport.width * roi.x / g.source_width,
        viewport.y + viewport.height * roi.y / g.source_height,
        viewport.width * roi.width / g.source_width, viewport.height * roi.height / g.source_height};
}
void MoveTo(Selection &s, const ScreenRect &viewport, ImVec2 point) {
    if (!viewport.valid || !std::isfinite(point.x) || !std::isfinite(point.y)) return;
    s.center_x = std::clamp((point.x - viewport.x) / viewport.width, 0.f, 1.f);
    s.center_y = std::clamp((point.y - viewport.y) / viewport.height, 0.f, 1.f);
}
void Outline(ImDrawList *draw, const ScreenRect &rect, bool picking) {
    if (!rect.valid) return;
    const ImVec2 low(rect.x, rect.y), high(rect.x + rect.width, rect.y + rect.height);
    draw->AddRect(low, high, IM_COL32(0, 0, 0, 220), 0.f, 0, 5.f);
    draw->AddRect(low, high, picking ? IM_COL32(255, 210, 66, 255) : IM_COL32(80, 220, 240, 255),
        0.f, 0, 2.f);
}

void PickRegion(Selection &s, const View &view, ImDrawList *draw,
    ImVec2 low, ImVec2 high, bool zh) {
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    const auto viewport = Viewport(view.geometry, low, high);
    ImGui::SetNextWindowPos(low, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(high.x - low.x, high.y - low.y), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("###nr-face-region-picker", nullptr, flags)) {
        const float toolbar_height = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() + 40.f;
        ImGui::GetWindowDrawList()->AddRectFilled(low,
            ImVec2(high.x, std::min(high.y, low.y + toolbar_height)), IM_COL32(16, 18, 24, 235));
        ImGui::TextUnformatted(ui("Click the face centre or drag the frame. The camera stays under your control after selection.",
            "点击人脸中心，或拖动取景框。选好后请保持镜头不动。"));
        if (ImGui::Button(ui("Use this region###nr-face-region-confirm", "确认选区###nr-face-region-confirm")))
            s.picking = false;
        ImGui::SameLine();
        if (ImGui::Button(ui("Cancel selection###nr-face-region-cancel", "取消框选###nr-face-region-cancel"))) {
            s.center_x = s.saved_x; s.center_y = s.saved_y; s.picking = false;
        }
        if (s.picking) {
            const ImVec2 canvas_low(low.x, std::min(high.y, low.y + toolbar_height));
            ImGui::SetCursorScreenPos(canvas_low);
            if (high.y > canvas_low.y) {
                ImGui::InvisibleButton("##nr-face-region-canvas",
                    ImVec2(high.x - low.x, high.y - canvas_low.y), ImGuiButtonFlags_MouseButtonLeft);
                if (ImGui::IsItemActive()) {
                    const ImVec2 mouse = nr_imgui_abi::CallHostVec2(imgui_function_table_instance()->GetMousePos);
                    const auto rect = Region(view.geometry, viewport, s.center_x, s.center_y);
                    if (ImGui::IsItemActivated()) {
                        const bool inside = rect.valid && mouse.x >= rect.x && mouse.x <= rect.x + rect.width &&
                            mouse.y >= rect.y && mouse.y <= rect.y + rect.height;
                        s.grab_x = inside ? mouse.x - (rect.x + rect.width * .5f) : 0.f;
                        s.grab_y = inside ? mouse.y - (rect.y + rect.height * .5f) : 0.f;
                    }
                    MoveTo(s, viewport, ImVec2(mouse.x - s.grab_x, mouse.y - s.grab_y));
                }
            }
        }
        Outline(draw, Region(view.geometry, viewport, s.center_x, s.center_y), true);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// The renderer receives a snapshot and returns user actions. A local UI host can
// draw this exact code with constructed observations without running NR.
Requests Draw(Selection &s, const View &view, bool zh) {
    Requests requests;
    const auto ui = [zh](const char *en, const char *cn) { return zh ? cn : en; };
    ImGui::SeparatorText(ui("Face parameter comparison", "人脸参数对照"));
    const bool expanded = true;
    ImDrawList *draw = ImGui::GetForegroundDrawList(nullptr);
    const ImVec2 low = draw->GetClipRectMin(), high = draw->GetClipRectMax();
    const auto viewport = Viewport(view.geometry, low, high);
    if (expanded) {
        ImGui::TextWrapped("%s", ui("Select the face region, then keep the camera and scene still. Nine comparisons run automatically; each saves a full-screen image and the selected region before/after model processing. Your original settings are restored when the sequence ends or is cancelled.",
            "先选好人脸区域，再保持镜头和场景不动。自动完成九组对照，每组保存全屏画面与选区的模型处理前后结果；结束或取消后自动恢复原设置。"));
        ImGui::TextWrapped("%s", ui("Sequence: current controls with Face adjustment off/on; model baseline off/on; baseline with Local contrast 2 off/on; baseline with Overall detail 2 off/on; current controls on again.",
            "顺序：当前参数关闭/开启人脸调节；模型基准关闭/开启；基准的局部明暗设为 2 后关闭/开启；基准的整体细节设为 2 后关闭/开启；最后复测当前参数开启。"));
        if (view.blocked) ImGui::BeginDisabled();
        ImGui::Checkbox(ui("Show selected region###nr-face-show-region", "显示取景框###nr-face-show-region"), &s.show_frame);
        if (!view.enabled || !viewport.valid) ImGui::BeginDisabled();
        if (ImGui::Button(ui("Choose face region###nr-face-region-select", "选择人脸区域###nr-face-region-select"))) {
            s.saved_x = s.center_x; s.saved_y = s.center_y;
            s.request_failed = false;
            requests.choose_region = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ui("Start automatic comparison###nr-face-start", "开始自动对照###nr-face-start")))
            requests.start = true;
        if (!view.enabled || !viewport.valid) ImGui::EndDisabled();
        if (view.blocked) ImGui::EndDisabled();
        SettingsTooltip(ui("The panel closes when capture starts. Keep the camera still until it finishes; reopen Feedback and diagnostics to cancel. This is a parameter comparison, not a face-mask export.",
            "开始后自动收起面板。采集完成前请保持镜头不动；需要取消时可重新打开反馈与诊断。此项用于参数对照，不代表导出人脸遮罩。"));
        if (!view.enabled)
            SettingsNote(ui("Enable NR before starting this comparison.", "请先启用 NR，再开始人脸对照。"));
        else if (!viewport.valid)
            SettingsNote(ui("The current AI picture cannot yet be aligned with this view. Let NR run in the scene; selection stays unavailable while its position is unverified.",
                "当前尚未确认 AI 画面与显示位置对齐。请先让 NR 在场景中运行；位置未确认时暂不可选择区域。"));
        if (s.request_failed)
            ImGui::TextWrapped("%s", ui("The comparison could not start. Wait for other feedback work to finish, then try again.",
                "本次未能开始对照。请等待其他反馈任务结束后重试。"));
    }
    if (view.progress_visible && (expanded || view.active || view.packaging)) {
        if (view.active) {
            static constexpr const char *en_groups[] = {
                "Current controls: face off", "Current controls: face on", "Model baseline: face off", "Model baseline: face on",
                "Baseline contrast 2: face off", "Baseline contrast 2: face on", "Baseline detail 2: face off", "Baseline detail 2: face on",
                "Current controls: face on again"};
            static constexpr const char *zh_groups[] = {
                "当前参数：人脸关闭", "当前参数：人脸开启", "模型基准：人脸关闭", "模型基准：人脸开启",
                "基准明暗 2：人脸关闭", "基准明暗 2：人脸开启", "基准细节 2：人脸关闭", "基准细节 2：人脸开启",
                "当前参数：人脸开启复测"};
            const unsigned index = std::min(view.group, 8u);
            ImGui::TextWrapped(ui("Comparison %u/%u: %s", "对照 %u/%u：%s"),
                std::min(view.group + 1u, view.total), view.total, zh ? zh_groups[index] : en_groups[index]);
            ImGui::TextDisabled("%s", view.roi_ready && view.screenshot_ready
                ? ui("Saving this comparison...", "正在保存本组……")
                : ui("Waiting for stable AI output and image capture...", "正在等待 AI 输出稳定并采集画面……"));
            if (ImGui::Button(ui("Cancel and restore settings###nr-face-cancel", "取消并恢复设置###nr-face-cancel")))
                requests.cancel = true;
        } else if (view.packaging)
            ImGui::TextDisabled("%s", ui("Packaging face comparisons...", "正在整理人脸对照……"));
        else if (view.complete)
            ImGui::TextWrapped("%s", ui("Face comparisons saved.", "人脸对照已保存。"));
        else if (view.partial || view.failed)
            ImGui::TextWrapped("%s", ui("Some comparisons did not complete; missing evidence is recorded in the report.",
                "部分对照未完成，报告已记录缺失证据。"));
        if (view.restored)
            ImGui::TextDisabled("%s", ui("Original settings restored.", "已恢复原设置。"));
    }
    if (!view.blocked && viewport.valid && expanded && s.show_frame)
        Outline(draw, Region(view.geometry, viewport, s.center_x, s.center_y), false);
    return requests;
}
} // namespace nr_face_diagnostic_ui
