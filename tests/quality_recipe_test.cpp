// Production recipe + panel with a small UI/configuration substitute.
// This validates host control flow, NOT a Windows Core or rendered ImGui menu.
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include "../src/nr_quality_recipe.h"
#include "../src/nr_colour_restore_policy.h"
#include "../src/nr_colour_lab_policy.h"
#include "../src/nr_colour_stage_policy.h"
#include "../src/nr_colour_edge_policy.h"
struct ChainConfig {
    int colour_lab_mode=1, colour_restore_mode=nr_colour_restore::kDefaultMode;
    int colour_input_curve=0, colour_scaled_proxy_mode=0, colour_edge_recovery=0;
    bool colour_neutral_probe=false;
    // Sentinels for unrelated user settings; no recipe may modify them.
    int model_preset=3, layers=4, route=2, ui_correction=0, history=911;
    float work_scale=.99f, exposure=7.f, colour_strength=1.37f, edge_guard=.81f;
    float lighting_lock=.21f, local_structure=2.f, local_tone=1.2f;
    bool operator==(const ChainConfig&) const = default;
};
enum class NrColorPolicyStatus { Current, Bypassed, Stale };
struct Policy {
    float requested_color_strength=1, effective_color_strength=1;
    int color_reason=0, requested_input_curve=0, recorded_input_curve=0;
    int recorded_restore_mode=0, recorded_scaled_proxy_mode=0, recorded_edge_recovery=0;
    bool restore_encoding_known=true, recorded_neutral_probe=false;
};
struct Sample { unsigned config_generation=4; NrColorPolicyStatus status=NrColorPolicyStatus::Current; Policy policy; } sample;
Sample SnapshotColorPolicy(){return sample;}
bool chinese=false, expanded=false; unsigned capture_status=0, capture_calls=0;
std::string clicked; int selected=0; std::string notes;
bool UseChineseUi(){return chinese;}
void SettingsField(const char*){}
void SettingsTooltip(const char*){}
void SettingsHelp(const char*){}
void SettingsNote(const char* s){notes+=s;notes+='\n';}
const char* NrColorPolicyStatusText(NrColorPolicyStatus,bool){return "status";}
const char* NrColorReasonName(int){return "reason";}
unsigned StageCaptureStatus(){return capture_status;}
bool RequestStageCapture(){++capture_calls;return true;}
namespace ImGui {
int disabled=0;
void PushID(const char*){} void PopID(){} void Separator(){}
void BeginDisabled(bool b){if(b)++disabled;} void EndDisabled(){disabled=0;}
bool Combo(const char* label,int* value,const char*){
    if(clicked.empty()||!std::strstr(label,clicked.c_str()))return false;
    *value=selected;clicked.clear();return true;
}
bool Checkbox(const char* label,bool* value){
    if(clicked.empty()||!std::strstr(label,clicked.c_str()))return false;
    *value=!*value;clicked.clear();return true;
}
bool CollapsingHeader(const char*){return expanded;}
bool Button(const char* label){
    if(disabled||clicked.empty()||!std::strstr(label,clicked.c_str()))return false;
    clicked.clear();return true;
}
void TextWrapped(const char*,...){}
}
#include "../src/nr_quality_ui.inl"
void unrelatedSame(const ChainConfig&a,const ChainConfig&b){
    auto c=a; c.colour_lab_mode=b.colour_lab_mode;c.colour_restore_mode=b.colour_restore_mode;
    c.colour_input_curve=b.colour_input_curve;c.colour_scaled_proxy_mode=b.colour_scaled_proxy_mode;
    c.colour_edge_recovery=b.colour_edge_recovery;c.colour_neutral_probe=b.colour_neutral_probe;
    assert(c==b);
}
int main(){
    using nr_quality_recipe::Recipe;
    static_assert(nr_colour_restore::kDefaultMode==0);
    unsigned combinations=0;
    for(int lab=-1;lab<=3;++lab)for(int restore=-1;restore<=3;++restore)
    for(int curve=-1;curve<=2;++curve)for(int scaled=0;scaled<=1;++scaled)
    for(int edge=0;edge<=1;++edge)for(int probe=0;probe<=1;++probe){
        ChainConfig c;c.colour_lab_mode=lab;c.colour_restore_mode=restore;
        c.colour_input_curve=curve;c.colour_scaled_proxy_mode=scaled;
        c.colour_edge_recovery=edge;c.colour_neutral_probe=probe;
        auto before=c;
        auto described=nr_quality_recipe::Describe(c);assert(c==before);
        assert((described==Recipe::Custom)==!(lab==1&&restore==0&&scaled==0&&edge==0&&!probe&&(curve==0||curve==1)));
        assert(!nr_quality_recipe::Select(c,Recipe::Custom)&&c==before);
        assert(!nr_quality_recipe::Select(c,static_cast<Recipe>(99))&&c==before);
        for(auto r:{Recipe::ColourBaseline,Recipe::N2Shadow}){
            c=before;bool changed=nr_quality_recipe::Select(c,r);
            assert(changed==(described!=r));assert(nr_quality_recipe::Describe(c)==r);
            unrelatedSame(before,c);auto final=c;assert(!nr_quality_recipe::Select(c,r));assert(c==final);
        }
        ++combinations;
    }
    // Painting, pending records and collapsing are never configuration writes.
    for(bool zh:{false,true})for(bool open:{false,true})for(bool pending:{false,true}){
        chinese=zh;expanded=open;ChainConfig c;c.colour_restore_mode=2;c.colour_edge_recovery=1;
        c.colour_neutral_probe=true;auto before=c;bool changed=false,apply=false;notes.clear();
        DrawColourLabControls(c,4,pending,changed,apply);
        assert(c==before&&!changed&&!apply);assert(!notes.empty());
    }
    chinese=false;expanded=false;ChainConfig c;c.colour_restore_mode=2;bool changed=false,apply=false;
    clicked="quality-recipe";selected=0;DrawColourLabControls(c,4,false,changed,apply);
    assert(changed&&apply&&nr_quality_recipe::Describe(c)==Recipe::ColourBaseline);
    changed=apply=false;clicked="quality-recipe";selected=1;DrawColourLabControls(c,4,false,changed,apply);
    assert(changed&&apply&&nr_quality_recipe::Describe(c)==Recipe::N2Shadow);
    for(bool pending:{true,false}){changed=apply=false;clicked="quality-capture";capture_calls=0;
        DrawColourLabControls(c,4,pending,changed,apply);assert(capture_calls==unsigned(!pending));}
    changed=apply=false;clicked="quality-capture";capture_calls=0;
    DrawColourLabControls(c,5,false,changed,apply);assert(capture_calls==0);
    std::printf("quality_recipe: %u configurations, bilingual UI control-flow and capture guards passed; host substitutes, not rendered UI/Core\n",combinations);
}
