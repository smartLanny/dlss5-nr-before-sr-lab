// Controlled combined experiment; no game callback or Present simulation.
// Build with the frozen B fixture's tests directory on the include path.
#define wmain unused_direct_sr_wmain
#include <direct_sr_gpu.cpp>
#undef wmain
#include "src/interop/nr_external_provider_producer.h"

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, unsigned version) { return version == 18; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) {}
extern "C" __declspec(dllexport) const void *ReShadeGetImGuiFunctionTable(unsigned) {
    static const void *unused_table[1024]{}; // Never called; registration stub only.
    return unused_table;
}

namespace {
void RequireNgx(NVSDK_NGX_Result result, const char *stage) {
    std::cout << stage << "=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
    if (NVSDK_NGX_FAILED(result)) throw std::runtime_error(std::string(stage) + " rejected");
}
void TransitionCombined(DeviceContext &ctx, ID3D12Resource *resource,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=resource;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore=before;b.Transition.StateAfter=after;
    ctx.list->ResourceBarrier(1,&b);
}
NrExternalResourceV1 NrResource(ID3D12Resource *resource, D3D12_RESOURCE_STATES state) {
    const auto desc=resource->GetDesc(); NrExternalResourceV1 r{};
    r.resource=reinterpret_cast<std::uint64_t>(resource);r.format=desc.Format;
    r.width=r.rect_w=static_cast<unsigned>(desc.Width);r.height=r.rect_h=desc.Height;
    r.state_before=r.state_after=state;r.provenance=NrGuideProvenance::Synthetic;
    return r;
}
}

int wmain(int argc,wchar_t **argv) {
    if(argc!=4){std::cerr<<"usage: combined_nr_sr_gpu <core.dll> <runtime-dir> <off|on>\n";return 1;}
    const bool nr_on=wcscmp(argv[3],L"on")==0;
    if(!nr_on && wcscmp(argv[3],L"off")!=0)return 1;
    NVSDK_NGX_Parameter *caps=nullptr,*params=nullptr;NVSDK_NGX_Handle *sr=nullptr;
    nr_external_provider_client::QueryFn query=nullptr;
    nr_external_provider_client::ReleaseFn release=nullptr;
    bool owner=false,initialized=false;
    try {
        auto ctx=MakeDevice(false);auto frame=MakeFrame(ctx,true);BindFrame(frame,ctx);
        frame.dispatch.jitterOffset={0,0}; // This renderer's input is static and unjittered.
        const std::wstring runtime_dir=argv[2];
        const wchar_t *paths[]={runtime_dir.c_str()};NVSDK_NGX_FeatureCommonInfo common{};
        common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
        const std::wstring data_dir=runtime_dir+L"\\ngx-data";CreateDirectoryW(data_dir.c_str(),nullptr);
        RequireNgx(NVSDK_NGX_D3D12_Init_with_ProjectID("7a94da60-f8c9-4b8b-9d8a-22f0293c4801",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM,"combined-nr-sr-controlled-host-1",data_dir.c_str(),
            ctx.device.Get(),&common,NVSDK_NGX_Version_API),"official_ngx_init");
        initialized=true;
        RequireNgx(NVSDK_NGX_D3D12_GetCapabilityParameters(&caps),"ngx_capabilities");
        nr_fsr31_dlss::Quality quality{};
        if(nr_fsr31_dlss::SelectQuality(caps,frame.create,frame.dispatch,quality)!=nr_fsr31_dlss::Status::Ready)
            throw std::runtime_error("no real SR quality covers renderer contract");
        nr_fsr31_dlss::Mapping mapping{};
        if(nr_fsr31_dlss::Map(ctx.device.Get(),frame.create,frame.dispatch,mapping,quality)!=nr_fsr31_dlss::Status::Ready)
            throw std::runtime_error("B production map rejected renderer contract");

        // Init/capabilities may load additional runtime modules. Observe all of
        // them before creating Feature1; Claim intentionally precedes SR Create.
        HMODULE core=LoadLibraryW(argv[1]);
        if(!core)throw std::runtime_error("production core DLL registration failed");
        query=reinterpret_cast<nr_external_provider_client::QueryFn>(GetProcAddress(core,"NRExternalProvider_QueryV1"));
        auto claim=reinterpret_cast<nr_external_provider_client::ClaimFn>(GetProcAddress(core,"NRExternalProvider_ClaimOwnershipV1"));
        release=reinterpret_cast<nr_external_provider_client::ReleaseFn>(GetProcAddress(core,"NRExternalProvider_ReleaseOwnershipV1"));
        auto submit=reinterpret_cast<nr_external_provider_client::SubmitFn>(GetProcAddress(core,"NRExternalProvider_SubmitFrameV1"));
        if(!query||!claim||!release||!submit)throw std::runtime_error("four production provider exports absent");
        NrExternalStatusV1 status{};
        if (!nr_external_provider_client::WaitForNativeObserver(query, 3000, status))
            throw std::runtime_error(std::string("native observer preflight rejected: ") +
                                     NrExternalProviderReasonName(status.reason));
        std::cout<<"observer_before_claim="<<NrExternalProviderReasonName(status.reason)<<" epoch="<<status.observer_epoch<<'\n';
        if(!claim(kNrExternalProviderAbi,sizeof(status),kNrExternalProviderIdDl5f,&status))
            throw std::runtime_error(std::string("provider claim rejected: ")+NrExternalProviderReasonName(status.reason));
        owner=true;
        if((status.reason==NrExternalProviderReason::Ready)!=nr_on)
            throw std::runtime_error("NR fixture config differs from requested scenario");
        RequireNgx(NVSDK_NGX_D3D12_AllocateParameters(&params),"sr_allocate_parameters");
        const auto created=NGX_D3D12_CREATE_DLSS_EXT(ctx.list.Get(),1,1,&sr,params,&mapping.create);
        ctx.SubmitAndWait();ctx.Reset();
        RequireNgx(created,"sr_create_fence_completed");
        if(!sr)throw std::runtime_error("SR Create returned no handle");
        auto nr_output=Texture(ctx.device.Get(),kRenderWidth,kRenderHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        NrExternalFrameV1 nr{};
        nr.provider_id=kNrExternalProviderIdDl5f;nr.view_id=1;nr.epoch=1;
        nr.command_list=reinterpret_cast<std::uint64_t>(ctx.list.Get());
        nr.queue=reinterpret_cast<std::uint64_t>(ctx.queue.Get());nr.fence=reinterpret_cast<std::uint64_t>(ctx.fence.Get());
        nr.lease_flags=static_cast<unsigned>(NrExternalLeaseFlags::CommandListOwnedByProducer)|
            static_cast<unsigned>(NrExternalLeaseFlags::QueueFenceRequired)|
            static_cast<unsigned>(NrExternalLeaseFlags::HdrColor)|static_cast<unsigned>(NrExternalLeaseFlags::DepthInverted);
        nr.color=NrResource(frame.color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        nr.output=NrResource(nr_output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        nr.depth=NrResource(frame.depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        nr.motion=NrResource(frame.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        nr.motion.motion_scale_x=frame.dispatch.motionVectorScale.x;
        nr.motion.motion_scale_y=frame.dispatch.motionVectorScale.y;
        nr.pre_exposure=frame.dispatch.preExposure;nr.exposure_scale=1;
        unsigned nr_frames=0,sr_frames=0;
        for(unsigned i=0;i<8;++i){
            nr.frame_id=i+1;nr.fence_value=ctx.next_fence-1;
            NrExternalSubmitResultV1 nr_result{};
            const int accepted=submit(kNrExternalProviderAbi,&nr,&nr_result);
            std::cout<<"frame="<<i<<" nr_submit="<<accepted<<" nr_reason="<<NrExternalProviderReasonName(nr_result.reason)
                <<" nr_recorded="<<nr_result.writeback_performed<<'\n';
            if(!nr_on && (nr_result.reason!=NrExternalProviderReason::Disabled||nr_result.writeback_performed))
                throw std::runtime_error("NR-off failed to retain the SR-only input path");
            const bool transformed=nr_result.writeback_performed!=0;
            if(transformed)TransitionCombined(ctx,nr_output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            frame.dispatch.color=View(transformed?nr_output.Get():frame.color.Get(),FFX_API_RESOURCE_STATE_COMPUTE_READ);
            frame.dispatch.reset=i==0;
            if(nr_fsr31_dlss::Map(ctx.device.Get(),frame.create,frame.dispatch,mapping,quality)!=nr_fsr31_dlss::Status::Ready)
                throw std::runtime_error("B production map rejected actual NR/SR input resource");
            const auto evaluated=NGX_D3D12_EVALUATE_DLSS_EXT(ctx.list.Get(),sr,params,&mapping.evaluate);
            if(transformed)TransitionCombined(ctx,nr_output.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // Even a failed backend may have recorded commands. Complete this
            // submitted batch before reporting failure or releasing any input.
            ctx.SubmitAndWait();ctx.Reset();
            RequireNgx(evaluated,"sr_evaluate_combined_batch_completed");
            ++sr_frames;nr_frames+=transformed;
            const auto pixels=ReadOutput(ctx,frame.output.Get());ctx.Reset();
            if(pixels.nonfinite||pixels.finite!=size_t(kOutputWidth)*kOutputHeight*4)
                throw std::runtime_error("combined output is not fully finite");
            if(pixels.rgb_maximum-pixels.rgb_minimum<=0.05f)
                throw std::runtime_error("combined RGB output lacks spatial variation");
            if(!query(kNrExternalProviderAbi,sizeof(status),0,&status)||status.owner_provider_id!=kNrExternalProviderIdDl5f)
                throw std::runtime_error("provider ownership was lost during SR Evaluate");
            std::cout<<"frame="<<i<<" input="<<(transformed?"project-NR":"original")<<" owner="<<status.owner_provider_id
                <<" total_external_recorded="<<status.writeback_frames<<" output_hash="<<pixels.hash
                <<" finite="<<pixels.finite<<" rgb_min="<<pixels.rgb_minimum<<" rgb_max="<<pixels.rgb_maximum<<'\n';
        }
        if(nr_on&&!nr_frames)throw std::runtime_error("no combined NR-to-SR frame completed; preserve rejection evidence");
        RequireNgx(NVSDK_NGX_D3D12_ReleaseFeature(sr),"sr_release");sr=nullptr;
        if(!release(kNrExternalProviderAbi,kNrExternalProviderIdDl5f))throw std::runtime_error("provider release failed");owner=false;
        RequireNgx(NVSDK_NGX_D3D12_DestroyParameters(params),"sr_destroy_parameters");params=nullptr;
        RequireNgx(NVSDK_NGX_D3D12_DestroyParameters(caps),"destroy_capabilities");caps=nullptr;
        RequireNgx(NVSDK_NGX_D3D12_Shutdown1(ctx.device.Get()),"official_ngx_shutdown");initialized=false;
        std::cout<<"combined_result=completed nr_enabled="<<nr_on<<" NR_frames="<<nr_frames<<" SR_frames="<<sr_frames
            <<" R=640x360 N=640x360 O=1280x720 host=registration-stub guides=synthetic game=false FG=false\n";
        return 0;
    }catch(const std::exception &e){
        // The controlled test terminates on a refused ownership/SDK boundary.
        // Keep the production module alive; do not invent a cleanup or handle.
        std::cerr<<"combined_result=rejected reason="<<e.what()<<" owner="<<owner<<" initialized="<<initialized<<'\n';
        std::cerr.flush();std::cout.flush();ExitProcess(2);
    }
}
