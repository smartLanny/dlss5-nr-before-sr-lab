// Public ReShade/ImGui observation APIs only; no copied Core UI or fake clicks.
#include <windows.h>
#include <cstdio>
#include <imgui.h>
#include <reshade.hpp>

static reshade::api::effect_runtime* runtime = nullptr;
static unsigned frames = 0, events = 0;
static constexpr const char* coreWindow = "装机宅DLSS 5";
static void Record(const char* label, unsigned a = 0, unsigned b = 0) {
    FILE* file = std::fopen("sentinel.log","a");
    if (file) { std::fprintf(file,"%s %u %u\n",label,a,b); std::fclose(file); }
}
static void Init(reshade::api::effect_runtime* value) {
    runtime = value; Record("runtime_init");
}
static void Destroy(reshade::api::effect_runtime* value) {
    if (runtime == value) runtime = nullptr;
    Record("runtime_destroy",frames,events);
}
static bool Opened(reshade::api::effect_runtime*, bool open, reshade::api::input_source) {
    Record("overlay_event",++events,open); return false;
}
extern "C" __declspec(dllexport) void MenuTick() {
    if (!runtime) { Record("tick_without_runtime"); return; }
    ++frames;
    if (frames == 1)
        Record("actual_core_module",GetModuleHandleW(L"core-under-test.addon64") != nullptr,0);
    if (frames == 3 || frames == 10 || frames == 15 || frames == 22 || frames == 27 || frames == 34) {
        bool open = frames == 3 || frames == 15 || frames == 27;
        Record("open_request",frames,open);
        bool accepted = runtime->open_overlay(open,reshade::api::input_source::keyboard);
        Record("open_return",frames,accepted);
    }
    if (frames % 10 == 0) Record("runtime_present",frames,events);
}
static void Present(reshade::api::effect_runtime* value) {
    if (frames == 8 || frames == 20 || frames == 32) {
        const char* postfix = frames == 8 ? "core-menu-first" :
                              frames == 20 ? "core-menu-second" : "core-menu-third";
        value->save_screenshot(postfix); // API returns void; the script checks actual PNG files.
        Record("screenshot_request",frames,1);
    }
}
static void Overlay(reshade::api::effect_runtime*) {
    // Preserve ReShade's docking configuration. Its initial layout builder
    // requires docking; observation must not invalidate that host precondition.
    if (frames == 4 || frames == 16 || frames == 28) {
        ImGui::SetWindowCollapsed(coreWindow,false,ImGuiCond_Always);
        ImGui::SetWindowPos(coreWindow,ImVec2(24,90),ImGuiCond_Always);
        ImGui::SetWindowSize(coreWindow,ImVec2(1540,1450),ImGuiCond_Always);
        ImGui::SetWindowFocus(coreWindow);
        Record("focus_requested",frames,1);
    }
    Record("gui_frame",frames,0);
}
extern "C" { __declspec(dllexport) const char* NAME = "Light style actual-menu observer"; }
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!reshade::register_addon(module)) return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(Opened);
        reshade::register_event<reshade::addon_event::reshade_present>(Present);
        reshade::register_event<reshade::addon_event::reshade_overlay>(Overlay);
        Record("addon_loaded");
    } else if (reason == DLL_PROCESS_DETACH) {
        reshade::unregister_addon(module);
    }
    return TRUE;
}
