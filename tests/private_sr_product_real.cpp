// Included after the unmodified generated Core and the existing real-device Host.
// Synthetic scene with real native Feature1/13; optional real Feature18 chain,
// followed by the unchanged private Feature1 product path. No game acceptance.
#include <chrono>
#include <fstream>
namespace product_real {
using namespace sr_host;
NrContext context;
#if defined(NR_TEST_RESHADE_RUNTIME)
LONG WINAPI DiagnosticException(EXCEPTION_POINTERS* e){
    if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module=nullptr;wchar_t path[MAX_PATH]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(e->ExceptionRecord->ExceptionAddress),&module);
    GetModuleFileNameW(module,path,MAX_PATH);const auto* leaf=std::wcsrchr(path,L'\\');
    std::printf("RESHADE_AV module=%ls offset=%llx access=%llu address=%llx rdx=%llx\n",leaf?leaf+1:path,
        (unsigned long long)(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(module)),
        (unsigned long long)e->ExceptionRecord->ExceptionInformation[0],(unsigned long long)e->ExceptionRecord->ExceptionInformation[1],
        (unsigned long long)e->ContextRecord->Rdx);std::fflush(stdout);return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* p){ID3D12CommandQueue_ExecuteCommandLists(q,n,p);}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* l,ID3D12CommandAllocator* a,ID3D12PipelineState* p){return ID3D12GraphicsCommandList_Reset(l,a,p);}
double Millis(std::chrono::steady_clock::time_point t){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
void Register(Host& h){
    CommandStateMetaContract meta;meta.device_identity=g_next_command_device_identity.fetch_add(1);
    // Real creation observer reads this same device lifetime record. Keep the
    // cold proxy alias proof consistent with the registered native list.
    Hr(ID3D12Device_SetPrivateData(h.device,kCommandStateMetaContract,sizeof(meta),&meta),"device creation identity record");
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
// Independent CPU scene producer: positive viewport jitter shifts the rendered
// image right/down, hence raster values sample the unshifted scene at p-jitter.
void Scene(Host& h,ID3D12Resource* texture,float jitter_x,float jitter_y,bool normal=false){
    const auto d=ID3D12Resource_GetDesc(texture);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&size);auto* upload=h.Buffer(size,D3D12_HEAP_TYPE_UPLOAD);
    char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,reinterpret_cast<void**>(&mapped)),"scene upload map");
    const auto half=[](float value){if(value==0)return static_cast<unsigned short>(0);int e=0;std::frexp(value,&e);--e;
        return static_cast<unsigned short>(unsigned(e+15)*1024+unsigned(std::nearbyint(std::ldexp(value,10-e)))-1024);};
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x){
        const float u=std::clamp((x+.5f-jitter_x)/float(d.Width),0.f,1.f),v=std::clamp((y+.5f-jitter_y)/float(d.Height),0.f,1.f);
        const float color[]={normal?0.f:.125f+.75f*u,normal?0.f:.125f+.75f*v,normal?1.f:.25f,normal?.5f:1.f};
        unsigned short pixel[4]{};for(unsigned k=0;k<4;++k)pixel[k]=half(color[k]);std::memcpy(mapped+y*fp.Footprint.RowPitch+x*8,pixel,8);
    }
    ID3D12Resource_Unmap(upload,0,nullptr);h.Begin();h.Barrier(texture,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(texture,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    h.Submit();h.Wait();
    // Scene staging is complete and its recording retired. Do not grow the
    // measurement baseline by retaining one upload allocation per source frame.
    const auto staged=std::find(h.owned.begin(),h.owned.end(),upload);
    Require(staged!=h.owned.end(),"owned scene upload");h.owned.erase(staged);ReleaseCom(upload);
}
namespace present=nr_compat_present_runtime;
// Controlled assembly reuses the actual signed NR model/codec/spatial helpers.
// It does not call Present or fabricate a Present source. The native source and
// the original Core Capture/Finish/submit/Reset path remain those of this host.
struct RealNrChain {
    present::Context* c=nullptr;unsigned completed_frames=0;bool created_this_frame=false;
    void Build(Host& h,unsigned layers,unsigned nw,unsigned nh,unsigned ow,unsigned oh,const NrGuideBinding& guide){
        Require(EnsureNrApi(h.device)&&EnsureCodec(h.device),"real NR API and complete Core codec");
        nr_layers::Migrate(g_config.nr_layers,layers,nr_layers::Primary(g_config));
        for(unsigned i=1;i<layers;++i){auto& l=g_config.nr_layers[i-1];l.intensity=1.f+.1f*i;l.local_tone=.8f+.03f*i;l.local_structure=.7f+.03f*i;}
        c=new present::Context;c->device=h.device;ID3D12Device_AddRef(h.device);c->queue=h.queue;ID3D12CommandQueue_AddRef(h.queue);
        c->source={ow,oh};c->work={nw,nh};c->format=DXGI_FORMAT_R16G16B16A16_FLOAT;c->encoding=present::Encoding::ScRgb;
        c->passes=layers;c->numerator=c->denominator=1;c->generation=1;c->uses_native_guides=true;c->native_guides=guide;
        c->tuning=CurrentNrTuningSignature();c->tuning.input_filter=0;
        for(unsigned i=0;i<layers;++i){c->layer_tuning[i]=CurrentNrTuningSignature(i);c->layer_tuning[i].input_filter=0;}
        Require(present::Build(*c),"real independent signed NR layers Build");
    }
    bool Record(Host& h,ID3D12Resource* base,ID3D12Resource* final,const NrGuideBinding& guide){
        c->native_guides=guide;created_this_frame=false;auto* list=h.list;auto& image=c->image;
        auto base_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Transition(list,base,base_state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(list,image.source,image.source_state,D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12GraphicsCommandList_CopyResource(list,image.source,base);Transition(list,base,base_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,image.source,image.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,image.proxy,image.proxy_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);present::Codec(*c,list,false);UavBarrier(list,image.proxy);
        Transition(list,image.proxy,image.proxy_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for(unsigned i=0;i<c->passes;++i){auto& l=c->layers[i];auto* params=l.evaluation_params[0];
            if(!l.context.nr_handle){
                if(!i){Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    present::Utility(*c,list,l.init_output,c->init_color,c->work);UavBarrier(list,image.neural);}
                else{auto& parent=i==1?image:c->layers[i-1].binding;auto state=unsigned(parent.neural_state);
                    Require(nr_dual::dx12::RecordCreationInputs(c->spatial_pipelines,l.spatial,list,state),"real preceding layer initializes next Create");parent.neural_state=D3D12_RESOURCE_STATES(state);}
                Require(present::CreateLayer(*c,l,list),"real signed Feature18 Create");created_this_frame=true;return false;
            }
            Require(present::Bind(*c,l,params),"same-source N guides bound to each real model");
            if(!i){Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Ngx(present::Evaluate(&l,list,l.context.nr_handle,params),"real signed primary Feature18 Evaluate");
                UavBarrier(list,image.neural);Transition(list,image.neural,image.neural_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
            else{auto& parent=i==1?image:c->layers[i-1].binding;auto state=unsigned(parent.neural_state);auto& previous=c->layers[i-1];
                nr_dual::Admission a{};a.primary_feature=previous.context.nr_handle;a.primary_params=previous.evaluation_params[0];
                a.secondary_feature=l.context.nr_handle;a.secondary_params=params;a.secondary_create_extent={l.context.width,l.context.height};
                a.creation_completion_proven=c->timeline.Ready(ID3D12Fence_GetCompletedValue(c->fence));
                a.recording_and_queue_admitted=c->queue==h.queue&&!c->timeline.Poisoned();a.dependencies_pinned=c->recorded&&l.spatial;
                a.current_inputs_and_guides_bound=l.binding.proxy&&l.binding.neural&&l.frame_guides.depth&&l.frame_guides.motion;
                auto result=nr_dual::dx12::RecordSecond(c->spatial_pipelines,l.spatial,list,state,l.plan,a,present::Evaluate,&l);
                Require(result.HasSecond(),"real full previous output feeds next independent Feature18");parent.neural_state=D3D12_RESOURCE_STATES(state);
                l.binding.neural_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
            ++l.context.evaluations;l.context.guide_history.CommitOnEvaluate(l.frame_guides.signature,true);
            if(l.context.diagnostic_reset_requested)l.context.successful_evaluations_since_reset=0;
            ++l.context.successful_evaluations_since_reset;l.context.reset_retry_pending=false;
        }
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);present::Codec(*c,list,true);UavBarrier(list,image.result);
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        h.Barrier(final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12GraphicsCommandList_CopyResource(list,final,image.result);
        h.Barrier(final,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,image.result,image.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);++completed_frames;return true;
    }
    void SubmittedAndWait(Host& h){
        if(!c)return;const auto value=c->timeline.Reserve(0);Require(value,"NR exact fence reservation");
        Hr(ID3D12CommandQueue_Signal(h.queue,c->fence,value),"NR host exact post-execution fence");
        Require(c->timeline.Submitted(0,value,created_this_frame),"NR actual Signal receipt");c->recorded=true;
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Hr(ID3D12Fence_SetEventOnCompletion(c->fence,value,event),"NR completion event");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"NR exact completion");CloseHandle(event);
    }
    void Verify(Host& h,unsigned expected_frames){
        Require(c&&completed_frames==expected_frames,"requested complete real NR chain Z producers");
        std::vector<char> previous;
        for(unsigned i=0;i<c->passes;++i){auto& l=c->layers[i];
            const auto input=Bytes(h,l.binding.proxy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),output=Bytes(h,l.binding.neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Require(input!=output,"each real model changes its own input");if(i)Require(input==previous,"full previous NR answer, not only last residual, feeds next layer");
            previous=output;std::ofstream f("real-nr-layer-"+std::to_string(i+1)+".fp16",std::ios::binary);f.write(output.data(),output.size());
            for(unsigned j=0;j<i;++j)Require(l.context.nr_handle!=c->layers[j].context.nr_handle&&l.context.params!=c->layers[j].context.params,"independent real Feature18 identities");
        }
        Require(present::RetireProof(c),"all NR exact fences drained");
    }
    void Destroy(){Require(present::Release(c)&&!c,"real signed NR release after host recording ended and all fences");}
};

// Test-only captures are recorded on the exact producing command list. Mapping
// occurs only after its Core fence completes, and never replays model work.
struct PlaneCapture {
    const char* name=nullptr; ID3D12Resource* readback=nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    unsigned width=0,height=0,bytes_per_pixel=0,format=0;
};
PlaneCapture CapturePlane(Host& h,const char* name,ID3D12Resource* resource,D3D12_RESOURCE_STATES state){
    const auto d=ID3D12Resource_GetDesc(resource);PlaneCapture c{};c.name=name;
    c.width=unsigned(d.Width);c.height=d.Height;c.format=unsigned(d.Format);
    Require(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"capture known uncompressed plane format");
    c.bytes_per_pixel=d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:8;UINT64 bytes=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&c.footprint,nullptr,nullptr,&bytes);
    c.readback=h.Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
    h.Barrier(resource,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=c.readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=c.footprint;
    from.pResource=resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);
    h.Barrier(resource,D3D12_RESOURCE_STATE_COPY_SOURCE,state);return c;
}
void SaveCaptures(const std::vector<PlaneCapture>& captures,nr_private_sr::Source source,nr_private_sr::Recording recording,unsigned frame,bool rr,bool private_sr,unsigned temporal_case){
    std::ofstream manifest("actual-core-same-frame.json");Require(bool(manifest),"capture identity file");
    manifest<<"{\"schema\":\"actual-core-same-frame-v1\",\"frame\":"<<frame<<",\"source_stream\":"<<source.stream
        <<",\"source_epoch\":"<<source.epoch<<",\"source_sequence\":"<<source.sequence
        <<",\"recording_lifetime\":"<<recording.lifetime<<",\"queue\":"<<recording.queue
        <<",\"native_feature\":"<<(rr?13:1)<<",\"private_sr\":"<<(private_sr?"true":"false")
        <<",\"temporal_case\":"<<temporal_case
        <<",\"exact_fence_completed\":true,\"capture_excluded_from_cost\":true,\"planes\":[";
    for(unsigned i=0;i<captures.size();++i){const auto& c=captures[i];char* mapped=nullptr;
        Hr(ID3D12Resource_Map(c.readback,0,nullptr,(void**)&mapped),"completed same-frame capture map");
        std::ofstream file(c.name,std::ios::binary);Require(bool(file),"capture plane file");
        for(unsigned y=0;y<c.height;++y)file.write(mapped+c.footprint.Offset+size_t(y)*c.footprint.Footprint.RowPitch,size_t(c.width)*c.bytes_per_pixel);
        Require(bool(file),"capture plane write");ID3D12Resource_Unmap(c.readback,0,nullptr);
        if(i)manifest<<",";manifest<<"{\"file\":\""<<c.name<<"\",\"width\":"<<c.width<<",\"height\":"<<c.height
            <<",\"dxgi_format\":"<<c.format<<",\"bytes_per_pixel\":"<<c.bytes_per_pixel<<"}";
    }
    manifest<<"]}\n";Require(bool(manifest),"capture identity write");
}
// The spatial-only control normalizes the same native guides without creating
// a private Feature1 owner, history or carrier. Its leases use exact Core proof.
struct GuideOnly {
    nr_edit_dx12::Kernels* kernels=nullptr;ID3D12Resource *depth=nullptr,*motion=nullptr;
    nr_private_sr::Recording recording{};nr_private_sr::Source previous{};
    void Record(Host& h,LifecycleQueue* queue,const NrGuideBinding& guides,const FeatureRecord& feature,const NgxParams* params,
                unsigned rw,unsigned rh,unsigned nw,unsigned nh,unsigned ow,unsigned oh){
        if(!kernels){kernels=nr_edit_dx12::Create(h.device,reinterpret_cast<UINT64>(h.device),queue->identity,nr_private_sr_artifacts::Kernels(),nr_post_product::SameDevice);
            Require(kernels!=nullptr,"spatial control guide normalizer");nr_edit_dx12::SetRetirementGuard(kernels,nr_post_product::RecordingReleased,nullptr);
            UINT64 bytes=0;depth=nr_edit_dx12::CreatePlane(h.device,nw,nh,41,768ull<<20,&bytes);
            motion=nr_edit_dx12::CreatePlane(h.device,nw,nh,16,768ull<<20,&bytes);Require(depth&&motion,"spatial control N guide planes");}
        Require(nr_edit_dx12::Collect(kernels),"retire prior guide recording");
        const auto source=nr_source_runtime::CurrentSource();recording=nr_post_product::RecordingFor(h.list,queue);
        nr_private_sr_guides::Input input{};input.enabled=true;input.source=input.snapshot_source={source.stream,source.epoch,source.sequence};
        input.recording=input.snapshot_recording=recording;input.signature=guides.signature;
        input.native_feature=unsigned(feature.feature_id);input.native_flags=unsigned(feature.create_flags);input.native_flags_known=feature.create_flags_known;
        input.read_boundary_snapshot=true;input.render_width=rw;input.render_height=rh;input.work_width=nw;input.work_height=nh;input.output_width=ow;input.output_height=oh;
        input.original_motion_scale_known=GetFloat(params,"MV.Scale.X",&input.original_motion_scale_x)&&GetFloat(params,"MV.Scale.Y",&input.original_motion_scale_y);
        input.original_jitter_known=GetFloat(params,"Jitter.Offset.X",&input.original_jitter_x)&&GetFloat(params,"Jitter.Offset.Y",&input.original_jitter_y);
        input.raster_convention=nr_private_sr_guides::RasterConvention::NgxViewportOffset;input.motion_reference=previous;
        int reset=1;GetInt(params,"Reset",&reset);input.reset=reset!=0||!previous||previous.sequence+1!=input.source.sequence;
        input.depth={guides.depth,41,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true};
        input.motion={guides.motion,16,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,true};
        Require(nr_source_runtime::PrivateSrInputReadState(h.list,guides.depth,input.depth.state)&&
            nr_source_runtime::PrivateSrInputReadState(h.list,guides.motion,input.motion.state),"same native guide read-state proof");
        auto result=nr_private_sr_guides::Record(kernels,h.list,input,{depth,41,0,D3D12_RESOURCE_STATE_COMMON,true},{motion,16,0,D3D12_RESOURCE_STATE_COMMON,true});
        Require(result.dispatch.recorded&&result.normalized_guide_units_verified,"same production native guide normalization");previous=input.source;
    }
    void Submitted(LifecycleQueue* queue){if(kernels)Require(nr_edit_dx12::Submitted(kernels,recording,queue->fence,
        {queue->identity,reinterpret_cast<UINT64>(queue->fence),context.completion_fence_value},true),"spatial guides exact submission");}
    void Destroy(){if(kernels){Require(nr_edit_dx12::Collect(kernels)&&nr_edit_dx12::ReadyToRetire(kernels),"spatial guide fences and recordings retired");
        Require(nr_edit_dx12::DestroyAfterRetirement(kernels),"spatial guide normalizer release");ReleaseCom(depth);ReleaseCom(motion);}}
};

// 0=off, 1=PostRr AMDOF, 2=PostRr AMDOF low-frequency,
// 3=PostRr native-MV contract refusal. Later-layer P1 is not available here.
int Run(const wchar_t* bridge,unsigned nr_layers_count,unsigned percent,bool rr,unsigned requested_frames,bool odd,bool private_sr,unsigned temporal_case){
    Require(temporal_case<=3&&(!temporal_case||(rr&&nr_layers_count&&!private_sr)),
        "PostRr temporal requires real RR, at least one real NR layer, and private SR OFF");
#if defined(NR_TEST_RESHADE_RUNTIME)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);AddVectoredExceptionHandler(1,DiagnosticException);
#endif
    InitializeCriticalSection(&g_chain_cs);Host h;
    auto* native_device=h.device;auto* native_list=h.list;
#if defined(NR_TEST_RESHADE_RUNTIME)
    const GUID test_unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    ID3D12Device* raw_device=nullptr;ID3D12GraphicsCommandList* raw_list=nullptr;
    Hr(ID3D12Device_QueryInterface(native_device,test_unwrapped,(void**)&raw_device),"ReShade device raw interface");
    Hr(ID3D12GraphicsCommandList_QueryInterface(native_list,test_unwrapped,(void**)&raw_list),"ReShade list raw interface");
    Require(raw_device&&raw_device!=native_device&&raw_list&&raw_list!=native_list,"actual distinct ReShade proxy interfaces");
    Require(IsSameRrDepthDevice(native_device,raw_device),"production proxy/raw device identity");
    h.list=raw_list;Register(h);h.list=native_list; // Register the actual native pointer once, before any recording.
    std::puts("RESHADE domains: Init/native parent=proxy; Core-owned device/list=raw; Feature18 recording=proxy; private Feature1=production live original alias; Core recording=raw");
#else
    Register(h);
#endif
    GetCurrentDirectoryW(MAX_PATH,g_log_path);wcscat_s(g_log_path,L"\\product-real.log");
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
    // The pinned real RR runtime hangs at the old synthetic 320x192 input on
    // its second native Evaluate even with no private Feature1 creation. The
    // 1280x720 -> 1920x1080 control passes; this is a fixture choice, not a
    // claimed vendor minimum or a product admission restriction.
    const unsigned rw=(rr?1280:320)+unsigned(odd),rh=(rr?720:192)+unsigned(odd),
        ow=(rr?1920:640)+unsigned(odd),oh=(rr?1080:384)+unsigned(odd);
    const unsigned nw=ow*percent/100,nh=oh*percent/100;
    for(auto v:{std::pair{"Width",rw},{"Height",rh},{"OutWidth",ow},{"OutHeight",oh},{"CreationNodeMask",1u},{"VisibilityNodeMask",1u},
                {"DLSS.Render.Subrect.Dimensions.Width",rw},{"DLSS.Render.Subrect.Dimensions.Height",rh}})SetUInt(params,v.first,v.second);
    SetInt(params,"PerfQualityValue",0);SetInt(params,"DLSS.Feature.Create.Flags",75);
    Require(MH_Initialize()==MH_OK,"controlled host actual lifecycle hook initialization");
#if defined(NR_TEST_RESHADE_RUNTIME)
    // No addon is loaded by this isolated host. Run the actual observer producer
    // normally invoked by OnNrFormatInitDevice(get_native()), never fake receipts.
    Require(EnsureNrNativeFormatHooks(raw_device),"real native format init observer");
#endif
    ScopedNgxLayer native_layer(NgxLayer::Core);g_original_core_create=create;
    if(rr){SetInt(params,"DLSS.Denoise.Mode",1);SetUInt(params,"DLSS.Roughness.Mode",0);SetUInt(params,"DLSS.Use.HW.Depth",1);}
    NgxHandle* parent=nullptr;h.Begin();Ngx(HookCoreCreate(h.list,rr?13:1,params,&parent),"observed original native parent Create");h.Submit();h.Wait();
    const auto feature=FindFeature(parent);Require(feature.generation&&feature.create_flags_known,"observed parent contract");
#if defined(NR_TEST_RESHADE_RUNTIME)
    // The raw aliases share the same device and recording; their QI references
    // become Host-owned. Keep original proxy references for native forwarding.
    h.device=raw_device;h.list=raw_list; // The underlying native pointer was registered before parent creation.
    Require(InstallNativeCommandStateHooks(h.list),"actual product native state hooks");
#endif
    if(nr_layers_count)Require(EnsureCodec(h.device)&&EnsureNrApi(h.device),"real codec/NR initialization before private capture");
    g_codec.device=h.device;g_config.enabled=true;g_enabled_fast.store(true);g_config.private_residual_sr=private_sr;
    g_config.temporal_backend=temporal_case==3?1:temporal_case?2:0;
    g_config.temporal_scope=0;g_config.temporal_mode=temporal_case==2?1:0;
    g_config.nr_passes=nr_layers_count>1?2:1;g_config.nr_full_reference=nr_layers_count>2;
    g_config.nr_full_passes=nr_layers_count;
    g_config.processing_start=nr_processing::Start::After;nr_processing_runtime::Reconcile();
    auto* color=h.Plane(rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.5f);
    auto* depth=h.Plane(rw,rh,DXGI_FORMAT_R32_FLOAT,false);auto* motion=h.Plane(rw,rh,DXGI_FORMAT_R32G32_FLOAT,false);
    auto* native=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    auto* base=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    auto* final=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    for(auto p:{std::pair{"Color",color},{"Depth",depth},{"MotionVectors",motion},{"Output",native}})SetResource(params,p.first,p.second);
#if defined(NR_TEST_RESHADE_RUNTIME)
    ID3D12Device* resource_device=nullptr;
    Hr(ID3D12Resource_GetDevice(color,IID_ID3D12Device,(void**)&resource_device),"native color device identity");
    std::printf("RESHADE resource_GetDevice_proxy=%u raw=%u\n",resource_device==native_device,resource_device==raw_device);
    Require(IsSameRrDepthDevice(resource_device,raw_device),"native resource belongs to exact device");ReleaseCom(resource_device);
#endif
    float view[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float projection[16]{1,0,0,0,0,1,0,0,0,0,0,1,0,0,.1f,0}; // reversed infinite device depth
    if(rr){
        auto* albedo=h.Plane(rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.5f);
        auto* specular=h.Plane(rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,false,.125f);
        auto* normals=h.Plane(rw,rh,DXGI_FORMAT_R16G16B16A16_FLOAT,false);Scene(h,normals,0,0,true);
        auto* roughness=h.Plane(rw,rh,DXGI_FORMAT_R32_FLOAT,false,.5f);
        SetResource(params,"DLSS.Input.DiffuseAlbedo",albedo);SetResource(params,"DLSS.Input.SpecularAlbedo",specular);
        SetResource(params,"GBuffer.Normals",normals);SetResource(params,"GBuffer.Roughness",roughness);
        SetVoid(params,"WorldToViewMatrix",view);SetVoid(params,"ViewToClipMatrix",projection);
        for(const char* key:{"DLSS.Input.DiffuseAlbedo.Subrect.Base.X","DLSS.Input.DiffuseAlbedo.Subrect.Base.Y",
            "DLSS.Input.SpecularAlbedo.Subrect.Base.X","DLSS.Input.SpecularAlbedo.Subrect.Base.Y","DLSS.Input.Normals.Subrect.Base.X",
            "DLSS.Input.Normals.Subrect.Base.Y","DLSS.Input.Roughness.Subrect.Base.X","DLSS.Input.Roughness.Subrect.Base.Y"})SetUInt(params,key,0);
    }
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
    context.instance_id=1;context.feature_generation=feature.generation;context.mode=1;
    auto& slot=context.slots[0];slot.source=base;slot.result=final;slot.source_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    slot.result_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;slot.snapshots_native_output=true;
    slot.result_format=slot.result_uav_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    slot.codec_color_mode=PostSrCodecColorMode(ClassifyPostSrStorage(
        ID3D12Resource_GetDesc(native).Format,feature.create_flags));
    NrGuideBinding guides{};guides.depth=depth;guides.motion=motion;auto& sig=guides.signature;
    sig.depth_format=41;sig.motion_format=16;sig.depth_inverted=1;sig.depth_physical_width=sig.depth_width=rw;sig.depth_physical_height=sig.depth_height=rh;
    sig.motion_physical_width=sig.motion_source_width=rw;sig.motion_physical_height=sig.motion_source_height=rh;
    ID3D12QueryHeap* timing=nullptr;D3D12_QUERY_HEAP_DESC timing_desc{};timing_desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;timing_desc.Count=6;
    Hr(ID3D12Device_CreateQueryHeap(h.device,&timing_desc,IID_ID3D12QueryHeap,(void**)&timing),"host timestamp queries");
    auto* timing_readback=h.Buffer(6*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK);UINT64 frequency=0;
    Hr(ID3D12CommandQueue_GetTimestampFrequency(h.queue,&frequency),"direct queue timestamp frequency");Require(frequency!=0,"nonzero GPU clock");
    const auto initial_memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
    RealNrChain chain;GuideOnly standalone_guides;unsigned frame=0,produced=0,temporal_recorded=0;
    for(;produced<requested_frames&&frame<nr_layers_count+requested_frames+2;++frame){
        const float jitter_x=frame%2?.25f:-.25f,jitter_y=frame%3?.125f:-.125f;
        if(nr_layers_count||rr)Scene(h,color,jitter_x,jitter_y);
        Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"begin production recording");h.released=false;
        SetFloat(params,"Jitter.Offset.X",jitter_x);SetFloat(params,"Jitter.Offset.Y",jitter_y);SetInt(params,"Reset",frame==0?1:0);
        const auto start=std::chrono::steady_clock::now();
        std::vector<PlaneCapture> captures;nr_private_sr::Source capture_source{};nr_private_sr::Recording capture_recording{};
        {
            // Match the live scope held by the actual original DispatchEvaluate.
            NrPostBoundaryAliasScope source_list_alias(native_list);
#if defined(NR_TEST_RESHADE_RUNTIME)
            const auto raw_identity=SnapshotPostBoundaryList(h.list);
            const auto proxy_identity=SnapshotPostBoundaryList(native_list);
            const auto& alias_identity=source_list_alias.identity;
            Require(raw_identity.known&&alias_identity.known&&proxy_identity.known&&
                raw_identity.address==alias_identity.address&&raw_identity.token==alias_identity.token&&
                raw_identity.reset==alias_identity.reset&&raw_identity.device==alias_identity.device&&
                proxy_identity.address==raw_identity.address&&proxy_identity.token==raw_identity.token&&
                proxy_identity.reset==raw_identity.reset&&proxy_identity.device==raw_identity.device,
                "production cold alias equals current raw recording snapshot");
#endif
            Require(NrPrivateOriginalCallList(h.list)==native_list,"production private call resolves live original interface");
            nr_source_runtime::EvaluateScope source(h.list,parent,params,feature);
            Require(bool(nr_source_runtime::CurrentSource()),"actual native source producer");
            Require(RecordNrUseLocked(h.list,context,slot),"Core resource recording lease");
            if(frame==0&&private_sr){
                const auto capture_test=[&](const FeatureRecord& f,const NrGuideBinding& g){return nr_post_product::Capture(h.list,h.device,context,parent,params,f,g,
                    CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);};
                const auto refused=[&](const FeatureRecord& f,const NrGuideBinding& g,nr_private_sr_status::State reason){
                    Require(capture_test(f,g)==0&&nr_private_sr_status::state.load()==reason,"product refusal reason");
                    Require(context.private_sr==nullptr&&nr_private_sr_status::captures.load()==0&&nr_private_sr_status::bytes.load()==0,"refusal records no private GPU or model work");};
                g_config.private_residual_sr=false;Require(capture_test(feature,guides)==0&&nr_private_sr_status::requested.load()==0&&!context.private_sr,"disabled skips even request and allocation");
                g_config.private_residual_sr=true;g_config.temporal_backend=1;refused(feature,guides,nr_private_sr_status::State::Conflict);
                Require(g_config.temporal_backend==1,"conflict preserves saved stabilizer");g_config.temporal_backend=0;
                auto* original_alias=g_nr_post_boundary_alias_scope;g_nr_post_boundary_alias_scope=nullptr;
                refused(feature,guides,nr_private_sr_status::State::OriginalInterface);
                g_nr_post_boundary_alias_scope=original_alias;
                auto unsupported=feature;unsupported.feature_id=18;refused(unsupported,guides,nr_private_sr_status::State::Route);
                unsupported=feature;unsupported.create_flags_known=false;refused(unsupported,guides,nr_private_sr_status::State::Domain);
                SetFloat(params,"Jitter.Offset.X",.75f);refused(feature,guides,nr_private_sr_status::State::Phase);SetFloat(params,"Jitter.Offset.X",jitter_x);
                SetFloat(params,"MV.Scale.X",0);refused(feature,guides,nr_private_sr_status::State::Guides);SetFloat(params,"MV.Scale.X",1);
                auto bad=guides;bad.signature.depth_x=rw;refused(feature,bad,nr_private_sr_status::State::Guides);
                auto* active=nr_source_runtime::evaluation;nr_source_runtime::evaluation=nullptr;
                refused(feature,guides,nr_private_sr_status::State::Source);nr_source_runtime::evaluation=active;
            }
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,0);
            const unsigned capture=nr_post_product::Capture(h.list,h.device,context,parent,params,feature,guides,
                CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);
            if(private_sr&&!capture)std::printf("capture reason=%s guide=%u\n",nr_private_sr_status::Name(nr_private_sr_status::state.load()),nr_private_sr_status::guide_reason.load());
            if(private_sr)Require(capture!=0,"original product Capture/CreateOwner");
            else {Require(!capture&&!context.private_sr,"spatial control creates no private SR owner");
                if(nr_layers_count)standalone_guides.Record(h,queue,guides,feature,params,rw,rh,nw,nh,ow,oh);}
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,1);
            Ngx(evaluate(native_list,parent,params,nullptr),"actual native baseline Evaluate");
            Copy(h,native,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Copy(h,native,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,2);
            bool final_z=true;
            NrGuideBinding normalized{};
            if(nr_layers_count){
                normalized.depth=private_sr?context.private_sr->flights[capture-1].depth:standalone_guides.depth;
                normalized.motion=private_sr?context.private_sr->flights[capture-1].motion:standalone_guides.motion;auto& ns=normalized.signature;
                ns.depth_width=ns.motion_source_width=ns.motion_bound_width=nw;ns.depth_height=ns.motion_source_height=ns.motion_bound_height=nh;
                ns.depth_physical_width=ns.motion_physical_width=ns.color_physical_width=nw;ns.depth_physical_height=ns.motion_physical_height=ns.color_physical_height=nh;
                ns.depth_format=41;ns.motion_format=16;ns.color_format=10;ns.normalized_create_flags=9;ns.depth_inverted=1;
                ns.motion_scale_x=ns.motion_scale_y=1;normalized.valid_motion_scale_x=normalized.valid_motion_scale_y=true;normalized.game_reset=frame==0;
                h.Barrier(normalized.depth,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                h.Barrier(normalized.motion,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if(!chain.c)chain.Build(h,nr_layers_count,nw,nh,ow,oh,normalized);
#if defined(NR_TEST_RESHADE_RUNTIME)
                // Actual NR model calls retain the native proxy boundary. Core
                // Capture/Finish and their recording receipts remain raw.
                h.list=native_list;
#endif
                final_z=chain.Record(h,base,final,normalized);
#if defined(NR_TEST_RESHADE_RUNTIME)
                h.list=raw_list;
#endif
                h.Barrier(normalized.depth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
                h.Barrier(normalized.motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
            }
            if(final_z)++produced;
            const bool capture_frame=final_z&&produced==requested_frames;
            if(capture_frame){const auto source_id=nr_source_runtime::CurrentSource();capture_source={source_id.stream,source_id.epoch,source_id.sequence};
                capture_recording=nr_post_product::RecordingFor(h.list,queue);
                captures.push_back(CapturePlane(h,"same-frame-B.fp16",base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
                captures.push_back(CapturePlane(h,"same-frame-Z.fp16",final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));}
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,3);
            if(final_z&&temporal_case){
                // This is the same Evaluate's live exposure contract. The
                // observed Flags75 FP16 parent declares HDR; do not relabel it SDR.
                const auto color_contract=ReadColorContract(params);
                auto prepared=nr_temporal::PrepareForRoute(nr_temporal_route::Entry::PostRr,
                    h.list,context,slot,normalized,feature,ow,oh,0,0,false,&color_contract);
                if(temporal_case==3){
                    Require(!prepared&&nr_temporal_status::state.load()==19,
                        "native MV remains rejected by actual PostRr motion contract");
                }else if(prepared){
                    const auto before=nr_temporal_status::recorded.load();
                    Require(nr_temporal::Record(h.list,context,slot,prepared),
                        "actual Core PostRr temporal Record");
                    if(nr_temporal_status::recorded.load()>before)++temporal_recorded;
                }
            }
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,4);
            if(capture_frame&&(temporal_case==1||temporal_case==2))
                captures.push_back(CapturePlane(h,"same-frame-temporal.fp16",final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            if(final_z&&private_sr)Require(nr_post_product::Finish(h.list,context,slot,feature,capture,ow,oh,0,0),"original product Finish once after complete final Z");
            ID3D12GraphicsCommandList_EndQuery(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,5);
            if(capture_frame){
                if(private_sr){auto planes=nr_post_private_sr_dx12::InspectRecordedPlanes(context.private_sr->backend,capture_recording);
                    Require(bool(planes),"same-recording private diagnostic planes");
                    captures.push_back(CapturePlane(h,"same-frame-carrier.fp16",planes.carrier,D3D12_RESOURCE_STATE_COMMON));
                    captures.push_back(CapturePlane(h,"same-frame-tail.fp32",planes.tail,D3D12_RESOURCE_STATE_COMMON));
                    captures.push_back(CapturePlane(h,"same-frame-reconstructed.fp16",planes.reconstructed,D3D12_RESOURCE_STATE_COMMON));}
                captures.push_back(CapturePlane(h,"same-frame-final.fp16",final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS));}
            ID3D12GraphicsCommandList_ResolveQueryData(h.list,timing,D3D12_QUERY_TYPE_TIMESTAMP,0,6,timing_readback,0);
            Require(!private_sr||(context.private_sr&&!context.private_sr->poisoned),"no private quarantine");source.Finish(1);
        }
        const double record_ms=Millis(start);const auto submit=std::chrono::steady_clock::now();
        Hr(ID3D12GraphicsCommandList_Close(h.list),"recording close");ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};
        ProcessSubmittedCommandLists(0,h.queue,1,lists);
        Require(!g_lifecycle_tracking_uncertain.load()&&context.completion_fence_value,"actual Core Execute/Signal receipt");
        const double submit_ms=Millis(submit);standalone_guides.Submitted(queue);Complete(h,queue);chain.SubmittedAndWait(h);
        if(temporal_case==1||temporal_case==2)nr_temporal::CollectGpuStats(context);
        if(!captures.empty())SaveCaptures(captures,capture_source,capture_recording,frame,rr,private_sr,temporal_case);
        UINT64* stamps=nullptr;Hr(ID3D12Resource_Map(timing_readback,0,nullptr,(void**)&stamps),"completed timing readback");
        for(unsigned n=1;n<6;++n)Require(stamps[n]>=stamps[n-1],"ordered GPU timestamps");
        const double guide_ms=double(stamps[1]-stamps[0])*1000/frequency,private_ms=double(stamps[5]-stamps[4])*1000/frequency;
        const double nr_ms=double(stamps[3]-stamps[2])*1000/frequency;
        const double temporal_ms=double(stamps[4]-stamps[3])*1000/frequency;
        ID3D12Resource_Unmap(timing_readback,0,nullptr);
        if(private_sr)Require(!nr_post_product::Ready(context),"fence alone cannot retire replayable recording");
        const auto retire=std::chrono::steady_clock::now();ResetAndClose(h);
        Require(nr_post_product::Ready(context),"fence plus real Reset permits retirement");
        const auto memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
        std::printf("METRIC frame=%u cpu_record_ms=%.6f cpu_submit_ms=%.6f cpu_retire_ms=%.6f gpu_guides_ms=%.6f gpu_private_sr_ms=%.6f gpu_nr_chain_ms=%.6f gpu_temporal_ms=%.6f nr_create=%u produced=%u plane_bytes=%llu temporal_bytes=%llu dxgi_local_usage=%llu diagnostic_capture=%u private_enabled=%u temporal_recorded=%u temporal_submitted=%llu temporal_gpu_frames=%llu temporal_gpu_tested=%llu temporal_gpu_adopted=%llu temporal_gpu_rejected=%llu temporal_gpu_depth_rejected=%llu\n",
            frame,record_ms,submit_ms,Millis(retire),guide_ms,private_ms,nr_ms,temporal_ms,chain.c&&chain.created_this_frame?1u:0u,produced,nr_private_sr_status::bytes.load(),nr_temporal_status::bytes.load(),(unsigned long long)memory.local.CurrentUsage,unsigned(!captures.empty()),unsigned(private_sr),temporal_recorded,(unsigned long long)nr_temporal_status::submitted.load(),(unsigned long long)nr_temporal_status::gpu_frames.load(),(unsigned long long)nr_temporal_status::gpu_tested.load(),(unsigned long long)nr_temporal_status::gpu_adopted.load(),(unsigned long long)nr_temporal_status::gpu_rejected.load(),(unsigned long long)nr_temporal_status::gpu_depth_rejected.load());
        Require(nr_private_sr_status::composed.load()==(private_sr&&produced?produced-1:0),"first create preserves Z; subsequent exact composition");
    }
    Require(produced==requested_frames,"all requested final Z frames completed");
    if(temporal_case==1||temporal_case==2)
        Require(temporal_recorded&&nr_temporal_status::recorded.load()==temporal_recorded&&
            nr_temporal_status::submitted.load()==temporal_recorded&&
            !context.temporal_token&&nr_temporal_status::gpu_config.load()==g_config_generation.load()&&
            nr_temporal_status::gpu_instance.load()==context.instance_id&&
            nr_temporal_status::gpu_tested.load()&&nr_temporal_status::gpu_adopted.load()&&
            nr_temporal_status::domain.load()==2,
            "actual Core PostRr temporal recordings have matching Execute/Signal and GPU history-adoption receipts");
    if(temporal_case==3)Require(!temporal_recorded&&!nr_temporal_status::recorded.load()&&
        !nr_temporal_status::submitted.load()&&!context.temporal,
        "rejected PostRr native MV creates no temporal owner or GPU work");
    if(private_sr)Require(nr_private_sr_status::creations.load()==1&&nr_private_sr_status::captures.load()==frame&&nr_private_sr_status::composed.load()==requested_frames-1&&nr_private_sr_status::adopted.load()==requested_frames-1,"distinct creation/record/submit counts");
    else Require(!context.private_sr&&!nr_private_sr_status::creations.load()&&!nr_private_sr_status::captures.load()&&!nr_private_sr_status::composed.load()&&!nr_private_sr_status::adopted.load(),"spatial-only control performs no private SR work");
    if(!nr_layers_count&&!rr)h.Pixels(final,.5f,L"actual-core-neutral.fp16",.04);
    const auto baseline=Bytes(h,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),output=Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::ofstream saved_base("actual-core-native-base.fp16",std::ios::binary);saved_base.write(baseline.data(),baseline.size());saved_base.close();
    Require(!baseline.empty(),"complete native B bytes");
    if(!nr_layers_count)Require(baseline==output,"zero edit preserves every native B byte exactly");
    else{chain.Verify(h,requested_frames);const auto z=Bytes(h,chain.c->image.result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Require(z!=baseline&&output!=baseline,"complete real NR Z and private reconstruction both retain nonzero full-chain contribution");
        std::ofstream saved_z("actual-core-final-Z.fp16",std::ios::binary);saved_z.write(z.data(),z.size());
        std::ofstream saved_output("actual-core-private-result.fp16",std::ios::binary);saved_output.write(output.data(),output.size());}
    if(private_sr){Require(nr_private_sr_status::timing_config.load()==g_config_generation.load()&&
        nr_private_sr_status::timing_instance.load()==context.instance_id,"production timing belongs to exact configuration and owner");
    Require(std::isfinite(nr_private_sr_status::gpu_first_create_ms.load())&&nr_private_sr_status::gpu_first_create_ms.load()>=0&&
        std::isfinite(nr_private_sr_status::gpu_first_inference_ms.load())&&nr_private_sr_status::gpu_first_inference_ms.load()>=0,"finite first creation/inference timings");
    const auto timing_samples=nr_private_sr_status::timing_samples.load();
    if(requested_frames>9){Require(nr_private_sr_status::gpu_timing_available.load()&&timing_samples==std::min(requested_frames-9,120u),"stable production timing window after eight private inferences");
        const double median=nr_private_sr_status::gpu_median_ms.load(),p95=nr_private_sr_status::gpu_p95_ms.load();
        Require(std::isfinite(median)&&std::isfinite(p95)&&median>=0&&p95>=median,"finite ordered production median/P95");}
    std::printf("METRIC timing_config=%llu timing_instance=%llu timing_samples=%llu first_create_ms=%.6f first_inference_ms=%.6f private_median_ms=%.6f private_p95_ms=%.6f\n",
        nr_private_sr_status::timing_config.load(),nr_private_sr_status::timing_instance.load(),timing_samples,
        nr_private_sr_status::gpu_first_create_ms.load(),nr_private_sr_status::gpu_first_inference_ms.load(),
        nr_private_sr_status::gpu_median_ms.load(),nr_private_sr_status::gpu_p95_ms.load());}
    ReleaseCom(timing);ResetAndClose(h); // end readback recording before releasing any referenced model resource
    const auto peak=nr_private_sr_status::peak.load();Require(nr_post_product::Destroy(context)&&!context.private_sr,"product original owner retirement");
    Require(nr_private_sr_status::bytes.load()==0,"owned plane account retired");
    g_config.temporal_backend=0;nr_temporal::Maintain(context);
    Require(!context.temporal,"completed PostRr temporal owner retired after exact recording and fence");
    standalone_guides.Destroy();if(chain.c)chain.Destroy();
    nr_private_sr_core::ForgetParent(parent);ForgetFeature(parent);Ngx(release(parent),"native parent Release");Ngx(destroy(params),"native parameters destroyed");
    const auto final_memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(h.device),true);
    std::printf("METRIC peak_plane_bytes=%llu retired_plane_bytes=%llu dxgi_before=%llu dxgi_after=%llu\n",peak,nr_private_sr_status::bytes.load(),(unsigned long long)initial_memory.local.CurrentUsage,(unsigned long long)final_memory.local.CurrentUsage);
    // Real core Shutdown export is forwarded exactly once and retires its domain.
    const auto epoch=nr_private_sr_core::DomainEpoch(h.device,ngx);Require(epoch!=0,"live domain before real Shutdown");
    auto shutdown=reinterpret_cast<NgxResult(*)(ID3D12Device*)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_Shutdown1"));
    Require(shutdown!=nullptr,"real device Shutdown export");Ngx(shutdown(native_device),"actual original device Shutdown");
    Require(nr_private_sr_core::DomainEpoch(h.device,ngx)==0,"Shutdown invalidated original domain");
    g_codec.device=nullptr;slot.source=slot.result=nullptr;
#if defined(NR_TEST_RESHADE_RUNTIME)
    // All product and NR fences/recordings are retired before proxy release.
    ReleaseCom(native_list);ReleaseCom(native_device);
#endif
    std::printf("PASS actual generated Core native=%u realNRLayers=%u percent=%u checks=%u privateCreate=%u privateEvaluate=%u submit=%u retire=1; artificial scene, no game\n",rr?13:1,nr_layers_count,percent,checks,unsigned(private_sr),private_sr?requested_frames-1:0,frame);return 0;
}
}
int wmain(int argc,wchar_t**argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc<6||argc>9)return 2;const unsigned frames=std::wcstoul(argv[5],nullptr,10);if(frames<2||frames>256)return 2;try{return product_real::Run(argv[1],std::wcstoul(argv[2],nullptr,10),std::wcstoul(argv[3],nullptr,10),std::wcstoul(argv[4],nullptr,10)!=0,frames,argc>=7&&std::wcstoul(argv[6],nullptr,10)!=0,argc<8||std::wcstoul(argv[7],nullptr,10)!=0,argc>=9?std::wcstoul(argv[8],nullptr,10):0);}
catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fflush(nullptr);ExitProcess(1);}}
