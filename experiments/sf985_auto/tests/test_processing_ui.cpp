// Executes the actual replacement UI function with a recording ImGui shim.
// Not a Windows ABI, font, GPU, ReShade or game validation.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
struct ImVec2 { float x,y; ImVec2(float a=0,float b=0):x(a),y(b){} };
struct ImVec4 {};
struct ChainConfig { int mode=2; bool enabled=true; float strength=1.2f; };
constexpr int ImGuiCol_Button=0, ImGuiCol_Header=1;
static unsigned checks=0;
#define CHECK(x) do {++checks; if(!(x))throw std::runtime_error(#x);} while(0)
namespace test {
struct Button {std::string label;float width,height;bool disabled,selected;int id;};
static std::vector<Button> buttons;
static std::vector<std::string> tips;
static std::vector<int> ids;
static int disabled=0, selected=0, click=-1, same_line=0;
static bool zh=false;
static float available=560, font=16;
}
namespace ImGui {
struct Style { ImVec2 ItemSpacing{8,7},FramePadding{4,3}; };
static Style style;
const Style& GetStyle(){return style;}
const ImVec4& GetStyleColorVec4(int){static ImVec4 color;return color;}
float GetFrameHeight(){return test::font+2*style.FramePadding.y;}
void PushID(const char*){test::ids.push_back(-1);}
void PushID(int n){test::ids.push_back(n);}
void PopID(){CHECK(!test::ids.empty());test::ids.pop_back();}
void BeginDisabled(){++test::disabled;}
void EndDisabled(){CHECK(test::disabled>0);--test::disabled;}
void PushStyleColor(int,const ImVec4&){++test::selected;}
void PopStyleColor(){CHECK(test::selected>0);--test::selected;}
void SameLine(){++test::same_line;}
bool Button(const char* text,ImVec2 size){
    CHECK(!test::ids.empty());
    test::buttons.push_back({text,size.x,size.y,test::disabled>0,test::selected>0,test::ids.back()});
    // Deliberately permit a bogus click while disabled to test the caller guard.
    return test::click==test::ids.back();
}
}
ImVec2 measure(const char* label,const char*,bool hide_id,float){
    float line=0,largest=0;unsigned lines=1;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(label);*p;++p){
        if(hide_id && p[0]=='#' && p[1]=='#')break;
        if(*p=='\n'){largest=std::max(largest,line);line=0;++lines;continue;}
        if((*p&0xc0)==0x80)continue; // one CJK codepoint, not one UTF-8 byte
        line+=*p<128?test::font*0.58f:test::font;
    }
    return {std::max(largest,line),test::font*static_cast<float>(lines)};
}
struct Functions { ImVec2 (*CalcTextSize)(const char*,const char*,bool,float)=measure; };
Functions* imgui_function_table_instance(){static Functions f;return &f;}
namespace nr_imgui_abi {
template<class... Args> ImVec2 CallHostVec2(ImVec2(*fn)(Args...),Args... args){return fn(args...);}
}
bool UseChineseUi(){return test::zh;}
float SettingsContentWidth(){return test::available;}
void SettingsTooltip(const char* s){test::tips.emplace_back(s);}
#include "../ui/DrawProcessingModeBlocks.inl"

void Reset(bool zh,float width,float font,int click=-1,bool parent_disabled=false){
    test::buttons.clear();test::tips.clear();test::ids.clear();test::same_line=0;
    test::disabled=parent_disabled?1:0;test::selected=0;test::click=click;
    test::zh=zh;test::available=width;test::font=font;
}
void Verify(int mode,bool guarded){
    CHECK(test::buttons.size()==3);CHECK(test::tips.size()==3);
    CHECK(test::ids.empty());CHECK(test::selected==0);
    CHECK(test::buttons[2].disabled);CHECK(!test::buttons[2].selected);
    for(unsigned i=0;i<3;++i){
        CHECK(test::buttons[i].id==static_cast<int>(i));
        CHECK(test::buttons[i].width>0 && test::buttons[i].height>0);
        if(test::available>=200){
            const auto text=measure(test::buttons[i].label.c_str(),nullptr,true,-1);
            CHECK(text.x+2*ImGui::style.FramePadding.x<=test::buttons[i].width+0.01f);
            CHECK(text.y+2*ImGui::style.FramePadding.y<=test::buttons[i].height+0.01f);
        }
    }
    CHECK(test::buttons[0].selected==(mode==2&&!guarded));
    CHECK(test::buttons[1].selected==(mode==1||guarded));
    if(test::same_line){CHECK(test::same_line==2);CHECK(3*test::buttons[0].width+16<=std::max(test::available,1.f)+0.01f);}
}
int main(){
    try {
        for(bool zh:{false,true})for(float width:{320.f,420.f,560.f,900.f}){
            Reset(zh,width,16);ChainConfig c{};CHECK(!DrawProcessingModeBlocks(c,false,false,2));Verify(2,false);
            CHECK(c.mode==2&&c.enabled&&c.strength==1.2f);CHECK(test::disabled==0);
        }
        for(int start:{1,2})for(int click:{0,1,2})for(bool rr:{false,true})for(bool compat:{false,true}){
            Reset(false,560,16,click);ChainConfig c{start,false,0.7f};
            const bool changed=DrawProcessingModeBlocks(c,rr,compat,start);
            const bool expected=!rr&&!compat&&click<2&&start!=(click==0?2:1);
            CHECK(changed==expected);CHECK(c.mode==(expected?(click==0?2:1):start));
            CHECK(!c.enabled&&c.strength==0.7f);CHECK(c.mode!=3);CHECK(test::disabled==0);
        }
        // No user action: saved PRE must not turn into saved POST because RR is active.
        Reset(true,420,16);ChainConfig saved{};CHECK(!DrawProcessingModeBlocks(saved,true,false,2));
        Verify(2,true);CHECK(saved.mode==2);
        // Selected route changes do not mutate NR strength/F6, and invalid observations
        // do not select the disconnected Present tile.
        Reset(false,560,16);CHECK(!DrawProcessingModeBlocks(saved,false,false,3));
        CHECK(!test::buttons[0].selected&&!test::buttons[1].selected&&!test::buttons[2].selected);
        // Nested disabled scopes must balance (e.g. F7 capture).
        Reset(true,560,20,-1,true);CHECK(!DrawProcessingModeBlocks(saved,false,false,2));CHECK(test::disabled==1);
        // Layout decisions react to font scale instead of hardcoding 3 narrow columns.
        for(bool zh:{false,true})for(float font:{12.f,20.f,28.f})for(float width:{200.f,420.f,900.f}){
            Reset(zh,width,font);CHECK(!DrawProcessingModeBlocks(saved,false,false,2));Verify(2,false);
        }
        Reset(false,1,16);DrawProcessingModeBlocks(saved,false,false,2);CHECK(test::same_line==0);
        CHECK(test::tips[2].find("does not enable or disable game SR")!=std::string::npos);
        std::cout<<checks<<" UI-shim checks passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<"Failed: "<<e.what()<<'\n';return 1;}
}
