// SPDX-License-Identifier: MIT
// Game-Bar-inspired UX. Not an Xbox widget and not a second graphics injector.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include "frame_history.hpp"
#include "core_bridge.hpp"
#include "nvml_monitor.hpp"
static_assert(RESHADE_API_VERSION==20,"Build and test against pinned ReShade 6.8.0/API20");
static_assert(IMGUI_VERSION_NUM==19250,"ReShade/ImGui ABI mismatch");
extern "C" __declspec(dllexport) const char *NAME="装机宅 · Quickbar";
extern "C" __declspec(dllexport) const char *DESCRIPTION="F10: 调节与监测；Home: 原始 ReShade。F6/F8 不变。观测 FPS 不代表插帧后的显示 FPS。";
namespace {
using Runtime=reshade::api::effect_runtime;
using Clock=std::chrono::steady_clock;
struct View {
    quickbar::FrameHistory history;bool open=false,builtin=false;int chart=0;
    unsigned width=0,height=0;unsigned long long touched=0;
};
struct App {
    std::recursive_mutex mutex;std::map<Runtime*,View> views;
    quickbar::NvmlMonitor sensors;quickbar::GpuSnapshot gpu;
    quickbar::CoreBridge core;std::wstring preferences;
    bool pinned=false;float opacity=.86f,font_scale=1.f;int key=VK_F10;
    unsigned long long sensor_tick=0,core_tick=0;
    Runtime* primary=nullptr;std::string feedback;
    void load(){
        pinned=GetPrivateProfileIntW(L"Quickbar",L"Pinned",0,preferences.c_str())!=0;
        int k=GetPrivateProfileIntW(L"Quickbar",L"KeyToggle",VK_F10,preferences.c_str());
        key=(k==VK_F9||k==VK_F10||k==VK_F11)?k:VK_F10;
        opacity=std::clamp(GetPrivateProfileIntW(L"Quickbar",L"OpacityPercent",86,preferences.c_str())/100.f,.6f,.98f);
        font_scale=std::clamp(GetPrivateProfileIntW(L"Quickbar",L"ScalePercent",100,preferences.c_str())/100.f,.85f,1.5f);
    }
    void save(){
        wchar_t text[32]{};bool ok=true;
        auto set=[&](const wchar_t*n,int v){swprintf_s(text,L"%d",v);ok=WritePrivateProfileStringW(L"Quickbar",n,text,preferences.c_str())!=FALSE&&ok;};
        set(L"Pinned",pinned?1:0);set(L"KeyToggle",key);set(L"OpacityPercent",int(opacity*100+.5f));set(L"ScalePercent",int(font_scale*100+.5f));
        feedback=ok?"外观设置已保存":"无法保存外观设置，请检查目录权限";
    }
};
App* app=nullptr;
bool foreground(Runtime*r){HWND h=static_cast<HWND>(r->get_hwnd());return h&&GetAncestor(h,GA_ROOT)==GetForegroundWindow()&&!IsIconic(h);}
double now_seconds(){LARGE_INTEGER f{},t{};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return double(t.QuadPart)/double(f.QuadPart);}
struct GlassStyle {
    GlassStyle(float alpha){
        ImGui::PushFont(nullptr,ImGui::GetStyle().FontSizeBase*app->font_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,12.f);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20,16));ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,10));
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.065f,.08f,.10f,alpha));
        ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.7f,.8f,.9f,.15f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.17f,.20f,.24f,.70f));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.22f,.29f,.27f,.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.27f,.39f,.31f,.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.30f,.46f,.33f,1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(.56f,.78f,.55f,1));
        ImGui::PushStyleColor(ImGuiCol_PlotLines,ImVec4(.56f,.78f,.68f,1));
    }
    ~GlassStyle(){ImGui::PopStyleColor(8);ImGui::PopStyleVar(4);ImGui::PopFont();}
};
void metric(const char*label,float value,const char*format){ImGui::TextDisabled("%s",label);if(value<0 || !std::isfinite(value))ImGui::TextUnformatted("--");else ImGui::Text(format,value);}
void send(uint32_t key,float v){auto r=app->core.write(key,v);app->feedback=r==NRUI_OK?"设置已交由核心应用":app->core.message;}
void draw_controls(){
    ImGui::TextUnformatted("AI 渲染");ImGui::Separator();
    const bool ready=app->core.available;auto&s=app->core.state;
    if(!ready)ImGui::TextWrapped("%s",app->core.message);
    ImGui::BeginDisabled(!ready||s.capture_busy!=0);
    bool enabled=s.enabled!=0;if(ImGui::Checkbox("启用 AI 渲染",&enabled))send(NRUI_ENABLED,enabled?1.f:0.f);
    int mode=s.mode==1?1:0;const char*modes[]={"性能链路  NR → SR","后置链路  SR → NR"};
    if(ImGui::Combo("处理链路",&mode,modes,2))send(NRUI_MODE,mode?1.f:2.f);
    static float edit_strength=1.2f;static bool editing=false;
    if(!editing)edit_strength=s.strength;
    ImGui::SliderFloat("增强强度",&edit_strength,0.f,4.f,"%.2f");editing=ImGui::IsItemActive();
    if(ImGui::IsItemDeactivatedAfterEdit()){send(NRUI_STRENGTH,edit_strength);editing=false;}
    ImGui::TextDisabled("残差强度；数值更大不等于画质更好");
    int style=static_cast<int>(s.style);const char*styles[]={"默认","自然","电影"};
    if(ImGui::Combo("风格",&style,styles,3))send(NRUI_STYLE,float(style));
    bool mask=s.auto_mask!=0;if(ImGui::Checkbox("人物遮罩",&mask))send(NRUI_MASK,mask?1.f:0.f);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("使用核心 AutoMask；不保证人物完全不被改变。");
    if(s.mode==2){int work=static_cast<int>(s.work_mode);const char*items[]={"参考","质量","均衡","性能","极限","自定义（核心设置）"};
        if(ImGui::Combo("AI 工作档位",&work,items,6))send(NRUI_WORK_MODE,float(work));
        ImGui::TextDisabled("只改 AI 工作分辨率，不改游戏 DLSS 档位");}
    ImGui::EndDisabled();
    if(s.capture_busy)ImGui::TextWrapped("F8 正在抓帧，设置暂时锁定。");
    if(ready){
        ImGui::Separator();
        if(!s.enabled)ImGui::TextUnformatted("AI 总开关已关闭");
        else if(s.bypass_reason!=0)ImGui::Text("核心旁路状态：%u（Home 查看原因）",s.bypass_reason);
        else ImGui::TextUnformatted("核心可处理；提交数见下方");
        ImGui::Text("NR 命令提交累计：%llu",static_cast<unsigned long long>(s.submitted_count));
    }
}
void draw_performance(View&v){
    ImGui::TextUnformatted("性能观测");ImGui::Separator();
    const float fps=v.history.count?v.history.fps():-1;
    metric("观测 FPS · ReShade 回调",fps,"%.1f FPS");ImGui::SameLine(195);
    metric("平均回调间隔",v.history.count?v.history.mean_ms():-1,"%.2f ms");
    const char* charts[]={"帧率曲线（观测）","帧时间曲线（回调间隔）"};
    ImGui::Combo("##chart",&v.chart,charts,2);
    auto data=v.history.ordered();float ceiling=v.chart==0?60.f:33.4f;
    for(size_t i=0;i<v.history.count;++i){if(v.chart==0)data[i]=data[i]>0?1000.f/data[i]:0;ceiling=std::max(ceiling,data[i]);}
    if(v.history.count)ImGui::PlotLines("##cadence",data.data(),static_cast<int>(v.history.count),0,v.chart==0?"FPS":"ms",0,ceiling,ImVec2(-1,75));
    else ImGui::TextDisabled("等待前台帧样本…");
    ImGui::TextDisabled("最近 %u 次回调；不是显示/插帧 FPS",static_cast<unsigned>(v.history.count));
    ImGui::Text("输出缓冲：%u × %u",v.width,v.height);
    ImGui::Separator();
    auto&g=app->gpu;
    if(g.names.size()>1){const char*cur=g.selected>=0&&size_t(g.selected)<g.names.size()?g.names[g.selected].c_str():"选择监测显卡";
        if(ImGui::BeginCombo("传感器设备",cur)){for(size_t i=0;i<g.names.size();++i){std::string label=std::to_string(i)+": "+g.names[i];if(ImGui::Selectable(label.c_str(),g.selected==int(i)))app->sensors.select(int(i));}ImGui::EndCombo();}}
    else ImGui::TextWrapped("%s",g.names.empty()?g.reason.c_str():g.names.front().c_str());
    const bool fresh=g.reading.ready&&GetTickCount64()-g.reading.sample_tick<3500;
    metric("GPU 占用（设备总体）",fresh?g.reading.utilization:-1,"%.0f %%");ImGui::SameLine(195);
    metric("显卡功耗 · 驱动读数",fresh?g.reading.power_w:-1,"%.1f W");
    metric("GPU 温度",fresh?g.reading.temperature_c:-1,"%.0f °C");ImGui::SameLine(195);
    metric("已用显存（设备总体）",fresh?g.reading.memory_mib/1024.f:-1,"%.2f GiB");
    ImGui::TextDisabled("NVML 约 1 Hz；不支持或过期显示 --");
}
void on_init(Runtime*r){if(!app)return;std::lock_guard<std::recursive_mutex>l(app->mutex);if(app->views.size()<16)app->views.emplace(r,View{});}
void on_destroy(Runtime*r){if(!app)return;std::lock_guard<std::recursive_mutex>l(app->mutex);app->views.erase(r);if(app->primary==r)app->primary=nullptr;}
void on_present(Runtime*r){if(!app)return;std::lock_guard<std::recursive_mutex>l(app->mutex);auto it=app->views.find(r);if(it==app->views.end())return;
    it->second.history.observe(now_seconds(),foreground(r));it->second.touched=GetTickCount64();}
bool on_builtin(Runtime*r,bool open,reshade::api::input_source){if(!app)return false;std::lock_guard<std::recursive_mutex>l(app->mutex);auto it=app->views.find(r);if(it!=app->views.end()){it->second.builtin=open;if(open)it->second.open=false;}return false;}
void on_overlay(Runtime*r){
    if(!app)return;std::lock_guard<std::recursive_mutex>l(app->mutex);auto it=app->views.find(r);if(it==app->views.end())return;auto&v=it->second;
    r->get_screenshot_width_and_height(&v.width,&v.height);
    if(!foreground(r)){v.open=false;if(app->primary==r){app->sensors.request(false);if(!v.builtin)ImGui::GetIO().MouseDrawCursor=false;}return;}
    // Select one foreground runtime (largest recently presenting surface). Never merge streams.
    Runtime*best=r;uint64_t area=uint64_t(v.width)*v.height;
    for(auto&pair:app->views)if(pair.first!=r&&foreground(pair.first)&&GetTickCount64()-pair.second.touched<1500){auto n=uint64_t(pair.second.width)*pair.second.height;if(n>area){area=n;best=pair.first;}}
    app->primary=best;if(best!=r)return;
    if(r->is_key_pressed(app->key)&&!r->is_key_down(VK_CONTROL)&&!r->is_key_down(VK_MENU)&&!r->is_key_down(VK_SHIFT)){
        bool desired=!v.open;r->open_overlay(false,reshade::api::input_source::keyboard);v.open=desired;
    }
    if(v.open&&r->is_key_pressed(VK_ESCAPE))v.open=false;
    if(v.builtin)v.open=false;
    app->sensors.request(v.open||app->pinned);
    if(GetTickCount64()-app->sensor_tick>500){app->gpu=app->sensors.copy();app->sensor_tick=GetTickCount64();}
    if((v.open||app->pinned)&&GetTickCount64()-app->core_tick>100){app->core.read();app->core_tick=GetTickCount64();}
    if(!v.open){
        if(!v.builtin)ImGui::GetIO().MouseDrawCursor=false;
        if(!app->pinned)return;
        GlassStyle style(app->opacity);
        ImGui::SetNextWindowPos(ImVec2(24,24),ImGuiCond_FirstUseEver);
        const auto flags=ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings;
        if(ImGui::Begin("Quickbar 性能条###zjz-pin",nullptr,flags)){
            if(v.history.count)ImGui::Text("%.1f FPS  |  %.2f ms",v.history.fps(),v.history.mean_ms());else ImGui::TextUnformatted("FPS / ms：--");
            auto&g=app->gpu.reading;bool fresh=g.ready&&GetTickCount64()-g.sample_tick<3500;
            metric("GPU",fresh?g.utilization:-1,"%.0f %%");ImGui::SameLine();metric("功耗",fresh?g.power_w:-1,"%.1f W");
            ImGui::TextDisabled("ReShade 观测口径 · F%d 调节",app->key-VK_F1+1);
        }ImGui::End();return;
    }
    r->block_input_next_frame();ImGui::GetIO().MouseDrawCursor=true;
    GlassStyle style(app->opacity);
    ImGui::SetNextWindowSize(ImVec2(std::min(850.f,ImGui::GetIO().DisplaySize.x-32),std::min(580.f,ImGui::GetIO().DisplaySize.y-100)),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(36,70),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("DLSS 5 AI 超分 · 野生的装机宅###zjz-quickbar",&v.open,ImGuiWindowFlags_NoCollapse)){
        ImGui::TextDisabled("F6 总开关   F8 诊断抓帧   Home 原设置页");
        if(ImGui::Checkbox("固定性能条",&app->pinned))app->save();ImGui::SameLine();
        if(ImGui::Button("原始 ReShade / 高级")){v.open=false;r->open_overlay(true,reshade::api::input_source::mouse);}
        ImGui::Separator();
        if(ImGui::BeginTable("##layout",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)){
            ImGui::TableNextColumn();draw_controls();ImGui::TableNextColumn();draw_performance(v);ImGui::EndTable();}
        if(ImGui::CollapsingHeader("面板外观")){
            ImGui::SliderFloat("不透明度",&app->opacity,.6f,.98f,"%.2f");
            ImGui::SliderFloat("文字缩放",&app->font_scale,.85f,1.5f,"%.2f");
            int key=app->key-VK_F9;const char*keys[]={"F9","F10","F11"};if(ImGui::Combo("面板快捷键",&key,keys,3))app->key=VK_F9+key;
            if(ImGui::Button("保存外观"))app->save();
            ImGui::TextDisabled("半透明材质；没有全屏背景模糊 pass");
        }
        if(!app->feedback.empty())ImGui::TextWrapped("%s",app->feedback.c_str());
    }ImGui::End();
}
void draw_fallback(Runtime*){ImGui::Text("F%d 打开 Quickbar；Home 保留原始设置页。",app?app->key-VK_F1+1:10);}
}
extern "C" __declspec(dllexport) bool AddonInit(HMODULE self,HMODULE host){
    if(!reshade::register_addon(self,host))return false;
    try{
        app=new App;wchar_t file[32768]{};GetModuleFileNameW(self,file,32768);app->preferences=file;
        const auto slash=app->preferences.find_last_of(L"\\/");app->preferences.resize(slash==std::wstring::npos?0:slash+1);app->preferences+=L"nr_quickbar.ini";app->load();
        reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy);
        reshade::register_event<reshade::addon_event::reshade_present>(on_present);
        reshade::register_event<reshade::addon_event::reshade_overlay>(on_overlay);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(on_builtin);
        reshade::register_overlay(nullptr,draw_fallback);return true;
    }catch(...){delete app;app=nullptr;reshade::unregister_addon(self,host);return false;}
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE self,HMODULE host){
    reshade::unregister_addon(self,host);delete app;app=nullptr;
}
BOOL APIENTRY DllMain(HMODULE,DWORD,LPVOID){return TRUE;}
