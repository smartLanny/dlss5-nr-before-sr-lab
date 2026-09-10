"""Compile the small D13 menu adapters with a CPU-only ImGui recorder.

The fixture checks that the new quick/details split and the single light
method selector remain ordinary UI code. It does not construct Core, NGX,
GPU resources, or a game.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for at in range(brace, len(source)):
        if source[at] == "{":
            depth += 1
        elif source[at] == "}":
            depth -= 1
            if depth == 0:
                return source[start : at + 1]
    raise ValueError(marker)


PRELUDE = r'''
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "nr_dual_observation.hpp"
struct ImVec2 { float x, y; ImVec2(float a=0, float b=0) : x(a), y(b) {} };
struct ChainConfig {
    unsigned nr_passes=1, nr_second_numerator=1, nr_second_denominator=1;
    bool generic_residual=false, region_enabled=false;
};
bool chinese=false;
bool UseChineseUi() { return chinese; }
std::string click;
bool show_more=false, combo_changed=false;
int combo_value=0;
namespace nr_dual_status_runtime {
using UiState=nr_dual_observation::UiState;
using UiSnapshot=nr_dual_observation::Snapshot;
UiSnapshot current{};
inline UiSnapshot SnapshotUi() noexcept { return current; }
}
std::string rendered;
namespace ImGui {
void SeparatorText(const char*) {}
bool Checkbox(const char*, bool*) { return false; }
bool SliderFloat(const char*, float*, float, float, const char*) { return false; }
bool Button(const char* label) { return !click.empty() && std::strstr(label, click.c_str()) != nullptr; }
bool CollapsingHeader(const char*) { return show_more; }
bool Combo(const char*, int* value, const char*) {
    if (!combo_changed) return false;
    *value=combo_value; combo_changed=false; return true;
}
void SameLine() {}
void TextDisabled(const char* format, ...) { char value[2048];va_list args;va_start(args,format);std::vsnprintf(value,sizeof(value),format,args);va_end(args);rendered+=value; }
void TextWrapped(const char* format, ...) { char value[2048];va_list args;va_start(args,format);std::vsnprintf(value,sizeof(value),format,args);va_end(args);rendered+=value; }
void TextUnformatted(const char*) {}
void AlignTextToFramePadding() {}
void SetNextItemWidth(float) {}
}
void SettingsTooltip(const char*) {}
void SettingsNote(const char*) {}
void SettingsField(const char*) {}
bool SettingsSliderFloat(const char*, const char*, float*, float, float, const char*) { return false; }
std::atomic<unsigned> g_config_generation{1};
std::atomic<unsigned long long> g_dual_allocated_bytes{0};
namespace nr_dual_capture { std::atomic<unsigned> status{0}; }
void RequestStageCapture() {}
namespace nr_dual_runtime {
enum class Status : unsigned { Off, PrimaryWarming, Preparing, CreatePending, Active,
    UnsupportedGuides, ResourceBudget, QueuePending, SlotPending, Failed,
    DeviceIdentity, PipelineUnavailable, ResourceContract };
std::atomic<Status> status{Status::Off};
std::atomic<unsigned> primary_width{0},primary_height{0},second_width{0},second_height{0};
std::atomic<unsigned long long> second_evaluations{0};
std::atomic<unsigned> time_samples{0},time_config{0};
std::atomic<double> time_median[7]{},time_p95[7]{};
}
'''


@unittest.skipUnless(os.name == "nt", "CPU UI adapter compile requires the Windows toolchain")
class D13UiCompile(unittest.TestCase):
    def test_quick_details_and_method_selector_compile_and_preserve_values(self):
        configured = os.environ.get("NR_LLVM_BIN")
        bins = sorted((ROOT.parent / "dlss5-lab" / "toolchains").glob("llvm-mingw-*/bin"))
        compiler = (Path(configured) if configured else bins[-1] if bins else Path("missing")) / "clang++.exe"
        if not compiler.is_file():
            self.skipTest("Set NR_LLVM_BIN to the pinned LLVM-MinGW bin directory")
        dual = (ROOT / "src" / "nr_dual_ui.inl").read_text(encoding="utf-8")
        settings = (ROOT / "src" / "nr_settings_ui.inl").read_text(encoding="utf-8")
        selector = definition(settings, "unsigned DrawLightAdjustmentMethodSelector(")
        main = selector + r'''
int main() {
    ChainConfig c; c.nr_passes=2; c.nr_second_numerator=2; c.nr_second_denominator=3;
    show_more=true; click="2/3";
    bool changed=false, discrete=false;
    nr_dual_runtime::DrawQuickUi(c, changed, discrete);
    if (c.nr_second_numerator!=2 || c.nr_second_denominator!=3 || !changed || !discrete) return 1;
    c.nr_second_numerator=1; c.nr_second_denominator=2; changed=discrete=false;
    show_more=false; click="50%";
    nr_dual_runtime::DrawQuickUi(c, changed, discrete);
    if (c.nr_second_numerator!=1 || c.nr_second_denominator!=2) return 2;
    c.generic_residual=false; c.region_enabled=false; combo_changed=true; combo_value=1;
    changed=discrete=false;
    if (DrawLightAdjustmentMethodSelector(c, changed, discrete)!=1 ||
        !c.region_enabled || c.generic_residual || !changed || !discrete) return 3;
    c.generic_residual=true; c.region_enabled=true; combo_changed=false;
    if (DrawLightAdjustmentMethodSelector(c, changed, discrete)!=2) return 4;
    using UiState=nr_dual_status_runtime::UiState;
    click.clear(); changed=discrete=false;
    nr_dual_status_runtime::current.state=UiState::Active;
    rendered.clear();nr_dual_runtime::DrawQuickUi(c,changed,discrete,true);
    if(rendered.find("waiting for the requested settings")==std::string::npos ||
        rendered.find("Second AI: active")!=std::string::npos)return 5;
    nr_dual_status_runtime::current.state=UiState::Failed;
    nr_dual_status_runtime::current.fatal_code=0xBAD00005u;
    rendered.clear();nr_dual_runtime::DrawQuickUi(c,changed,discrete,true);
    if(rendered.find("restart the game")==std::string::npos ||
        rendered.find("waiting for the requested settings")!=std::string::npos)return 6;
    nr_dual_runtime::DrawDetailsUi();
    std::puts("D13 UI adapters: quick/details and method selector compiled");
    return 0;
}
'''
        source = PRELUDE + dual + main
        with tempfile.TemporaryDirectory(prefix="nr-d13-ui-") as directory:
            work = Path(directory)
            cpp = work / "d13.cpp"
            exe = work / "d13.exe"
            cpp.write_text(source, encoding="utf-8")
            built = subprocess.run([str(compiler), "-std=c++20", "-O0", "-static", "-Wall", "-Wextra", "-Werror",
                                    "-I", str(ROOT / "src"), str(cpp), "-o", str(exe)], capture_output=True, text=True, timeout=45)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("D13 UI adapters", ran.stdout)


if __name__ == "__main__":
    unittest.main()
