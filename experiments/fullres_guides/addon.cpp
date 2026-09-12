// Optional ReShade input probe alongside the owner's D14 Core. Never creates NR.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define ImTextureID ImU64
#include <windows.h>
#include <imgui.h>
#include <reshade.hpp>
#include <wrl/client.h>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include "backend.hpp"

using namespace nr::fullres;
using Microsoft::WRL::ComPtr;
namespace api=reshade::api;
namespace {
constexpr char effect[]="NR_FullResGuides.fx";
HMODULE self=nullptr;
std::mutex state_mutex;
struct State {
    bool enabled=false, sdr_confirmed=false, unavailable=false, captured_this_effects=false;
    int method=1, preview=0, scale_percent=100;
    uint64_t stream=0,epoch=1,frame=0,signal=0,shown=0,reference=0;
    bool depth_binding=false;
    uint32_t depth_width=0,depth_height=0,grid=0;
    ComPtr<ID3D12Fence> producer;
    ComPtr<ID3D12CommandQueue> producer_queue;
    std::unique_ptr<Session> session;
    std::string status="Disabled. This is an input probe, not an NR compatibility implementation.";
};
std::map<api::effect_runtime*,std::unique_ptr<State>> states;
uint64_t next_stream=0;
State& state(api::effect_runtime* r) {
    auto& p=states[r]; if(!p){p=std::make_unique<State>();p->stream=++next_stream;} return *p;
}
void report(State& s,const char* why) {
    s.status=why;s.unavailable=true;
    reshade::log::message(reshade::log::level::warning,why);
    reshade::log::message(reshade::log::level::warning,
        "FullRes Guides bypass: save ReShade.log, selected backend and input dimensions; submit a BUG report. Original Core/SR remain unchanged.");
}
void set_int(api::effect_runtime* r,const char* key,int value) {
    const auto v=r->find_uniform_variable(effect,key);
    if(v.handle)r->set_uniform_value_int(v,value);
}
void options(api::effect_runtime* r,State& s) {
    const auto capture=r->find_technique(effect,"NRGuideCapture");
    const auto preview=r->find_technique(effect,"NRGuidePreview");
    const bool active=capture_enabled(s.enabled,s.sdr_confirmed,s.unavailable);
    if(capture.handle && r->get_technique_state(capture)!=active)r->set_technique_state(capture,active);
    const bool show=active && s.preview!=0;
    if(preview.handle && r->get_technique_state(preview)!=show)r->set_technique_state(preview,show);
    set_int(r,"NRGuideView",active?s.preview:0);
    set_int(r,"NRGuideReady",active && s.shown?1:0);
}
bool retire(State& s) {
    if(s.session && !s.session->safe_to_destroy())return false;
    s.session.reset();s.producer.Reset();s.producer_queue.Reset();
    s.shown=s.reference=s.signal=0;++s.epoch;return true;
}
void pin_module() {
    // A fault can intentionally retain SDK sessions/GPU resources until process exit.
    // Prevent unloading static FFX code while those objects may still depend on it.
    wchar_t path[32768]{};
    if(GetModuleFileNameW(self,path,32768)) {
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,path,&pinned);
    }
}
void overlay(api::effect_runtime* r) {
    std::unique_lock<std::mutex> lock(state_mutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    auto& s=state(r);
    ImGui::TextUnformatted("Full-resolution guide experiment / 输入实验");
    ImGui::TextWrapped("AMDOF / NVOF, 100% or 50% OF input. NR Color and Depth are not resized. This probe does NOT feed D14 NR yet.");
    bool history_changed=ImGui::Checkbox("Enable input probe (experimental)",&s.enabled);
    bool changed=history_changed;
    history_changed|=ImGui::Checkbox("SDR source, Core NR OFF and FG OFF (guide-only test)",&s.sdr_confirmed);
    changed|=history_changed;
    int choice=s.method-1;
    if(ImGui::Combo("Synthetic motion",&choice,"AMDOF (FidelityFX)\0NVOF (NVIDIA)\0")){s.method=choice+1;history_changed=true;changed=true;}
    int scale_choice=s.scale_percent==50?1:0;
    if(ImGui::Combo("OF input resolution",&scale_choice,"100% (reference)\0" "50% (performance experiment)\0")) {
        s.scale_percent=scale_choice==1?50:100; history_changed=true; changed=true;
    }
    changed|=ImGui::Combo("Preview",&s.preview,"Original\0Motion XY / magnitude\0Generic Depth (raw)\0");
    ImGui::TextWrapped("Native priority is preserved in the original Core. Enabling this standalone probe explicitly computes synthetic motion for comparison; it does not select the Core's guides.");
    ImGui::Text("Full input: %u x %u; motion grid: %u",
        s.session?s.session->extent().width:0,s.session?s.session->extent().height:0,s.grid);
    ImGui::Text("OF input: %u x %u; requested scale: %d%%",
        s.session?s.session->flow_extent().width:0,s.session?s.session->flow_extent().height:0,s.scale_percent);
    ImGui::Text("Last completed pair: %llu -> %llu; capture age: %llu",
        static_cast<unsigned long long>(s.shown),static_cast<unsigned long long>(s.reference),
        static_cast<unsigned long long>(s.shown?s.frame-s.shown:0));
    ImGui::Text("DEPTH binding: %s (%u x %u)",s.depth_binding?"present; geometry NOT confirmed":"unavailable/placeholder",s.depth_width,s.depth_height);
    ImGui::TextWrapped("%s",s.status.c_str());
    if(s.session)ImGui::TextWrapped("Provider: %s",s.session->status());
    ImGui::TextWrapped("A motion preview can be older than the current image. It is never presented as current-frame NR output. Native rendering, SR and FG are untouched.");
    if(s.unavailable)ImGui::TextWrapped("Bypassed: collect ReShade.log and report the backend, dimensions and failure. Do not keep toggling a failed session; restart the game.");
    if(history_changed){++s.epoch;s.shown=s.reference=0;s.grid=0;}
    if(changed)options(r,s);
}
api::resource binding(api::effect_runtime* r,const char* name) {
    const auto t=r->find_texture_variable(effect,name);
    if(!t.handle)return {};
    api::resource_view view{},srgb{};r->get_texture_binding(t,&view,&srgb);
    return view.handle?r->get_device()->get_resource_from_view(view):api::resource{};
}
bool plane(api::device* d,api::resource resource,Extent e,api::format format) {
    if(!resource.handle)return false;
    const auto desc=d->get_resource_desc(resource);
    return desc.type==api::resource_type::texture_2d && desc.texture.width==e.width &&
        desc.texture.height==e.height && desc.texture.depth_or_layers==1 &&
        desc.texture.levels==1 && desc.texture.samples==1 && desc.texture.format==format;
}
void rendered(api::effect_runtime* r,api::effect_technique tech,api::command_list* commands,
              api::resource_view,api::resource_view) {
    std::unique_lock<std::mutex> lock(state_mutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    auto& s=state(r);
    if(!s.enabled || !s.sdr_confirmed || s.unavailable)return;
    if(tech.handle==r->find_technique(effect,"NRGuidePreview").handle) {
        if(!s.captured_this_effects){report(s,"Preview preceded capture: restore capture-before-preview order, then restart the test.");options(r,s);}
        return;
    }
    if(tech.handle!=r->find_technique(effect,"NRGuideCapture").handle)return;
    if(s.captured_this_effects)return;
    s.captured_this_effects=true;
    set_int(r,"NRGuideCaptured",1);
    ++s.frame;
    try {
        auto* device=r->get_device();
        if(device->get_api()!=api::device_api::d3d12){report(s,"This first probe supports x64 D3D12 only; other API transports are not connected.");options(r,s);return;}
        auto* queue=r->get_command_queue();
        if(!queue || !commands || queue->get_immediate_command_list()!=commands) {
            report(s,"Capture is not on the known immediate effect list; refusing to guess queue provenance.");options(r,s);return;
        }
        Extent e{};r->get_screenshot_width_and_height(&e.width,&e.height);
        const auto color=binding(r,"NRGuideColor");
        const auto flow=binding(r,"NRGuideFlow");
        const auto depth=binding(r,"NRGuideDepthInput");
        s.depth_binding=false;s.depth_width=s.depth_height=0;
        if(depth.handle){const auto dd=device->get_resource_desc(depth);s.depth_width=dd.texture.width;s.depth_height=dd.texture.height;
            s.depth_binding=dd.type==api::resource_type::texture_2d && dd.texture.width>1 && dd.texture.height>1;}
        set_int(r,"NRGuideDepthAvailable",s.depth_binding?1:0);
        if(!plane(device,color,e,api::format::r8g8b8a8_unorm) || !plane(device,flow,e,api::format::r32g32_float)) {
            report(s,"Full-size capture/flow effect textures are unavailable or incompatible. Reload the matching FX source.");options(r,s);return;
        }
        auto* native_queue=reinterpret_cast<ID3D12CommandQueue*>(queue->get_native());
        const auto backend=static_cast<Backend>(s.method);
        if(s.session) {
            s.session->poll();
            if(s.session->faulted()){report(s,s.session->status());options(r,s);return;}
        }
        if(s.session && (!(s.session->extent()==e) || s.session->backend()!=backend || s.session->scale()!=static_cast<FlowScale>(s.scale_percent) || s.producer_queue.Get()!=native_queue)) {
            set_int(r,"NRGuideReady",0);
            if(!retire(s)){s.status="Draining old exact queue/fences before backend/scale/extent change; no replacement yet.";return;}
        }
        if(!s.session) {
            pin_module();
            auto* native_device=reinterpret_cast<ID3D12Device*>(device->get_native());
            s.session=std::make_unique<Session>(native_device,backend,e,static_cast<FlowScale>(s.scale_percent));
            if(!s.session->initialized()){report(s,s.session->status());options(r,s);return;}
            const auto h=native_device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.producer));
            if(FAILED(h)){report(s,"Source fence could not be created.");options(r,s);return;}
            s.producer_queue=native_queue;
        }
        if(s.session->faulted()){report(s,s.session->status());options(r,s);return;}
        if(!s.session->idle()){s.status="OF in flight: skipping new captures, not reusing resources or changing the reference.";return;}
        // Mark BEFORE recording any command referring to private session resources.
        s.session->mark_capture_recorded();
        Result result{};
        const Frame view{s.stream,s.epoch,s.frame,e,false};
        if(s.session->result(view,result) && result.current!=s.shown) {
            commands->barrier(flow,api::resource_usage::shader_resource,api::resource_usage::copy_dest);
            commands->copy_resource(api::resource{reinterpret_cast<uint64_t>(result.flow)},flow);
            commands->barrier(flow,api::resource_usage::copy_dest,api::resource_usage::shader_resource);
            s.shown=result.current;s.reference=result.reference;s.grid=result.grid;
        }
        commands->barrier(color,api::resource_usage::shader_resource,api::resource_usage::copy_source);
        commands->copy_resource(color,api::resource{reinterpret_cast<uint64_t>(s.session->snapshot())});
        commands->barrier(color,api::resource_usage::copy_source,api::resource_usage::shader_resource);
        queue->flush_immediate_command_list();
        if(s.signal>=UINT64_MAX-1 || FAILED(s.producer_queue->Signal(s.producer.Get(),++s.signal))) {
            s.session->poison("Game-source Signal failed after recorded capture; resources retained.");
            report(s,s.session->status());options(r,s);return;
        }
        Frame frame{s.stream,s.epoch,s.frame,e,false};
        if(!s.session->submit(s.producer.Get(),s.signal,frame)) {
            if(!s.session->faulted())s.session->poison("Capture was not accepted by the selected OF provider.");
            report(s,s.session->status());options(r,s);return;
        }
        set_int(r,"NRGuideReady",s.shown?1:0);
        s.status="Capture submitted at the selected OF scale. Preview contains the last completed pair; NR integration still pending.";
    }catch(const std::exception& e){if(s.session)s.session->poison(e.what());report(s,e.what());options(r,s);}
    catch(...){if(s.session)s.session->poison("Unexpected capture error");report(s,"Unexpected capture error; experiment stopped.");options(r,s);}
}
void begin_effects(api::effect_runtime* r,api::command_list*,api::resource_view,api::resource_view) {
    std::lock_guard<std::mutex> lock(state_mutex);
    auto& s=state(r);s.captured_this_effects=false;
    // A preset/manual technique toggle must not bypass the default-off consent.
    options(r,s);
    set_int(r,"NRGuideCaptured",0);
    if(!capture_enabled(s.enabled,s.sdr_confirmed,s.unavailable))set_int(r,"NRGuideReady",0);
}
void reloaded(api::effect_runtime* r) {
    std::lock_guard<std::mutex> lock(state_mutex);
    auto it=states.find(r);if(it==states.end())return;
    auto& s=*it->second;++s.epoch;s.shown=0;
    // Handles are looked up per callback; no old effect resource is retained.
    options(r,s);
}
void destroyed(api::effect_runtime* r) {
    std::lock_guard<std::mutex> lock(state_mutex);
    states.erase(r); // Session destructor quarantines unfinished work rather than freeing it.
}
}
extern "C" __declspec(dllexport) const char *NAME="NR FullRes Guides (unverified experiment)";
extern "C" __declspec(dllexport) const char *DESCRIPTION="D14 companion input probe: AMD/NVIDIA 100% or 50% optical flow and Generic Depth preview. No NR integration.";
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self=module;
        if(!reshade::register_addon(module))return FALSE;
        reshade::register_overlay("NR FullRes Guides (experimental)",overlay);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
        reshade::register_event<reshade::addon_event::reshade_render_technique>(rendered);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(reloaded);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(destroyed);
    }else if(reason==DLL_PROCESS_DETACH)reshade::unregister_addon(module);
    return TRUE;
}
