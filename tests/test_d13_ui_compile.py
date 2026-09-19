"""Compile the retained D13 and new D15 menu adapters with a CPU ImGui recorder.

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
#include <vector>
#include "nr_dual_observation.hpp"
#include "nr_processing_route.h"
#include "nr_layer_config.h"
#include "nr_compat_present_policy.hpp"
struct ImVec2 { float x, y; ImVec2(float a=0, float b=0) : x(a), y(b) {} };
struct ImVec4 { float x=0,y=0,z=0,w=1; };
enum { ImGuiCol_Button, ImGuiCol_Header, ImGuiTreeNodeFlags_DefaultOpen, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem, ImGuiMouseButton_Left };
enum class NrJitterMode:int {Legacy=0,GameInputPixel=1};
CHAIN_CONFIG

bool chinese=false;
bool UseChineseUi() { return chinese; }
std::string click;
bool show_more=false, combo_changed=false;
bool mouse_down=false;std::string activate,hover,last_button;
int combo_value=0;
namespace nr_dual_status_runtime {
using UiState=nr_dual_observation::UiState;
using UiSnapshot=nr_dual_observation::Snapshot;
UiSnapshot current{};
inline UiSnapshot SnapshotUi() noexcept { return current; }
}
std::string rendered;
std::vector<std::string> buttons,checkboxes;
int current_layer=-1,slider_layer=-1,button_layer=-1;
std::string slider_key;float slider_value=0;bool active_item=false;
unsigned disabled=0;
namespace ImGui {
void SeparatorText(const char*) {}
bool Checkbox(const char* label, bool* value) {
    checkboxes.emplace_back(label);
    if(disabled || click.empty() || std::strstr(label,click.c_str())==nullptr)return false;
    *value=!*value;return true;
}
bool SliderFloat(const char*, float*, float, float, const char*) { return false; }
bool Button(const char* label,ImVec2 = {}) { last_button=label; buttons.emplace_back(label); return !disabled && (button_layer<0||current_layer==button_layer) && !click.empty() && std::strstr(label, click.c_str()) != nullptr; }
void BeginDisabled(bool value=true) { if(value)++disabled; }
void EndDisabled() { if(disabled)--disabled; }
struct Style { ImVec2 ItemSpacing{8,4}; } style;
Style& GetStyle(){return style;}
float GetFontSize(){return 16;}
float GetFrameHeight(){return 24;}
const ImVec4& GetStyleColorVec4(int){static ImVec4 color;return color;}
void PushStyleColor(int,const ImVec4&){}
void PopStyleColor(){}
void PushID(const char*){}
void PushID(int i){current_layer=i;}
void PopID(){current_layer=-1;}
bool IsAnyItemActive(){return active_item;}
bool CollapsingHeader(const char*,int=0) { return show_more; }
bool Combo(const char*, int* value, const char*) {
    if (!combo_changed) return false;
    *value=combo_value; combo_changed=false; return true;
}
bool IsItemActivated(){return current_layer==button_layer&&activate==last_button;}
bool IsItemHovered(int){return current_layer==button_layer&&hover==last_button;}
bool IsMouseDown(int){return mouse_down;}
void SameLine() {}
void TextDisabled(const char* format, ...) { char value[2048];va_list args;va_start(args,format);std::vsnprintf(value,sizeof(value),format,args);va_end(args);rendered+=value; }
void TextWrapped(const char* format, ...) { char value[2048];va_list args;va_start(args,format);std::vsnprintf(value,sizeof(value),format,args);va_end(args);rendered+=value; }
void TextUnformatted(const char*) {}
void AlignTextToFramePadding() {}
void SetNextItemWidth(float) {}
}
void SettingsSection(const char*) {}
void SettingsTooltip(const char*) {}
float SettingsContentWidth(){return 600;}
void SettingsNote(const char*) {}
void SettingsField(const char*) {}
bool SettingsSliderFloat(const char*, const char* id, float* v, float, float, const char*) {
    if(disabled||current_layer!=slider_layer||slider_key!=id)return false;*v=slider_value;slider_layer=-1;return true;
}
std::atomic<unsigned> g_config_generation{1};
std::atomic<unsigned long long> g_dual_allocated_bytes{0};
namespace nr_dual_capture { std::atomic<unsigned> status{0}; }
void RequestStageCapture() {}
namespace nr_processing_runtime {
std::atomic<unsigned long long> next_selection{1};
nr_processing::Session session;
nr_processing::Start Effective(){return session.Current();}
bool TrySnapshot(nr_processing::Snapshot& value){value={};value.current=session.Current();return true;}
bool FgRedirectRequested(){return false;}
}
namespace nr_compat_present_runtime {
nr_compat_present::Observation current{};
bool TrySnapshot(nr_compat_present::Observation& value){value=current;return true;}
}
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
        processing = definition(settings, "bool DrawProcessingModeBlocks(")
        core=(ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
        layers=(ROOT / 'src/nr_layers_ui.inl').read_text(encoding='utf-8')
        config=definition(core,'struct ChainConfig')+';'
        footer=settings[settings.rindex('    if (changed) {'):]
        apply_blocks=definition(footer,'    if (changed) {')+'\n'+definition(footer,'    if (has_pending &&')
        release_harness=r'''
enum class OverlayApplyResult {Applied,Busy,Conflict};
namespace nr_model_controls {enum class Action {None};}
struct Current {unsigned generation=1;} current;
ChainConfig pending{},applied{};unsigned pending_generation=0,apply_calls=0;
bool has_pending=false,edit_conflict=false,pending_picture_reset=false,pending_light_undo=false;
int light_undo=0,model_undo=0;auto pending_model_action=nr_model_controls::Action::None;
OverlayApplyResult apply_result=OverlayApplyResult::Applied;
OverlayApplyResult ApplySettingsOverlay(const ChainConfig& value,unsigned,bool,bool,int,nr_model_controls::Action,int) {
    ++apply_calls;if(apply_result==OverlayApplyResult::Applied)applied=value;return apply_result;
}
void ApplyUiEdits(ChainConfig next,bool changed,bool apply_discrete_edit) {
'''+apply_blocks+'\n}\n'
        main = selector + processing + release_harness + r'''
int main() {
    ChainConfig c; c.nr_passes=2; c.nr_second_numerator=2; c.nr_second_denominator=3;
    show_more=true; click.clear();
    bool changed=false, discrete=false;
    nr_dual_runtime::DrawQuickUi(c, changed, discrete);
    if (c.nr_second_numerator!=2 || c.nr_second_denominator!=3 || changed || discrete) return 1;
    if (std::find(buttons.begin(),buttons.end(),"50%###dual-scale-50")!=buttons.end() ||
        std::find(buttons.begin(),buttons.end(),"75%###dual-scale-75")!=buttons.end() ||
        std::find(buttons.begin(),buttons.end(),"100%###dual-scale-100")!=buttons.end()) return 2;
    const auto primary=nr_layers::Primary(c);
    for(unsigned pass=2;pass<=5;++pass){
        click="Enable layer "+std::to_string(pass);changed=discrete=false;
        nr_dual_runtime::DrawQuickUi(c,changed,discrete);
        if(nr_layers::Active(c.nr_layers)!=pass||!changed||!discrete)return 3;
    }
    if(nr_layers::Primary(c)!=primary)return 4;
    c.nr_layers[2].intensity=.37f;c.nr_layers[2].style=2;
    click="Enable layer 2";changed=discrete=false;
    nr_dual_runtime::DrawQuickUi(c,changed,discrete);
    if(nr_layers::Active(c.nr_layers)!=1||c.nr_layers[2].intensity!=.37f||c.nr_layers[2].style!=2)return 5;
    checkboxes.clear();click.clear();nr_dual_runtime::DrawQuickUi(c,changed,discrete);
    if(std::find(checkboxes.begin(),checkboxes.end(),"Enable layer 3")!=checkboxes.end())return 6;
    click="Enable layer 2";nr_dual_runtime::DrawQuickUi(c,changed,discrete);click.clear();
    if(nr_layers::Active(c.nr_layers)!=5||c.nr_layers[2].intensity!=.37f)return 7;
    slider_layer=3;slider_key="##intensity";slider_value=.82f;active_item=true;
    changed=discrete=false;nr_dual_runtime::DrawQuickUi(c,changed,discrete);
    if(!changed||discrete||c.nr_layers[2].intensity!=.82f||nr_layers::Primary(c)!=primary)return 8;
    ApplyUiEdits(c,changed,discrete);
    if(!has_pending||apply_calls)return 9;
    active_item=false;ApplyUiEdits(c,false,false);
    if(has_pending||apply_calls!=1||applied.nr_layers[2].intensity!=.82f)return 10;
    active_item=true;ApplyUiEdits(c,true,true);
    if(has_pending||apply_calls!=2)return 16;
    apply_result=OverlayApplyResult::Busy;ApplyUiEdits(c,true,true);
    if(!has_pending||apply_calls!=3)return 17;
    apply_result=OverlayApplyResult::Conflict;active_item=false;ApplyUiEdits(c,false,false);
    if(has_pending||!edit_conflict||apply_calls!=4)return 18;
    click.clear();
    const auto before_reset=c;click="Reset this layer";button_layer=3;changed=discrete=false;
    nr_dual_runtime::DrawQuickUi(c,changed,discrete);button_layer=-1;click.clear();
    if(!changed||!discrete||nr_layers::Read(c,3)!=nr_layers::Model{}||
       nr_layers::Primary(c)!=primary||c.nr_layers[0]!=before_reset.nr_layers[0]||
       c.nr_layers[1]!=before_reset.nr_layers[1]||c.nr_layers[3]!=before_reset.nr_layers[3]||
       nr_layers::Active(c.nr_layers)!=5||c.nr_second_numerator!=2||c.nr_second_denominator!=3)return 21;
    for(unsigned layer=0;layer<5;++layer){
        click="Natural";button_layer=layer;changed=discrete=false;
        nr_dual_runtime::DrawQuickUi(c,changed,discrete);click.clear();
        if(nr_layers::Read(c,layer).style!=1||!changed||!discrete)return 30;
        activate="Natural";hover="Cinematic";mouse_down=true;changed=discrete=false;
        nr_dual_runtime::DrawQuickUi(c,changed,discrete);activate.clear();
        if(nr_layers::Read(c,layer).style!=1||changed||discrete)return 31;
        mouse_down=false;nr_dual_runtime::DrawQuickUi(c,changed,discrete);
        if(nr_layers::Read(c,layer).style!=2||!changed||!discrete)return 32;
        hover.clear();
    }
    button_layer=-1;
    c.generic_residual=false; c.region_enabled=false; combo_changed=true; combo_value=1;
    changed=discrete=false;
    if (DrawLightAdjustmentMethodSelector(c, changed, discrete)!=1 ||
        !c.region_enabled || c.generic_residual || !changed || !discrete) return 3;
    c.generic_residual=true; c.region_enabled=true; combo_changed=false;
    if (DrawLightAdjustmentMethodSelector(c, changed, discrete)!=2) return 4;
    using UiState=nr_dual_status_runtime::UiState;
    click.clear(); changed=discrete=false;
    nr_dual_status_runtime::current.state=UiState::Active;
    rendered.clear();nr_dual_runtime::DrawDetailsUi(true,true);
    if(rendered.find("Settings pending:")==std::string::npos ||
        rendered.find("Second AI: active")!=std::string::npos)return 5;
    nr_dual_status_runtime::current.state=UiState::Failed;
    nr_dual_status_runtime::current.fatal_code=0xBAD00005u;
    rendered.clear();nr_dual_runtime::DrawDetailsUi(true,true);
    if(rendered.find("restart the game")==std::string::npos ||
        rendered.find("Settings pending:")!=std::string::npos)return 6;
    nr_dual_runtime::DrawDetailsUi();
    for(bool zh:{false,true}) {
        chinese=zh;c.nr_layers={};
        for(unsigned pass=2;pass<=5;++pass){
            click=zh?"启用第 "+std::to_string(pass)+" 层":"Enable layer "+std::to_string(pass);
            changed=discrete=false;nr_dual_runtime::DrawQuickUi(c,changed,discrete);
            if(nr_layers::Active(c.nr_layers)!=pass||!changed||!discrete)return 19;
        }
        checkboxes.clear();click.clear();nr_dual_runtime::DrawQuickUi(c,changed,discrete);
        if(std::find(checkboxes.begin(),checkboxes.end(),zh?"启用第 6 层":"Enable layer 6")!=checkboxes.end())return 20;
        const char* labelsEn[]={"Enhance first","Upscale first","Compatibility"};
        const char* labelsZh[]={"先增强，后放大","先放大，后增强","兼容模式"};
        for(unsigned route=0;route<3;++route){
            const auto previous=c.processing_selection;const int oldmode=c.mode;
            click=zh?labelsZh[route]:labelsEn[route];buttons.clear();
            if(!DrawProcessingModeBlocks(c,true,true,1)||buttons.size()!=3||
               static_cast<unsigned>(c.processing_start)!=route||c.processing_selection==previous)return 11;
            if(c.mode!=(route<2?(route==0?2:1):oldmode))return 12;
            const auto selected=c.processing_selection;
            if(!DrawProcessingModeBlocks(c,true,true,1)||c.processing_selection==selected)return 13;
        }
        c.processing_start=nr_processing::Start::Before;c.mode=2;const auto saved=c.processing_selection;
        nr_processing_runtime::session.Select(nr_processing::Start::Present,100);
        nr_compat_present_runtime::current.color_only=true;
        click.clear();rendered.clear();
        if(DrawProcessingModeBlocks(c,true,true,1)||c.processing_start!=nr_processing::Start::Before||
           c.processing_selection!=saved||c.mode!=2)return 14;
        if(rendered.find("F8")==std::string::npos||rendered.find(zh?"占位深度和运动":"placeholder depth and motion")==std::string::npos)return 15;
        if(rendered.find(zh?"每帧重置历史":"resetting history each frame")==std::string::npos)return 21;
        nr_compat_present_runtime::current.color_only=false;rendered.clear();
        DrawProcessingModeBlocks(c,true,true,1);
        if(rendered.find(zh?"占位深度和运动":"placeholder depth and motion")!=std::string::npos)return 22;
    }
    std::puts("D13 UI adapters: quick/details and method selector compiled");
    std::puts("D21 UI adapters: nested layers 2-5, retained values, local reset and release-to-apply compiled");
    return 0;
}
'''
        source = PRELUDE.replace("CHAIN_CONFIG",config) + layers + dual + main
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
