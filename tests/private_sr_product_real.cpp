// Included after the unmodified generated Core and the existing real-device Host.
// Synthetic constant scene, native Feature1, zero NR edit. No Feature18 or game.
#include <chrono>
#include <fstream>
namespace product_real {
using namespace sr_host;
NrContext context;
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* p){ID3D12CommandQueue_ExecuteCommandLists(q,n,p);}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* l,ID3D12CommandAllocator* a,ID3D12PipelineState* p){return ID3D12GraphicsCommandList_Reset(l,a,p);}
double Millis(std::chrono::steady_clock::time_point t){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
void Register(Host& h){
    CommandStateMetaContract meta;meta.device_identity=g_next_command_device_identity.fetch_add(1);
    CommandCreationInfo creation;creation.serial=1;creation.device_identity=meta.device_identity;creation.method=1;
    // Only object identity is registered here; no command-state restoration
    // capability is fabricated. This controlled host owns all its bindings.
    Require(RegisterCreatedCommandStateList(h.list,meta,creation),"real native list identity");
    g_original_queue_execute[0]=Execute;g_original_command_list_reset[0]=Reset;
}
void ResetAndClose(Host& h){
    Hr(ID3D12CommandAllocator_Reset(h.allocator),"completed allocator reset");
    Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"production successful Reset callback");
    Hr(ID3D12GraphicsCommandList_Close(h.list),"retired close");h.released=true;
}
void Complete(Host& h,LifecycleQueue* q){
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"fence event");
    Hr(ID3D12Fence_SetEventOnCompletion(q->fence,context.completion_fence_value,event),"exact Core fence event");
    Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"bounded Core completion");CloseHandle(event);
    Require(ID3D12Fence_GetCompletedValue(q->fence)!=UINT64_MAX,"no removed fence");
}
void Copy(Host& h,ID3D12Resource* from,ID3D12Resource* to,D3D12_RESOURCE_STATES target){
    h.Barrier(from,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    h.Barrier(to,target,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(h.list,to,from);
    h.Barrier(from,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    h.Barrier(to,D3D12_RESOURCE_STATE_COPY_DEST,target);
}
std::vector<char> Bytes(Host& h,ID3D12Resource* texture,D3D12_RESOURCE_STATES state){
    const auto desc=ID3D12Resource_GetDesc(texture);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(h.device,&desc,0,1,0,&footprint,nullptr,nullptr,&size);
    auto* readback=h.Buffer(size,D3D12_HEAP_TYPE_READBACK);h.Begin();h.Barrier(texture,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);
    h.Barrier(texture,D3D12_RESOURCE_STATE_COPY_SOURCE,state);h.Submit();h.Wait();
    char* mapped=nullptr;Hr(ID3D12Resource_Map(readback,0,nullptr,(void**)&mapped),"byte oracle map");std::vector<char> data(size_t(desc.Width)*desc.Height*8);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(data.data()+y*size_t(desc.Width)*8,mapped+footprint.Offset+y*footprint.Footprint.RowPitch,size_t(desc.Width)*8);
    ID3D12Resource_Unmap(readback,0,nullptr);return data;
}
int Run(const wchar_t* bridge){
    InitializeCriticalSection(&g_chain_cs);Host h;Register(h);
    HMODULE module=LoadLibraryW(bridge);Require(module!=nullptr,"existing verified bootstrap library");
    auto bootstrap=reinterpret_cast<unsigned(*)(ID3D12Device*)>(GetProcAddress(module,"NRChain_Bootstrap"));
    Require(bootstrap!=nullptr,"bootstrap entry");Ngx(bootstrap(h.device),"host's sole NGX bootstrap");
    HMODULE ngx=GetModuleHandleW(L"_nvngx.dll");Require(ngx!=nullptr,"native NGX loader");
    auto alloc=reinterpret_cast<nr_private_sr_core::Allocate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_AllocateParameters"));
    auto create=reinterpret_cast<nr_private_sr_core::Create>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));
    auto evaluate=reinterpret_cast<nr_private_sr_core::Evaluate>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_EvaluateFeature"));
    auto release=reinterpret_cast<nr_private_sr_core::Release>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));
    auto destroy=reinterpret_cast<nr_private_sr_core::Destroy>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));
    Require(alloc&&create&&evaluate&&release&&destroy,"all original exports");
    NgxParams* params=nullptr;Ngx(alloc(&params),"parent parameters");
    constexpr unsigned rw=320,rh=192,ow=640,oh=384,nw=320,nh=192;
    for(auto v:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u},
                {"DLSS.Render.Subrect.Dimensions.Width",rw},{"DLSS.Render.Subrect.Dimensions.Height",rh}})SetUInt(params,v.first,v.second);
    SetInt(params,"PerfQualityValue",0);SetInt(params,"DLSS.Feature.Create.Flags",2);
    NgxHandle* parent=nullptr;h.Begin();Ngx(create(h.list,1,params,&parent),"native parent Create");h.Submit();h.Wait();
    RememberFeature(parent,1,params);const auto feature=FindFeature(parent);Require(feature.generation&&feature.create_flags_known,"observed parent contract");
    g_codec.device=h.device;g_config.enabled=true;g_enabled_fast.store(true);g_config.private_residual_sr=true;g_config.temporal_backend=0;
    g_config.processing_start=nr_processing::Start::After;nr_processing_runtime::Reconcile();
    auto* color=h.Plane(rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.5f);
    auto* depth=h.Plane(rw,rh,DXGI_FORMAT_R32_FLOAT,false);auto* motion=h.Plane(rw,rh,DXGI_FORMAT_R32G32_FLOAT,false);
    auto* native=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* base=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    auto* final=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(auto p:{std::pair{"Color",color},{"Depth",depth},{"MotionVectors",motion},{"Output",native}})SetResource(params,p.first,p.second);
    SetResource(params,"ExposureTexture",nullptr);
    for(const char* name:{"DLSS.Pre.Exposure","DLSS.Exposure.Scale","MV.Scale.X","MV.Scale.Y"})SetFloat(params,name,1);
    SetFloat(params,"Jitter.Offset.X",0);SetFloat(params,"Jitter.Offset.Y",0);SetInt(params,"Reset",1);
    for(const char* name:{"DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
        "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y","DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"})SetUInt(params,name,0);
    auto* queue=FindOrCreateLifecycleQueueLocked(h.queue);Require(queue&&queue->fence,"real Core queue owner");
    // Publish each queue observation together with a real empty execution.
    for(unsigned n=0;n<kRequiredStableQueueSubmissions;++n){Require(RememberLastSubmissionQueue(h.list,queue->identity),"queue observation");
        ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};Execute(h.queue,1,lists);}
    Hr(ID3D12CommandQueue_Signal(h.queue,h.fence,++h.value),"prime completion");h.Wait();
    context.instance_id=1;context.feature_generation=feature.generation;
    auto& slot=context.slots[0];slot.source=base;slot.result=final;slot.source_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    slot.result_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;slot.snapshots_native_output=true;
    slot.result_format=slot.result_uav_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    NrGuideBinding guides{};guides.depth=depth;guides.motion=motion;auto& sig=guides.signature;
    sig.depth_format=41;sig.motion_format=16;sig.depth_physical_width=sig.depth_width=rw;sig.depth_physical_height=sig.depth_height=rh;
    sig.motion_physical_width=sig.motion_source_width=rw;sig.motion_physical_height=sig.motion_source_height=rh;
    ID3D12QueryHeap* timing=nullptr;D3D12_QUERY_HEAP_DESC timing_desc{};timing_desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;timing_desc.Count=4;
    Hr(ID3D12Device_CreateQueryHeap(h.device,&timing_desc,IID_ID3D12QueryHeap,(void**)&timing),"host timestamp queries");
    auto* timing_readback=h.Buffer(4*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK);UINT64 frequency=0;
    Hr(ID3D12CommandQueue_GetTimestampFrequency(h.queue,&frequency),"direct queue timestamp frequency");Require(frequency!=0,"nonzero GPU clock");
    const auto initial_memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
    for(unsigned frame=0;frame<6;++frame){
        Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"begin production recording");h.released=false;
        const auto start=std::chrono::steady_clock::now();
        {
            nr_source_runtime::EvaluateScope source(h.list,parent,params,feature);
            Require(bool(nr_source_runtime::CurrentSource()),"actual native source producer");
            Require(RecordNrUseLocked(h.list,context,slot),"Core resource recording lease");
            if(frame==0){
                const auto capture_test=[&](const FeatureRecord& f,const NrGuideBinding& g){return nr_post_product::Capture(h.list,h.device,context,parent,params,f,g,
                    CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);};
                const auto refused=[&](const FeatureRecord& f,const NrGuideBinding& g,nr_private_sr_status::State reason){
                    Require(capture_test(f,g)==0&&nr_private_sr_status::state.load()==reason,"product refusal reason");
                    Require(context.private_sr==nullptr&&nr_private_sr_status::captures.load()==0&&nr_private_sr_status::bytes.load()==0,"refusal records no private GPU or model work");};
                g_config.private_residual_sr=false;Require(capture_test(feature,guides)==0&&nr_private_sr_status::requested.load()==0&&!context.private_sr,"disabled skips even request and allocation");
                g_config.private_residual_sr=true;g_config.temporal_backend=1;refused(feature,guides,nr_private_sr_status::State::Conflict);
                Require(g_config.temporal_backend==1,"conflict preserves saved stabilizer");g_config.temporal_backend=0;
                auto unsupported=feature;unsupported.feature_id=13;refused(unsupported,guides,nr_private_sr_status::State::Route);
                unsupported=feature;unsupported.create_flags|=1;refused(unsupported,guides,nr_private_sr_status::State::Domain);
                SetFloat(params,"Jitter.Offset.X",.25f);refused(feature,guides,nr_private_sr_status::State::Phase);SetFloat(params,"Jitter.Offset.X",0);
                SetFloat(params,"MV.Scale.X",0);refused(feature,guides,nr_private_sr_status::State::Guides);SetFloat(params,"MV.Scale.X",1);
                auto bad=guides;bad.signature.depth_x=rw;refused(feature,bad,nr_private_sr_status::State::Guides);
                auto* active=nr_source_runtime::evaluation;nr_source_runtime::evaluation=nullptr;
                refused(feature,guides,nr_private_sr_status::State::Source);nr_source_runtime::evaluation=active;
            }
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,0);
            const unsigned capture=nr_post_product::Capture(h.list,h.device,context,parent,params,feature,guides,
                CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,1);
            if(!capture)std::printf("capture reason=%s guide=%u\n",nr_private_sr_status::Name(nr_private_sr_status::state.load()),nr_private_sr_status::guide_reason.load());
            Require(capture!=0,"original product Capture/CreateOwner");
            Ngx(evaluate(h.list,parent,params,nullptr),"actual native baseline Evaluate");
            Copy(h,native,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Copy(h,native,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,2);
            Require(nr_post_product::Finish(h.list,context,slot,feature,capture,ow,oh,0,0),"original product Finish");
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,3);
            ID3D12GraphicsCommandList_ResolveQueryData(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,0,4,timing_readback,0);
            Require(context.private_sr&&!context.private_sr->poisoned,"no private quarantine");source.Finish(1);
        }
        const double record_ms=Millis(start);const auto submit=std::chrono::steady_clock::now();
        Hr(ID3D12GraphicsCommandList_Close(h.list),"recording close");ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};
        ProcessSubmittedCommandLists(0,h.queue,1,lists);
        Require(!g_lifecycle_tracking_uncertain.load()&&context.completion_fence_value,"actual Core Execute/Signal receipt");
        const double submit_ms=Millis(submit);Complete(h,queue);
        UINT64* stamps=nullptr;Hr(ID3D12Resource_Map(timing_readback,0,nullptr,(void**)&stamps),"completed timing readback");
        Require(stamps[1]>=stamps[0]&&stamps[3]>=stamps[2],"ordered GPU timestamps");
        const double guide_ms=double(stamps[1]-stamps[0])*1000/frequency,private_ms=double(stamps[3]-stamps[2])*1000/frequency;
        ID3D12Resource_Unmap(timing_readback,0,nullptr);
        Require(!nr_post_product::Ready(context),"fence alone cannot retire replayable recording");
        const auto retire=std::chrono::steady_clock::now();ResetAndClose(h);
        Require(nr_post_product::Ready(context),"fence plus real Reset permits retirement");
        const auto memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
        std::printf("METRIC frame=%u cpu_record_ms=%.6f cpu_submit_ms=%.6f cpu_retire_ms=%.6f gpu_guides_ms=%.6f gpu_private_sr_ms=%.6f plane_bytes=%llu dxgi_local_usage=%llu\n",
            frame,record_ms,submit_ms,Millis(retire),guide_ms,private_ms,nr_private_sr_status::bytes.load(),(unsigned long long)memory.local.CurrentUsage);
        Require(frame?nr_private_sr_status::composed.load()==frame:nr_private_sr_status::composed.load()==0,"first create preserves Z; subsequent exact composition");
    }
    Require(nr_private_sr_status::creations.load()==1&&nr_private_sr_status::captures.load()==6&&nr_private_sr_status::composed.load()==5&&nr_private_sr_status::adopted.load()==5,"distinct creation/record/submit counts");
    h.Pixels(final,.5f,L"actual-core-neutral.fp16",.04);
    const auto baseline=Bytes(h,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),output=Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::ofstream saved_base("actual-core-native-base.fp16",std::ios::binary);saved_base.write(baseline.data(),baseline.size());saved_base.close();
    Require(!baseline.empty()&&baseline==output,"zero edit preserves every native B byte exactly");ReleaseCom(timing);
    const auto peak=nr_private_sr_status::peak.load();Require(nr_post_product::Destroy(context)&&!context.private_sr,"product original owner retirement");
    Require(nr_private_sr_status::bytes.load()==0,"owned plane account retired");
    ForgetFeature(parent);Ngx(release(parent),"native parent Release");Ngx(destroy(params),"native parameters destroyed");
    const auto final_memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
    std::printf("METRIC peak_plane_bytes=%llu dxgi_before=%llu dxgi_after=%llu\n",peak,(unsigned long long)initial_memory.local.CurrentUsage,(unsigned long long)final_memory.local.CurrentUsage);
    g_codec.device=nullptr;slot.source=slot.result=nullptr;
    std::printf("PASS actual generated Core Feature1 checks=%u create=1 evaluate=5 submit=6 retire=1; no Feature18 or game\n",checks);return 0;
}
}
int wmain(int argc,wchar_t**argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;try{return product_real::Run(argv[1]);}
catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
