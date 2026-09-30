"""Host regressions for issues #39/#44/#45; executes extracted production code.

No NVIDIA runtime, game assets or Windows GPU is needed. This is not a claim of
D3D12/ReShade/Feature 18 or in-game validation. --source permits a red/green
comparison against an unmodified checkout.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


def definition(text: str, marker: str, semicolon: bool = False) -> str:
    start = text.index(marker)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start : end + int(semicolon)]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
parser.add_argument("--source", type=Path, help="Path to nr_before_sr.cpp")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source_path = args.source or root / "src/nr_before_sr.cpp"
source = source_path.read_text(encoding="utf-8-sig")
ui_source = source_path.with_name("nr_settings_ui.inl").read_text(encoding="utf-8-sig")

PREAMBLE = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <iterator>
#include <random>
#include <string>
#include "nr_light_style.h"
#include "nr_generic_residual.h"
#include "nr_dual_pass.hpp"
#include "nr_layer_config.h"
#include "nr_processing_route.h"
#if __has_include("nr_colour_preferences.h")
#include "nr_colour_preferences.h"
#include "nr_colour_restore_policy.h"
#include "nr_colour_edge_policy.h"
#include "nr_spatial_experiment_policy.h"
#include "nr_hotkeys.h"
#endif
int checks = 0, failures = 0;
void check(bool ok, const char *label) {
    ++checks;
    if (!ok) { if (failures < 12) std::printf("FAIL: %s\n", label); ++failures; }
}
int finish(const char *name) {
    std::printf("%s: %d checks / %d failures\n", name, checks, failures);
    return failures ? 1 : 0;
}
enum class NrJitterMode : int { Legacy = 0, GameInputPixel = 1 };
'''


def config_code() -> str:
    return "\n".join([
        definition(source, "struct ChainConfig {", True),
        "ChainConfig g_config;",
        definition(source, "float FiniteOr("),
        definition(source, "void NormalizeConfig("),
        definition(source, "struct NrTuningSignature", True),
        definition(source, "NrTuningSignature CurrentNrTuningSignature("),
        definition(source, "bool SameNrTuningSignature("),
    ])


class BugSweepTests(unittest.TestCase):
    def run_cpp(self, name: str, code: str) -> None:
        compiler = shutil.which(args.compiler)
        self.assertIsNotNone(compiler, f"C++ compiler not found: {args.compiler}")
        with tempfile.TemporaryDirectory(prefix="nr-bug-sweep-") as td:
            cpp = Path(td) / (name + ".cpp")
            exe = Path(td) / (name + (".exe" if os.name == "nt" else ""))
            cpp.write_text(PREAMBLE + code, encoding="utf-8")
            flags = ["-std=c++20", "-O0"]
            if os.name == "nt":
                flags.append("-static")  # Do not depend on toolchain DLLs in the temp directory.
            built = subprocess.run([compiler, *flags, "-I", str(source_path.parent), str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            print(ran.stdout, end="", flush=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_skin_requested_and_effective_state(self) -> None:
        self.run_cpp("skin-policy", config_code() + r'''
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (bool mask : {false, true}) {
        for (float raw : {-10.f, -1.f, -.3f, 0.f, .4f, 1.7f, 2.f, 5.f, nan, inf, -inf}) {
            g_config = {}; g_config.auto_mask = mask; g_config.skin_structure = raw;
            NormalizeConfig(g_config);
            const auto tuning = CurrentNrTuningSignature();
            check(std::isfinite(g_config.skin_structure), "stored preference finite");
            check(mask ? (g_config.skin_structure >= 0 && g_config.skin_structure <= 2)
                       : (g_config.skin_structure >= -1 && g_config.skin_structure <= 2),
                  "legacy negative migrates to zero only with face mask enabled");
            check(mask ? tuning.skin_structure == g_config.skin_structure
                       : tuning.skin_structure == -1.f, "mask off sends sentinel, not saved skin value");
            if (std::isfinite(raw) && raw >= 0 && raw <= 2)
                check(g_config.skin_structure == raw, "explicit nonnegative skin preference retained");
        }
    }
    g_config = {}; g_config.auto_mask = true; g_config.skin_structure = 1.7f;
    NormalizeConfig(g_config); const auto on = CurrentNrTuningSignature();
    g_config.auto_mask = false; NormalizeConfig(g_config);
    const auto off = CurrentNrTuningSignature();
    check(!SameNrTuningSignature(on, off), "mask toggle changes feature-create signature");
    check(g_config.skin_structure == 1.7f, "off does not destroy stored skin preference");
    g_config.skin_structure = .4f; NormalizeConfig(g_config);
    check(SameNrTuningSignature(off, CurrentNrTuningSignature()), "inactive skin preference does not rebuild model");
    g_config.auto_mask = true; NormalizeConfig(g_config);
    check(CurrentNrTuningSignature().skin_structure == .4f, "reenable restores saved nonnegative preference");
    for (bool ui : {false, true}) {
        g_config.ui_correction = ui; NormalizeConfig(g_config);
        check(CurrentNrTuningSignature().ui_correction == ui, "explicit UI correction preserved");
    }
    return finish("actual normalize/create-signature skin policy");
}
''')

    def test_face_controls_round_trip(self) -> None:
        layer_ui=source_path.with_name("nr_layers_ui.inl").read_text(encoding="utf-8-sig")
        # Locate the production control by its stable ID and field, not translated copy.
        control_at=layer_ui.index('ImGui::Checkbox("##native-mask",&model.auto_mask)')
        start=layer_ui.rindex('        NrLayerField(',0,control_at)
        end=layer_ui.index('        ImGui::EndDisabled();',start)+len('        ImGui::EndDisabled();')
        controls=layer_ui[start:end]
        self.run_cpp("face-ui", config_code() + r'''
int toggle = -1, sliders = 0;bool disabled=false;
float slider_value = -100.f;
void SettingsTooltip(const char *) {}
void NrLayerField(const char *,bool) {} // Layout only; controls below remain production code.
void SettingsNote(const char *) {}
namespace ImGui {
void BeginDisabled(bool value){disabled=value;}
void EndDisabled(){disabled=false;}
bool Checkbox(const char *label, bool *value) {
    const std::string text(label);
    if (text == "##native-mask") {
        if (toggle >= 0) { bool old = *value; *value = toggle != 0; return old != *value; }
    }
    return false;
}
bool SliderFloat(const char *, float *value, float lo, float hi, const char *) {
    if(disabled)return false;
    ++sliders;
    check(lo == 0.f && hi == 2.f, "visible skin range starts at zero");
    check(*value >= lo && *value <= hi, "visible skin never displays negative sentinel");
    if (slider_value >= 0.f) { *value = slider_value; return true; }
    return false;
}
void TextDisabled(const char *, ...) {}
}
bool SettingsSliderFloat(const char *label, const char *, float *value, float lo, float hi, const char *format) {
    return ImGui::SliderFloat(label, value, lo, hi, format);
}
bool DrawFaceControls(ChainConfig &next, bool chinese) {
    bool edited=false,discrete=false;const bool zh=chinese;const bool table=false;auto model=nr_layers::Read(next,0);
CONTROLS
    (void)discrete;
    if(edited)nr_layers::WritePrimary(next,model);
    return edited;
}
int main() {
    for (bool chinese : {false, true}) {
        ChainConfig c; c.skin_structure = -1.f; c.auto_mask = false;
        toggle = -1; slider_value = -100.f; sliders = 0;
        check(!DrawFaceControls(c, chinese), "disabled face controls do not edit settings");
        check(sliders == 0, "mask off disables skin editing");
        toggle = 1; sliders = 0;
        check(DrawFaceControls(c, chinese), "enabling face mask commits edit");
        check(c.auto_mask && c.skin_structure == .4f && sliders == 1, "first enable initializes native skin strength 0.4");
        toggle = -1; slider_value = 1.7f;
        check(DrawFaceControls(c, chinese), "skin slider commits chosen preference");
        toggle = 0; slider_value = -100.f; sliders = 0;
        check(DrawFaceControls(c, chinese), "disabling face mask commits edit");
        check(!c.auto_mask && c.skin_structure == 1.7f && sliders == 0, "disable keeps preference without sending skin edit");
        toggle = 1; sliders = 0;
        DrawFaceControls(c, chinese);
        check(c.auto_mask && c.skin_structure == 1.7f && sliders == 1, "reenable restores previous skin preference");
    }
    return finish("actual EN/ZH face-control block");
}
'''.replace("CONTROLS", controls))

    def test_lighting_protection_is_only_attenuation(self) -> None:
        block = definition(source, "if (LightingLock > 0.000001")
        self.run_cpp("lighting-lock", r'''
float clamp(double x, double a, double b) { return static_cast<float>(std::clamp(x, a, b)); }
float max(double a, double b) { return static_cast<float>(std::max(a, b)); }
float min(double a, double b) { return static_cast<float>(std::min(a, b)); }
float saturate(float x) { return clamp(x, 0, 1); }
float lerp(float a, float b, float t) { return a + (b-a)*t; }
struct float2 {
    float x, y; float2(float a, float b) : x(a), y(b) {}
    float2& operator+=(float2 b) { x+=b.x; y+=b.y; return *this; }
};
float2 operator+(float2 a, float2 b) { return {a.x+b.x, a.y+b.y}; }
float2 operator-(float2 a, float2 b) { return {a.x-b.x, a.y-b.y}; }
float2 operator*(float a, float2 b) { return {a*b.x, a*b.y}; }
float taps[4], weights[4] = {1,1,1,1};
bool layeredActive = false;
float LayerTone = 1.f, LayerStructure = 1.f, layerMean = 0.f;
float ModelLumaRatioV2(float2 p) { return taps[p.x > 0 ? 0 : p.x < 0 ? 1 : p.y > 0 ? 2 : 3]; }
// The actual luma-guidance helper is covered by test_lighting_edge_weights.py.
// This older oracle isolates the final attenuation bound for arbitrary weights.
float2 ModelLightingTapV2(float2 p, float) {
    const unsigned i=p.x > 0 ? 0 : p.x < 0 ? 1 : p.y > 0 ? 2 : 3;
    return {taps[i]*weights[i],weights[i]};
}
float apply(float logRatio, float rawLog, float LightingLock) {
    const float proxyLuma = 1.f, neuralLuma = std::exp2(rawLog);
    const float2 center(0, 0);
BLOCK
    return logRatio;
}
int main() {
    for (float &t : taps) t = 2.f;
    check(apply(.05f, .05f, 1.f) >= 0.f, "bright edge cannot become a dark outline");
    for (float &t : taps) t = .5f;
    check(apply(-.05f, -.05f, 1.f) <= 0.f, "dark edge cannot become a bright outline");
    for (float ratio : {.125f, .5f, 1.f, 2.f, 8.f}) {
        for (float &t : taps) t = ratio;
        float log = std::log2(ratio);
        check(std::abs(apply(log, log, 1.f)) < 1e-6f, "uniform luminance drift still removed");
        check(apply(log, log, 0.f) == log, "zero lock exactly preserves existing residual");
    }
    std::mt19937 rng(39044);
    std::uniform_real_distribution<float> log(-8.f, 8.f), unit(0.f, 1.f);
    for (int i = 0; i < 10000; ++i) {
        const float raw = log(rng), residual = raw * unit(rng);
        for (float &t : taps) t = std::exp2(log(rng));
        for (float &w : weights) w = unit(rng);
        layeredActive = (i & 1) != 0;
        LayerTone = (i % 3) == 0 ? .5f : 1.f;
        LayerStructure = (i % 3) == 0 ? 1.5f : 1.f;
        const float lock = unit(rng);
        const float result = apply(residual, raw, lock);
        if (layeredActive)
            check(apply(residual, raw, 0.f) == residual, "layer-only neighborhood never applies lighting attenuation");
        check(std::isfinite(result), "finite output");
        check(result >= std::min(residual, 0.f)-1e-6f && result <= std::max(residual, 0.f)+1e-6f,
              "lock never reverses or amplifies even an already stabilized residual");
        check(apply(0.f, raw, 1.f) == 0.f, "zero residual cannot acquire a neighbor's opposite edit");
    }
    return finish("actual HLSL lighting block compiled with scalar host intrinsics");
}
'''.replace("BLOCK", block))

    def test_native_hook_enable_failure_retries_its_own_trampoline(self) -> None:
        functions = "\n".join(definition(source, marker) for marker in (
            "bool EnsureNativeQueueExecuteHookForNative(ID3D12CommandQueue *native_queue) {",
            "bool EnsureNativeQueueExecuteHook(",
            "bool EnsureNativeCommandListResetHookForNative(",
            "bool EnsureNativeCommandListResetHook("))
        self.run_cpp("native-hook-retry", r'''
#include <cstdint>
#include <vector>
using SRWLOCK = int;
using ULONGLONG = unsigned long long;
ULONGLONG now_tick = 1000;
ULONGLONG GetTickCount64() { return now_tick; }
int lock_depth = 0;
void AcquireSRWLockExclusive(SRWLOCK *) { ++lock_depth; }
void ReleaseSRWLockExclusive(SRWLOCK *) { --lock_depth; }
template<class... T> void Log(const char *, T...) {}
struct ID3D12GraphicsCommandList { void **table; };
using ID3D12CommandQueue = ID3D12GraphicsCommandList;
constexpr int D3D12_COMMAND_LIST_TYPE_DIRECT = 0;
struct D3D12_COMMAND_QUEUE_DESC { int Type; };
D3D12_COMMAND_QUEUE_DESC ID3D12CommandQueue_GetDesc(ID3D12CommandQueue *) { return {0}; }
namespace reshade::api {
enum class device_api { d3d11, d3d12 };
struct device { device_api api = device_api::d3d12; device_api get_api() { return api; } };
struct command_list {
    device *owner; std::uintptr_t native;
    device *get_device() { return owner; }
    std::uintptr_t get_native() { return native; }
};
using command_queue = command_list;
}
enum MH_STATUS { MH_OK, MH_ERROR_ALREADY_INITIALIZED, MH_ERROR_ALREADY_CREATED,
    MH_ERROR_ENABLED, MH_ERROR_MEMORY_PROTECT, MH_ERROR_UNSUPPORTED_FUNCTION };
MH_STATUS init_status = MH_OK, create_status = MH_OK;
int enable_failures = 0, create_calls = 0, enable_calls = 0;
std::vector<void *> created, enabled;
MH_STATUS MH_Initialize() { return init_status; }
MH_STATUS MH_CreateHook(void *target, void *, void **original) {
    ++create_calls;
    if (create_status != MH_OK) return create_status;
    if (std::find(created.begin(), created.end(), target) != created.end()) return MH_ERROR_ALREADY_CREATED;
    created.push_back(target); *original = target; return MH_OK;
}
MH_STATUS MH_EnableHook(void *target) {
    ++enable_calls;
    if (enable_failures > 0) { --enable_failures; return MH_ERROR_MEMORY_PROTECT; }
    if (std::find(enabled.begin(), enabled.end(), target) != enabled.end()) return MH_ERROR_ENABLED;
    enabled.push_back(target); return MH_OK;
}
SRWLOCK g_queue_execute_hook_lock = 0, g_command_list_reset_hook_lock = 0;
void *g_queue_execute_hook_targets[4] = {}, *g_command_list_reset_hook_targets[4] = {};
bool g_queue_execute_hook_enabled[4] = {}, g_command_list_reset_hook_enabled[4] = {};
ULONGLONG g_queue_execute_hook_retry_after[4] = {}, g_command_list_reset_hook_retry_after[4] = {};
std::size_t g_queue_execute_hook_count = 0, g_command_list_reset_hook_count = 0;
void *g_original_queue_execute[4] = {}, *g_original_command_list_reset[4] = {};
void *g_queue_execute_detours[4] = {}, *g_command_list_reset_detours[4] = {};
FUNCTIONS
void reset_fixture() {
    std::fill_n(g_queue_execute_hook_targets, 4, nullptr);
    std::fill_n(g_command_list_reset_hook_targets, 4, nullptr);
    std::fill_n(g_queue_execute_hook_enabled, 4, false);
    std::fill_n(g_command_list_reset_hook_enabled, 4, false);
    std::fill_n(g_queue_execute_hook_retry_after, 4, 0);
    std::fill_n(g_command_list_reset_hook_retry_after, 4, 0);
    now_tick = 1000;
    std::fill_n(g_original_queue_execute, 4, nullptr);
    std::fill_n(g_original_command_list_reset, 4, nullptr);
    g_queue_execute_hook_count = g_command_list_reset_hook_count = 0;
    init_status = create_status = MH_OK;
    enable_failures = create_calls = enable_calls = lock_depth = 0;
    created.clear(); enabled.clear();
}
int main() {
    for (bool queue : {false, true}) {
        reshade::api::device device;
        int targets[5] = {};
        void *vtable[11] = {}; vtable[10] = &targets[0];
        ID3D12GraphicsCommandList native{vtable};
        reshade::api::command_list owner{&device, reinterpret_cast<std::uintptr_t>(&native)};
        auto ensure = [&] { return queue ? EnsureNativeQueueExecuteHook(&owner) : EnsureNativeCommandListResetHook(&owner); };
        reset_fixture(); enable_failures = 2;
        check(!ensure(), "first enable failure stays unavailable");
        check(!ensure() && enable_calls == 1, "no same-frame retry can freeze threads again");
        now_tick += 999;
        check(!ensure() && enable_calls == 1, "failed enable backs off for one second");
        ++now_tick;
        check(!ensure(), "repeated enable failure is not falsely marked ready");
        check(!ensure() && enable_calls == 2, "backoff renews after another failure");
        check(create_calls == 1, "retry reuses only its own created trampoline");
        now_tick += 1000;
        check(ensure(), "successful later enable recovers");
        check(create_calls == 1 && enable_calls == 3, "recovery enables without duplicate creation");
        check(ensure() && enable_calls == 3, "ready hook has no per-call republishing");
        check(lock_depth == 0, "retry releases publication lock on every exit");

        reset_fixture(); created.push_back(vtable[10]);
        check(!ensure(), "unknown pre-existing hook owner remains fail-closed");
        check(enable_calls == 0, "never enables another owner's trampoline");
        check(g_queue_execute_hook_count == 0 && g_command_list_reset_hook_count == 0,
              "foreign hook does not reserve a slot");

        reset_fixture(); create_status = MH_ERROR_UNSUPPORTED_FUNCTION;
        check(!ensure() && enable_calls == 0, "failed create does not enable");
        create_status = MH_OK;
        check(ensure(), "failed create is retryable without cached false success");
        check(lock_depth == 0, "create failure releases publication lock");

        reset_fixture(); init_status = MH_ERROR_MEMORY_PROTECT;
        check(!ensure() && create_calls == 0, "initialization failure stays unavailable");
        init_status = MH_ERROR_ALREADY_INITIALIZED;
        check(ensure(), "already initialized library can create own hook");

        reset_fixture();
        for (int i = 0; i < 5; ++i) {
            vtable[10] = &targets[i];
            check(ensure() == (i < 4), "target family capacity enforced");
        }
        check(create_calls == 4, "capacity rejection never creates untracked hook");
        check(lock_depth == 0, "capacity failure releases publication lock");
    }
    return finish("actual native Reset/ExecuteCommandLists publication with injected MinHook failures");
}
'''.replace("FUNCTIONS", functions))

    def test_overlay_uses_one_independent_stable_name(self) -> None:
        calls = re.findall(r"reshade::(register_overlay|unregister_overlay)\(([^,]+), DrawSettings\);", source)
        self.assertEqual(len(calls), 3, "attach plus both cleanup paths")
        self.assertEqual([op for op, _ in calls].count("register_overlay"), 1)
        names = {name.strip() for _, name in calls}
        self.assertEqual(len(names), 1, "register/unregister must use same name")
        self.assertNotIn("nullptr", names, "settings must use their own ReShade window")
        name = names.pop()
        if not name.startswith('"'):
            declared = re.search(r"(?:constexpr|const)\s+char\s+" + re.escape(name) + r'\[\]\s*=\s*"([^"]+)"', source + ui_source)
            self.assertIsNotNone(declared, "name must be stable, not language/version/config dependent")
            name = declared.group(1)
        self.assertNotEqual(name.strip('"'), "DLSS5 F8 Capture")
        self.assertNotRegex(ui_source, r"ImGui::(?:SetNextWindowPos|SetNextWindowSize|SetWindowFocus)\(")

    def test_existing_ui_default_and_parameter_types(self) -> None:
        self.assertRegex(source, r'L"UICorrection",\s*1,\s*g_ini_path')
        self.assertIn('SetUInt(params, "DLSSNR.UICorrection", tuning.ui_correction ? 1u : 0u);', source)
        self.assertIn('SetInt(params, "DLSSNR.UseAutoMask", tuning.auto_mask ? 1 : 0);', source)
        self.assertIn('SetFloat(params, "DLSSNR.SkinStructureStrength", tuning.skin_structure);', source)
        self.assertIn('WriteIniFloat(L"SkinStructureStrength", config.skin_structure)', source)
        self.assertIn('WriteIniInt(L"UICorrection", config.ui_correction ? 1 : 0)', source)


if __name__ == "__main__":
    unittest.main(argv=[__file__], verbosity=2)
