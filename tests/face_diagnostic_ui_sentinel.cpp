// Real ReShade 19250 drawing of the production face UI with constructed
// observations. No Core/NVIDIA DLL, image capture controller or fake clicks.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <reshade.hpp>
#include "nr_imgui_abi.h"

void SettingsTooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.f);
        ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
}
void SettingsNote(const char *text) { ImGui::TextWrapped("%s", text); }
#include "nr_face_diagnostic_ui.inl"

static reshade::api::effect_runtime *runtime = nullptr;
static unsigned frames = 0, events = 0;
static bool overlay_open = false, chinese = false;
static nr_face_diagnostic_ui::Selection selection;
static void Record(const char *label, unsigned a = 0, unsigned b = 0) {
    FILE *file = std::fopen("sentinel.log", "a");
    if (file) { std::fprintf(file, "%s %u %u\n", label, a, b); std::fclose(file); }
}
bool UseChineseUi() { return chinese; }
#include "nr_face_region_selection.inl"
nr_face_diagnostic_ui::View FaceSelectionView(reshade::api::effect_runtime *) {
    nr_face_diagnostic_ui::View view;
    view.enabled = true;
    view.geometry.valid = true;
    view.geometry.source_width = view.geometry.source_height = 2560;
    view.geometry.display_width = view.geometry.display_height = 1600;
    view.geometry.viewport_width = view.geometry.viewport_height = 1600;
    view.geometry.sampled_at = GetTickCount64();
    Record("selection_callback", frames, overlay_open);
    if (frames == 20) Record("production_ui_drawn", frames, 1);
    return view;
}
void FocusFaceDiagnosticPage() { Record("diagnostic_page_requested", frames, 1); }
static void Init(reshade::api::effect_runtime *value) {
    runtime = value;
    chinese = GetPrivateProfileIntW(L"Fixture", L"Chinese", 0, L".\\face-fixture.ini") != 0;
    selection.center_x = .66f; selection.center_y = .62f;
    Record("runtime_init", chinese, 0);
}
static void Destroy(reshade::api::effect_runtime *value) {
    const bool was_registered = SnapshotFaceRegionSelection().registered;
    CleanupFaceDiagnosticSelection(value);
    Record("destroy_selection_cleanup", was_registered, SnapshotFaceRegionSelection().registered);
    if (runtime == value) runtime = nullptr;
    Record("runtime_destroy", frames, events);
}
static bool Opened(reshade::api::effect_runtime *, bool value, reshade::api::input_source) {
    overlay_open = value; Record("overlay_event", ++events, value); return false;
}
static void FocusPanel(reshade::api::effect_runtime *) {
    const char *title = "Production face UI - constructed observations";
    ImGui::SetWindowCollapsed(title, false, ImGuiCond_Always);
    ImGui::SetWindowPos(title, ImVec2(24.f, 90.f), ImGuiCond_Always);
    ImGui::SetWindowSize(title, ImVec2(820.f, 620.f), ImGuiCond_Always);
    ImGui::SetWindowFocus(title);
}
extern "C" __declspec(dllexport) void MenuTick() {
    if (!runtime) { Record("tick_without_runtime"); return; }
    ++frames;
    if (frames == 3 || frames == 27) reshade::register_event<reshade::addon_event::reshade_overlay>(FocusPanel);
    if (frames == 5 || frames == 29) reshade::unregister_event<reshade::addon_event::reshade_overlay>(FocusPanel);
    if (frames == 1) {
        Record("constructed_observations", 1, 1);
        Record("model_module_absent", GetModuleHandleW(L"nvngx_dlssnr.dll") == nullptr, 0);
        Record("core_module_absent", GetModuleHandleW(L"core-under-test.addon64") == nullptr, 0);
    }
    if (frames == 3 || frames == 10 || frames == 15 || frames == 22 || frames == 27 || frames == 34) {
        const bool requested = frames == 3 || frames == 15 || frames == 27;
        Record("open_request", frames, requested);
        Record("open_return", frames, runtime->open_overlay(requested, reshade::api::input_source::keyboard));
    }
    if (frames == 15 || frames == 37) {
        Record("programmatic_selection_request", frames, QueueFaceRegionSelection(runtime, selection));
    }
    if (frames == 21) {
        const auto snapshot = SnapshotFaceRegionSelection();
        FinishFaceRegionSelection(runtime, snapshot.generation, snapshot.selection, true);
        Record("programmatic_selection_finish", frames, 1);
    }
    ServiceFaceDiagnosticSelection(runtime);
    if (frames == 8 || frames == 20 || frames == 23 || frames == 40)
        Record("selection_registration", frames, SnapshotFaceRegionSelection().registered);
}
static void Present(reshade::api::effect_runtime *value) {
    if (frames == 8 || frames == 20 || frames == 32) {
        const char *postfix = frames == 8 ? "face-ui-panel" : frames == 20 ? "face-ui-picker" : "face-ui-progress";
        value->save_screenshot(postfix); Record("screenshot_request", frames, 1);
    }
}
static void Panel(reshade::api::effect_runtime *) {
    nr_face_diagnostic_ui::View view;
    view.enabled = true;
    view.geometry.valid = true;
    view.geometry.source_width = view.geometry.source_height = 2560;
    view.geometry.display_width = view.geometry.display_height = 1600;
    view.geometry.viewport_width = view.geometry.viewport_height = 1600;
    view.geometry.sampled_at = GetTickCount64();
    if (frames >= 27) {
        view.blocked = view.progress_visible = view.active = true;
        view.group = 5; view.roi_ready = true;
    }
    ImGui::SetWindowPos(ImVec2(24.f, 90.f), ImGuiCond_Always);
    ImGui::SetWindowSize(ImVec2(820.f, 620.f), ImGuiCond_Always);
        ImGui::TextWrapped("%s", chinese ? "本地主机界面验证：使用构造观察值，未运行 NR。" :
            "Local UI validation: constructed observations; NR is not running.");
        const auto requests = nr_face_diagnostic_ui::Draw(selection, view, chinese);
        if (requests.start || requests.cancel || requests.choose_region)
            Record("unexpected_capture_action", requests.start, requests.cancel);
        if (frames == 8 || frames == 20 || frames == 32) {
            const bool glyphs = ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>(0x4EBA)) &&
                ImGui::GetFont()->IsGlyphInFont(static_cast<ImWchar>(0x8138));
            Record("production_ui_drawn", frames, selection.picking);
            Record("chinese_glyphs_available", frames, glyphs);
        }
}
extern "C" { __declspec(dllexport) const char *NAME = "Face diagnostic production UI fixture"; }
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!reshade::register_addon(module)) return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(Opened);
        reshade::register_event<reshade::addon_event::reshade_present>(Present);
        reshade::register_overlay("Production face UI - constructed observations", Panel);
        Record("addon_loaded");
    } else if (reason == DLL_PROCESS_DETACH) {
        CleanupFaceDiagnosticSelection(nullptr);
        reshade::unregister_addon(module);
    }
    return TRUE;
}
