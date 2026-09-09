#include "nr_post_boundary.h"
#define NR_HAS_POST_BOUNDARY_TRACE 1
extern thread_local bool g_inside_ngx_evaluate;

SRWLOCK g_post_boundary_lock=SRWLOCK_INIT;
std::atomic<std::uint64_t> g_post_boundary_resource{0};
std::atomic<std::uint64_t> g_post_boundary_device{0};
nr_post_boundary::Window g_post_boundary_current,g_post_boundary_presented;

struct NrNativeOutputTraceScope;
thread_local NrNativeOutputTraceScope *g_native_output_trace=nullptr;
struct NrNativeOutputTraceScope {
    NrNativeOutputTraceScope *previous=nullptr;
    std::uint64_t resource=0,native_list=0;
    bool active=false;
    nr_post_boundary::Native evidence;
    NrNativeOutputTraceScope(ID3D12Resource *output,ID3D12GraphicsCommandList *list) {
        if(!output||!list)return;
        resource=reinterpret_cast<std::uint64_t>(output);
        native_list=reinterpret_cast<std::uint64_t>(list);
        previous=g_native_output_trace;g_native_output_trace=this;active=true;
    }
    nr_post_boundary::Native Finish() {
        if(active){g_native_output_trace=previous;active=false;}
        return evidence;
    }
    ~NrNativeOutputTraceScope(){Finish();}
};
void ObserveNativeOutputBarrier(std::uint64_t list,std::uint64_t resource,
        unsigned before,unsigned after) {
    if(g_native_output_trace)
        g_native_output_trace->evidence.Barrier(list==g_native_output_trace->native_list,
            resource==g_native_output_trace->resource,before,after);
}
void PublishPostBoundary(ID3D12Resource *resource,const nr_post_boundary::Native &native,
        const NrDiagnosticIdentity &identity,unsigned long long feature,std::uint64_t device=0) {
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    g_post_boundary_current={};
    g_post_boundary_current.frame=identity.frame_id;
    g_post_boundary_current.config=identity.config_generation;
    g_post_boundary_current.feature=feature;
    g_post_boundary_current.native=native;
    g_post_boundary_current.tick=GetTickCount64();
    g_post_boundary_device.store(device,std::memory_order_release);
    g_post_boundary_resource.store(reinterpret_cast<std::uint64_t>(resource),std::memory_order_release);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void ForgetPostBoundary(std::uint64_t resource) {
    if(!resource||g_post_boundary_resource.load(std::memory_order_acquire)!=resource)return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_resource.load(std::memory_order_relaxed)==resource){
        g_post_boundary_resource.store(0,std::memory_order_release);
        g_post_boundary_device.store(0,std::memory_order_release);g_post_boundary_current={};
    }
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void ObserveFollowingOutputBarrier(std::uint64_t resource,unsigned before,unsigned after) {
    if(!resource||g_post_boundary_resource.load(std::memory_order_acquire)!=resource)return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_resource.load(std::memory_order_relaxed)==resource)
        g_post_boundary_current.Barrier(before,after);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
void OnPostBoundaryBarrier(reshade::api::command_list *commands,std::uint32_t count,
        const reshade::api::resource *resources,const reshade::api::resource_usage *before,
        const reshade::api::resource_usage *after) {
    if(!resources||!before||!after)return;
    if(g_native_output_trace) {
        const auto native=commands?commands->get_native():0;
        for(std::uint32_t i=0;i<count;++i)
            ObserveNativeOutputBarrier(native,resources[i].handle,
                static_cast<unsigned>(before[i]),static_cast<unsigned>(after[i]));
    } else if(!g_inside_ngx_evaluate && g_post_boundary_resource.load(std::memory_order_acquire)) {
        for(std::uint32_t i=0;i<count;++i)
            ObserveFollowingOutputBarrier(resources[i].handle,
                static_cast<unsigned>(before[i]),static_cast<unsigned>(after[i]));
    }
}
void ObservePostBoundaryCopy(std::uint64_t source,std::uint64_t destination) {
    if(g_inside_ngx_evaluate)return;
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(!target||(target!=source&&target!=destination))return;
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_resource.load(std::memory_order_relaxed)==target)
        g_post_boundary_current.Copy(source==target,destination==target);
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
bool OnPostBoundaryCopy(reshade::api::command_list *,reshade::api::resource source,reshade::api::resource destination) {
    ObservePostBoundaryCopy(source.handle,destination.handle);return false;
}
bool OnPostBoundaryCopyRegion(reshade::api::command_list *,reshade::api::resource source,std::uint32_t,
        const reshade::api::subresource_box *,reshade::api::resource destination,std::uint32_t,
        const reshade::api::subresource_box *,reshade::api::filter_mode) {
    ObservePostBoundaryCopy(source.handle,destination.handle);return false;
}
void OnPostBoundaryRenderTargets(reshade::api::command_list *commands,std::uint32_t count,
        const reshade::api::resource_view *views,reshade::api::resource_view) {
    if(g_inside_ngx_evaluate||!commands||!views)return;
    const auto target=g_post_boundary_resource.load(std::memory_order_acquire);
    if(!target)return;
    for(std::uint32_t i=0;i<std::min(count,8u);++i) {
        if(!views[i].handle||commands->get_device()->get_resource_from_view(views[i]).handle!=target)continue;
        AcquireSRWLockExclusive(&g_post_boundary_lock);
        if(g_post_boundary_resource.load(std::memory_order_relaxed)==target)
            nr_post_boundary::Increment(g_post_boundary_current.render_target_binds);
        ReleaseSRWLockExclusive(&g_post_boundary_lock);
    }
}
void OnPostBoundaryDestroyResource(reshade::api::device *,reshade::api::resource resource) {
    ForgetPostBoundary(resource.handle);
}
void ClosePostBoundaryAtPresent() {
    AcquireSRWLockExclusive(&g_post_boundary_lock);
    if(g_post_boundary_current.frame)g_post_boundary_presented=g_post_boundary_current;
    ReleaseSRWLockExclusive(&g_post_boundary_lock);
}
nr_post_boundary::Window SnapshotPostBoundary() {
    AcquireSRWLockShared(&g_post_boundary_lock);
    const auto result=g_post_boundary_presented;
    ReleaseSRWLockShared(&g_post_boundary_lock);return result;
}
void RegisterPostBoundaryEvents(bool add) {
    if(add) {
        reshade::register_event<reshade::addon_event::barrier>(OnPostBoundaryBarrier);
        reshade::register_event<reshade::addon_event::copy_resource>(OnPostBoundaryCopy);
        reshade::register_event<reshade::addon_event::copy_texture_region>(OnPostBoundaryCopyRegion);
        reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnPostBoundaryRenderTargets);
        reshade::register_event<reshade::addon_event::destroy_resource>(OnPostBoundaryDestroyResource);
    } else {
        reshade::unregister_event<reshade::addon_event::barrier>(OnPostBoundaryBarrier);
        reshade::unregister_event<reshade::addon_event::copy_resource>(OnPostBoundaryCopy);
        reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnPostBoundaryCopyRegion);
        reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnPostBoundaryRenderTargets);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(OnPostBoundaryDestroyResource);
    }
}
