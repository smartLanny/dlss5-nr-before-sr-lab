// Included after the unmodified generated Core and the existing real-device Host.
// Controlled WARP fixture API; native metadata/CreateOwner/NGX are NOT tested here.
#include <chrono>
#include <fstream>
namespace product_warp {
using namespace sr_host;
NrContext context;
NrContext independent_recording_context;
void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* p){ID3D12CommandQueue_ExecuteCommandLists(q,n,p);}
HRESULT STDMETHODCALLTYPE Reset(ID3D12GraphicsCommandList* l,ID3D12CommandAllocator* a,ID3D12PipelineState* p){return ID3D12GraphicsCommandList_Reset(l,a,p);}
double Millis(std::chrono::steady_clock::time_point t){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
void Register(Host& h){
    CommandStateMetaContract meta;UINT meta_bytes=sizeof(meta);
    if(FAILED(ID3D12Device_GetPrivateData(h.device,kCommandStateMetaContract,&meta_bytes,&meta))||
       meta_bytes!=sizeof(meta)||!meta.device_identity){
        meta=CommandStateMetaContract{};meta.device_identity=g_next_command_device_identity.fetch_add(1);
        Hr(ID3D12Device_SetPrivateData(h.device,kCommandStateMetaContract,sizeof(meta),&meta),"device creation identity");
    }
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

struct Params {
    NgxParams base{};void* table[17]{};float jitter_x=0,jitter_y=0;
    static NgxResult Float(const NgxParams* p,const char* k,float* v){
        const auto& self=*reinterpret_cast<const Params*>(p);
        *v=std::strcmp(k,"Jitter.Offset.X")==0?self.jitter_x:std::strcmp(k,"Jitter.Offset.Y")==0?self.jitter_y:1;return 1;}
    static NgxResult Int(const NgxParams*,const char*,int* v){*v=1;return 1;}
    static NgxResult UInt(const NgxParams*,const char*,unsigned* v){*v=0;return 0;}
    static NgxResult Resource(const NgxParams*,const char*,ID3D12Resource** v){*v=nullptr;return 0;}
    Params(){base.vtable=table;table[9]=(void*)Resource;table[11]=(void*)Int;table[12]=(void*)UInt;table[14]=(void*)Float;}
};
struct Block {ID3D12Resource* output=nullptr;};
struct Fixture {
    Host& h;ID3D12Resource* carrier;unsigned creates=0,evaluates=0,releases=0;
    nr_private_sr::Api Api(){nr_private_sr::Api a{};a.user=this;a.original_sr_owner_verified=true;a.recording_released=nr_post_product::RecordingReleased;
        a.allocate=[](void*,void** p){*p=new Block;return 1u;};a.destroy=[](void*,void* p){delete (Block*)p;return 1u;};
        a.set_u=[](void*,void*,const char*,unsigned){};a.set_i=[](void*,void*,const char*,int){};a.set_f=[](void*,void*,const char*,float){};
        a.set_r=[](void*,void* p,const char* k,void* v){if(std::strcmp(k,"Output")==0)((Block*)p)->output=(ID3D12Resource*)v;};
        a.create=[](void* u,void*,int f,void*,void** p){auto& t=*(Fixture*)u;Require(f==1,"fixture Feature1");++t.creates;*p=u;return 1u;};
        a.evaluate=[](void* u,void*,void*,void* p){auto& t=*(Fixture*)u;++t.evaluates;auto* output=((Block*)p)->output;
            t.h.Barrier(t.carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
            t.h.Barrier(output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyResource(t.h.list,output,t.carrier);
            t.h.Barrier(output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            t.h.Barrier(t.carrier,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);return 1u;};
        a.release=[](void* u,void*){++((Fixture*)u)->releases;return 1u;};
        a.retain_resource=[](void*,void* p){ID3D12Resource_AddRef((ID3D12Resource*)p);};a.release_resource=[](void*,void* p){ID3D12Resource_Release((ID3D12Resource*)p);};
        a.retain_fence=[](void*,uint64_t p){ID3D12Fence_AddRef((ID3D12Fence*)p);};a.release_fence=[](void*,uint64_t p){ID3D12Fence_Release((ID3D12Fence*)p);};
        a.poll=[](void*,nr_private_sr::Fence f){return nr_private_sr::Completion{f.queue,f.object,ID3D12Fence_GetCompletedValue((ID3D12Fence*)f.object)};};return a;
    }
};
void Install(Host& h,Fixture& fixture,LifecycleQueue* queue,unsigned nw,unsigned nh,unsigned ow,unsigned oh,unsigned flags=2){
    auto* o=new nr_post_product::Owner;context.private_sr=o;o->queue=queue;o->configuration=g_config_generation.load();
    o->shape={nw,nh,ow,oh,2,flags,(uint64_t)h.device,queue->identity,1};auto code=nr_private_sr_artifacts::Kernels();
    o->normalizer=nr_edit_dx12::Create(h.device,o->shape.device,o->shape.queue,code,nr_post_product::SameDevice);Require(o->normalizer,"fixture normalizer");
    nr_edit_dx12::SetRetirementGuard(o->normalizer,nr_post_product::RecordingReleased,nullptr);
    for(auto& f:o->flights)for(unsigned n=0;n<2;++n){uint64_t bytes=0;auto*& plane=n?f.motion:f.depth;
        plane=nr_edit_dx12::CreatePlane(h.device,nw,nh,n?16:41,128ull<<20,&bytes);Require(plane,"fixture guides");o->bytes+=bytes;}
    uint64_t bytes=0;o->backend=nr_post_private_sr_dx12::Create(h.device,fixture.Api(),o->shape,code,nr_post_product::SameDevice,128ull<<20,&bytes);
    Require(o->backend,"fixture backend");o->bytes+=bytes;nr_private_sr_status::Allocated(o->bytes);
}

void Fill(Host& h,ID3D12Resource* texture,D3D12_RESOURCE_STATES state,float value,unsigned origin_x=0,unsigned origin_y=0){
    const auto d=ID3D12Resource_GetDesc(texture);D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    ID3D12Device_GetCopyableFootprints(h.device,&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto* upload=h.Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);
    char* mapped=nullptr;Hr(ID3D12Resource_Map(upload,0,nullptr,(void**)&mapped),"fill map");
    int e=0;std::frexp(value,&e);--e;const unsigned short packed=value==0?0:unsigned(e+15)*1024+unsigned(std::nearbyint(std::ldexp(value,10-e)))-1024;
    for(unsigned y=0;y<d.Height;++y)for(unsigned x=0;x<d.Width;++x){const unsigned short rgb=x<origin_x||y<origin_y?0x3400:packed;unsigned short pixel[]={rgb,rgb,rgb,0x3c00};std::memcpy(mapped+y*fp.Footprint.RowPitch+x*8,pixel,8);}
    ID3D12Resource_Unmap(upload,0,nullptr);h.Begin();h.Barrier(texture,state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(h.list,&to,0,0,0,&from,nullptr);h.Barrier(texture,D3D12_RESOURCE_STATE_COPY_DEST,state);h.Submit();h.Wait();
}
void Pixels(const std::vector<char>& bytes,float expected,unsigned width,unsigned origin_x,unsigned origin_y){
    Require(!bytes.empty(),"nonempty complete readback");
    for(size_t i=0;i<bytes.size();i+=2){unsigned short p=0;std::memcpy(&p,bytes.data()+i,2);unsigned e=(p>>10)&31,m=p&1023;
        const float v=(p&0x8000?-1.f:1.f)*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));
        const unsigned x=unsigned(i/8)%width,y=unsigned(i/8)/width;const float want=x<origin_x||y<origin_y?.25f:expected;
        const float expected_channel=(i/2)%4==3?1.f:want;
        if(v!=expected_channel)std::printf("PIXEL_MISMATCH x=%u y=%u channel=%u actual=%.9g expected=%.9g\n",x,y,unsigned((i/2)%4),v,expected_channel);
        Require(v==expected_channel,"whole image including atlas margin and alpha exact");}
}
HRESULT STDMETHODCALLTYPE FailedSignal(ID3D12CommandQueue*,ID3D12Fence*,UINT64){return E_FAIL;}
HRESULT STDMETHODCALLTYPE FailedReset(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*){return E_FAIL;}
Host* shutdown_host=nullptr;
NgxHandle* shutdown_parent=nullptr;
void DuringOriginalShutdown(){
    Params parameters;auto module=GetModuleHandleW(L"_nvngx.dll");
    const auto create=reinterpret_cast<nr_private_sr_core::Create>(GetProcAddress(module,"NVSDK_NGX_D3D12_CreateFeature"));
    Ngx(create(shutdown_host->list,13,&parameters.base,&shutdown_parent),"controlled reentrant create during original Shutdown");
    Require(!nr_private_sr_core::DomainEpoch(shutdown_host->device,module),"in-flight Shutdown cannot reopen factory domain");
}
int DomainCase(){
    InitializeCriticalSection(&g_chain_cs);Host h;Register(h);g_codec.device=h.device;
    auto module=LoadLibraryW(L"_nvngx.dll");Require(module,"controlled export fixture loaded");
    auto count=reinterpret_cast<unsigned(*)(unsigned)>(GetProcAddress(module,"FixtureCount"));Require(count,"fixture identity, not NVIDIA");
    Require(MH_Initialize()==MH_OK,"domain host hook initialization");
    auto* target=reinterpret_cast<void*>(GetProcAddress(module,"NVSDK_NGX_D3D12_CreateFeature"));bool queued=false;
    Require(QueueLateHook(NgxHookOperation::Create,target,reinterpret_cast<void*>(&HookCoreCreate),
        reinterpret_cast<void**>(&g_original_core_create),"controlled Core Create",queued),"original factory hook");
    Require(MH_ApplyQueued()==MH_OK,"publish controlled Create hook");MarkNgxHooksPublished(false);
    ScopedNgxLayer native_layer(NgxLayer::Core);Params parameters;NgxHandle* parent=nullptr;
    auto create=reinterpret_cast<nr_private_sr_core::Create>(target);
    Ngx(create(h.list,13,&parameters.base,&parent),"actual callback native RR-only Create");
    const auto f=FindFeature(parent);Require(f.handle==parent&&f.feature_id==13&&f.generation&&!f.pending,"actual RR parent generation");
    Require(count(3)==0&&count(4)==1,"no SR parent / exactly one native RR create");
    nr_private_sr_core::Exports e;Require(nr_private_sr_core::ResolveExports(e,h.device,parent,f.generation),"RR-only original factory admitted");
    auto api=nr_private_sr_core::MakeApi(e);Require(api.Valid(),"domain API complete");void* p=nullptr;void* private_handle=nullptr;
    NrPostBoundaryAliasScope original_call(h.list);
    Require(NrPrivateOriginalCallList(h.list)==h.list,"current native interface proof");
    Ngx(api.allocate(api.user,&p),"private original allocate");Ngx(api.create(api.user,h.list,1,p,&private_handle),"private Feature1 through real Core hook TLS bypass");
    Require(count(3)==1&&count(4)==1&&!FindFeature(static_cast<NgxHandle*>(private_handle)).handle,"private handle not observed as game feature");
    Ngx(api.evaluate(api.user,h.list,private_handle,p),"private original Evaluate");Ngx(api.release(api.user,private_handle),"private original Release");
    Ngx(api.destroy(api.user,p),"private original DestroyParameters");
    const auto old_epoch=e.domain_epoch;const auto allocations=count(0);
    auto shutdown=reinterpret_cast<NgxResult(*)(ID3D12Device*)>(GetProcAddress(module,"NVSDK_NGX_D3D12_Shutdown1"));
    auto set_shutdown_callback=reinterpret_cast<void(*)(void(*)())>(GetProcAddress(module,"FixtureShutdownCallback"));
    Require(set_shutdown_callback,"controlled Shutdown callback available");shutdown_host=&h;
    set_shutdown_callback(&DuringOriginalShutdown);
    Ngx(shutdown(h.device),"actual original Shutdown1 callback");set_shutdown_callback(nullptr);
    Require(shutdown_parent&&!nr_private_sr_core::DomainEpoch(h.device,module),"reentrant successful Create has no surviving domain receipt");Require(count(7)==1&&!nr_private_sr_core::DomainAlive(e),"one forwarding and domain invalidation");
    p=nullptr;Require(api.allocate(api.user,&p)!=1&&!p&&count(0)==allocations,"invalidated domain never reaches old allocator");
    nr_private_sr_core::Exports stale;Require(!nr_private_sr_core::ResolveExports(stale,h.device,parent,f.generation),"stale RR generation cannot reopen domain");
    // A failed or unobserved create cannot establish a domain. A new successful
    // original create establishes a new epoch and handle generation only.
    nr_private_sr_core::ObserveCoreCreate(h.list,13,parent,0,nr_private_sr_core::shutdown_sequence.load());
    Require(!nr_private_sr_core::DomainEpoch(h.device,module),"failed original Create gives no receipt");
    NgxHandle* replacement=nullptr;Ngx(create(h.list,13,&parameters.base,&replacement),"new original RR generation");
    const auto next=FindFeature(replacement);Require(next.generation!=f.generation,"new generation");
    nr_private_sr_core::Exports renewed;Require(nr_private_sr_core::ResolveExports(renewed,h.device,replacement,next.generation)&&renewed.domain_epoch!=old_epoch,"new exact epoch");
    Require(!nr_private_sr_core::ResolveExports(stale,h.device,parent,f.generation),"old parent never borrows new epoch");
    nr_private_sr_core::ReleaseExports(renewed);nr_private_sr_core::ReleaseExports(e);
    auto shutdown_all=reinterpret_cast<NgxResult(*)()>(GetProcAddress(module,"NVSDK_NGX_D3D12_Shutdown"));
    Ngx(shutdown_all(),"original global Shutdown callback");Require(count(7)==2&&!nr_private_sr_core::DomainEpoch(h.device,module),"both Shutdown forms covered");
    std::printf("PASS controlled actual Core RR-only domain/TLS/Shutdown checks=%u; no NVIDIA NGX inference\n",checks);
    std::fflush(nullptr);ExitProcess(0);
}
int Run(const std::wstring& mode){if(mode==L"domain")return DomainCase();InitializeCriticalSection(&g_chain_cs);Host h;Register(h);Params parameters;auto* params=&parameters.base;
    NgxHandle parent{};FeatureRecord feature{};feature.feature_id=1;feature.generation=1;feature.create_flags=2;feature.create_flags_known=true;
    const bool hdr=mode==L"hdr-jitter"||mode==L"rr-jitter";
    if(hdr){feature.create_flags=75;feature.feature_id=mode==L"rr-jitter"?13:1;}
    const bool odd=mode==L"odd"||mode==L"odd-quarter"||hdr;const unsigned rw=odd?31:32,rh=odd?23:24,ow=odd?63:64,oh=odd?47:48,nw=mode==L"odd-quarter"?15:odd?21:mode==L"quarter"?16:mode==L"full"?64:32,nh=mode==L"odd-quarter"?11:odd?17:mode==L"quarter"?12:mode==L"full"?48:24;
    const unsigned gx=odd?2:0,gy=odd?1:0,tx=odd?3:0,ty=odd?2:0;const bool numeric=odd||mode==L"numeric"||mode==L"quarter"||mode==L"full";
    g_codec.device=h.device;g_config.enabled=true;g_enabled_fast.store(true);g_config.private_residual_sr=true;g_config.temporal_backend=0;
    g_config.processing_start=nr_processing::Start::After;nr_processing_runtime::Reconcile();
    auto* depth=h.Plane(rw+gx,rh+gy,DXGI_FORMAT_R32_FLOAT,false);auto* motion=h.Plane(rw+gx,rh+gy,DXGI_FORMAT_R32G32_FLOAT,false);
    auto* base=h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,false);auto* final=h.Plane(ow+tx,oh+ty,DXGI_FORMAT_R16G16B16A16_FLOAT,true);
    Fixture fixture{h,h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,false)};
    auto* second_z=mode==L"order"?h.Plane(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true,.625f):nullptr;
    auto* queue=FindOrCreateLifecycleQueueLocked(h.queue);Require(queue&&queue->fence,"Core queue");
    for(unsigned n=0;n<kRequiredStableQueueSubmissions;++n){Require(RememberLastSubmissionQueue(h.list,queue->identity),"queue observation");ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};Execute(h.queue,1,lists);}
    Hr(ID3D12CommandQueue_Signal(h.queue,h.fence,++h.value),"prime");h.Wait();Install(h,fixture,queue,nw,nh,ow,oh,hdr?10:2);
    context.instance_id=1;context.feature_generation=1;auto& slot=context.slots[0];slot.source=base;slot.result=final;
    slot.source_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;slot.result_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    slot.snapshots_native_output=true;slot.result_format=slot.result_uav_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    NrGuideBinding guides{};guides.depth=depth;guides.motion=motion;auto& sig=guides.signature;sig.depth_inverted=hdr?1:0;
    sig.depth_format=41;sig.motion_format=16;sig.depth_physical_width=rw+gx;sig.depth_width=rw;sig.depth_physical_height=rh+gy;sig.depth_height=rh;sig.depth_x=gx;sig.depth_y=gy;
    sig.motion_physical_width=rw+gx;sig.motion_source_width=rw;sig.motion_physical_height=rh+gy;sig.motion_source_height=rh;sig.motion_x=gx;sig.motion_y=gy;
    const unsigned frames=numeric?7:3;const float edits[]={.25f,.25f,-.25f,0,1.f/1024.f,-1.f/2048.f,.125f+.25f};
    for(unsigned frame=0;frame<frames;++frame){const float hdr_edits[]={1,1,-1,0,1.f/128,-1.f/256,1.5f};
        const float edit=hdr?hdr_edits[frame]:numeric?edits[frame]:0,base_value=hdr?std::ldexp(1.f,2+int(frame%2)):.5f;
        if(hdr)Fill(h,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,base_value);
        if(hdr){parameters.jitter_x=frame%2?.5f:-.5f;parameters.jitter_y=frame%3?.25f:-.25f;}
        Fill(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,base_value+edit,tx,ty);
        // The controlled Feature1 output is an identity upscale of the actual post-only range-1 carrier.
        // Legacy range-4 half encoding rounds these two tiny edits to neutral; their entire signal is in the tail.
        const bool legacy_neutral=edit==1.f/1024.f||edit==-1.f/2048.f;
        const float carrier_value=legacy_neutral?.5f:.5f+std::clamp(edit,-1.f,1.f)/2.f;
        Fill(h,fixture.carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,carrier_value);
        Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"Core begin Reset");h.released=false;
        {NrPostBoundaryAliasScope original_call(h.list);nr_source_runtime::EvaluateScope source(h.list,&parent,params,feature);Require(bool(nr_source_runtime::CurrentSource()),"actual source scope");
            Require(RecordNrUseLocked(h.list,context,slot),"Core resource lease");
            const auto capture=nr_post_product::Capture(h.list,h.device,context,&parent,params,feature,guides,CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);
            if(!capture)std::printf("capture refused %s guide=%u\n",nr_private_sr_status::Name(nr_private_sr_status::state.load()),nr_private_sr_status::guide_reason.load());
            Require(capture,"actual Capture");
            if(mode!=L"guide-only")Require(nr_post_product::Finish(h.list,context,slot,feature,capture,ow,oh,tx,ty),"actual Finish");
            source.Finish(mode==L"guide-only"?0:1);}
        if(mode==L"order"&&frame==1){
            auto* owner=context.private_sr;const auto last_source=owner->last_source,last_capture=owner->last_capture;
            const auto captures=nr_private_sr_status::captures.load(),adopted=nr_private_sr_status::adopted.load();const auto evaluates=fixture.evaluates;
            auto* first_list=h.list;auto* first_allocator=h.allocator;ID3D12CommandAllocator* second_allocator=nullptr;ID3D12GraphicsCommandList* second=nullptr;
            Hr(ID3D12Device_CreateCommandAllocator(h.device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)&second_allocator),"second allocator");
            Hr(ID3D12Device_CreateCommandList(h.device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,second_allocator,nullptr,IID_ID3D12GraphicsCommandList,(void**)&second),"second actual list");
            Hr(ID3D12GraphicsCommandList_Close(second),"second prime close");h.list=second;h.allocator=second_allocator;Register(h);
            for(unsigned n=0;n<kRequiredStableQueueSubmissions;++n){Require(RememberLastSubmissionQueue(second,queue->identity),"second stable queue");ID3D12CommandList* lists[]={(ID3D12CommandList*)second};Execute(h.queue,1,lists);}
            Hr(ID3D12CommandQueue_Signal(h.queue,h.fence,++h.value),"second prime fence");h.Wait();
            Hr(ProcessCommandListReset(0,second,second_allocator,nullptr),"second actual begin Reset");h.released=false;
            auto& second_slot=independent_recording_context.slots[0];second_slot.source=base;second_slot.result=second_z;
            {NrPostBoundaryAliasScope original_call(second);nr_source_runtime::EvaluateScope source(second,&parent,params,feature);Require(bool(nr_source_runtime::CurrentSource()),"second source scope");
                Require(!RecordNrUseLocked(second,context,second_slot),"Core rejects second context use before private Capture");
                // Exercise the additional private admission seam explicitly:
                // normal routing already bypassed above. Obtain a real empty
                // Core recording token from an independent resource lease; do not
                // fabricate successful second use of the blocked context.
                Require(RecordNrUseLocked(second,independent_recording_context,second_slot),"independent Core resource lease for direct private Busy seam");
                const auto capture=nr_post_product::Capture(second,h.device,context,&parent,params,feature,guides,CodecColorMode::SrgbEncodedSdr,rw,rh,nw,nh,ow,oh);
                Require(!capture&&nr_private_sr_status::state.load()==nr_private_sr_status::State::Busy,"unsubmitted predecessor blocks second actual Capture");
                Require(owner->last_source==last_source&&owner->last_capture==last_capture&&fixture.evaluates==evaluates&&nr_private_sr_status::captures.load()==captures,"Busy does not advance source or private history");source.Finish(1);}
            Hr(ID3D12GraphicsCommandList_Close(second),"second close");ID3D12CommandList* second_lists[]={(ID3D12CommandList*)second};ProcessSubmittedCommandLists(0,h.queue,1,second_lists);
            Require(!owner->poisoned&&nr_private_sr_status::adopted.load()==adopted&&!nr_post_product::Ready(context),"reverse first submission cannot adopt predecessor or retire it");
            h.list=first_list;h.allocator=first_allocator;Hr(ID3D12GraphicsCommandList_Close(first_list),"first deferred close");
            ID3D12CommandList* first_lists[]={(ID3D12CommandList*)first_list};ProcessSubmittedCommandLists(0,h.queue,1,first_lists);Complete(h,queue);
            Require(!owner->poisoned&&nr_private_sr_status::adopted.load()==adopted+1&&!nr_post_product::Ready(context),"only actual first flight adopts after its own submission");
            Hr(ProcessCommandListReset(0,second,second_allocator,nullptr),"second retirement Reset");Hr(ID3D12GraphicsCommandList_Close(second),"second retirement close");
            ResetAndClose(h);Require(nr_post_product::Ready(context),"both submitted lists safely released");
            Pixels(Bytes(h,second_z,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),.625f,ow,0,0);
            Require(fixture.evaluates==1&&nr_private_sr_status::adopted.load()==1,"exact one private inference/adoption across reverse submissions");
            Require(nr_post_product::Destroy(context)&&fixture.releases==1,"ordered owner retirement");ReleaseCom(second);ReleaseCom(second_allocator);
            g_codec.device=nullptr;slot.source=slot.result=second_slot.source=second_slot.result=nullptr;
            std::printf("PASS actual Core reverse submission Busy/Z/history/lifetime checks=%u; fixture API only\n",checks);return 0;
        }
        if(mode==L"signal-fail"){
            Hr(ID3D12GraphicsCommandList_Close(h.list),"signal failure list close");ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};
            // Fixture-only per-object dispatch table: all native methods stay
            // intact except Signal's controlled E_FAIL. Restore before checks.
            auto* original=h.queue->lpVtbl;auto injected=*original;injected.Signal=FailedSignal;h.queue->lpVtbl=&injected;
            ProcessSubmittedCommandLists(0,h.queue,1,lists);h.queue->lpVtbl=original;
            auto* owner=context.private_sr;Require(owner&&owner->poisoned&&g_lifecycle_tracking_uncertain.load(),"actual Core Signal HRESULT failure poisons owner");
            Require(nr_private_sr_status::adopted.load()==0&&nr_private_sr_status::submitted.load()==0&&!nr_post_product::Destroy(context)&&fixture.releases==0,"unproven execution cannot adopt or release private references");
            Hr(ID3D12CommandQueue_Signal(h.queue,h.fence,++h.value),"restored native Signal drains host only");
            std::printf("PASS actual Core injected Signal E_FAIL checks=%u; not a driver-generated fault, quarantine retained\n",checks);ExitProcess(0);
        }
        if(mode==L"discard"){
            auto* owner=context.private_sr;const auto before=nr_private_sr_status::bytes.load();Hr(ID3D12GraphicsCommandList_Close(h.list),"unsubmitted close");Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"actual unsubmitted Reset");
            Require(context.private_sr==owner&&owner->poisoned&&g_lifecycle_tracking_uncertain.load(),"discard quarantines exact owner");
            Require(!nr_post_product::Destroy(context)&&nr_private_sr_status::bytes.load()==before&&fixture.releases==0,"discard retains uncertain resources");
            std::printf("PASS actual Core unsubmitted discard checks=%u; quarantine retained until process exit\n",checks);ExitProcess(0);
        }
        Hr(ID3D12GraphicsCommandList_Close(h.list),"close");ID3D12CommandList* lists[]={(ID3D12CommandList*)h.list};if(mode==L"pending")Hr(ID3D12CommandQueue_Wait(h.queue,h.fence,h.value+1),"blocked exact GPU fence");
        ProcessSubmittedCommandLists(0,h.queue,1,lists);
        Require(!g_lifecycle_tracking_uncertain.load()&&context.completion_fence_value,"Core submit");
        if(mode==L"pending"){
            auto* owner=context.private_sr;const auto recording=owner->flights[0].recording;
            Hr(ProcessCommandListReset(0,h.list,h.allocator,nullptr),"release recording while GPU queue blocked");Hr(ID3D12GraphicsCommandList_Close(h.list),"detached list close");
            Require(nr_post_product::RecordingReleased(nullptr,recording),"actual Reset releases recording before fence");
            Require(!nr_post_product::Ready(context)&&context.private_sr==owner,"recording release alone cannot retire GPU resources");
            Hr(ID3D12Fence_Signal(h.fence,++h.value),"unblock actual GPU queue");Complete(h,queue);
            Require(nr_post_product::Ready(context),"completion plus earlier Reset permits retirement");h.released=true;
            Require(nr_post_product::Destroy(context),"pending owner retired");std::printf("PASS actual Core early Reset plus delayed fence checks=%u\n",checks);g_codec.device=nullptr;slot.source=slot.result=nullptr;return 0;
        }
        Complete(h,queue);
        if(mode==L"duplicate"){
            auto* owner=context.private_sr;ProcessSubmittedCommandLists(0,h.queue,1,lists);
            Require(owner->poisoned&&g_lifecycle_tracking_uncertain.load()&&!nr_post_product::Destroy(context)&&fixture.releases==0,"actual replay submission quarantines without release");
            std::printf("PASS actual Core duplicate submit checks=%u; quarantine retained until process exit\n",checks);ExitProcess(0);
        }
        if(mode==L"reset-fail"){
            const auto recording=context.private_sr->flights[0].recording;g_original_command_list_reset[0]=FailedReset;
            Require(FAILED(ProcessCommandListReset(0,h.list,h.allocator,nullptr)),"original Reset failure propagated");
            Require(!nr_post_product::RecordingReleased(nullptr,recording)&&!nr_post_product::Ready(context)&&!context.private_sr->poisoned,"failed Reset retains completed recording without false poison");g_original_command_list_reset[0]=Reset;
        }
        Require(!nr_post_product::Ready(context),"fence alone cannot retire");
        if(mode==L"off"){
            auto* owner=context.private_sr;g_config.private_residual_sr=false;nr_post_product::Maintain(context);
            Require(context.private_sr==owner&&owner->retiring&&fixture.releases==0,"disabled retains replayable recording");
            ResetAndClose(h);nr_post_product::Maintain(context);Require(!context.private_sr&&nr_private_sr_status::bytes.load()==0&&fixture.releases==1,"disabled drains after exact fence and Reset");
            std::printf("PASS actual Core disabled retirement checks=%u\n",checks);g_codec.device=nullptr;slot.source=slot.result=nullptr;return 0;
        }
        ResetAndClose(h);Require(nr_post_product::Ready(context),"fence and recording release");
        Require(nr_private_sr_status::composed.load()==(mode==L"guide-only"?0:frame),"first create preserves Z; later composes once");
        Pixels(Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),base_value+edit,ow+tx,tx,ty);
        std::printf("CASE frame=%u edit=%.9f composed=%llu guide_only=%u\n",frame,edit,nr_private_sr_status::composed.load(),mode==L"guide-only");}
    Require(fixture.creates==(mode==L"guide-only"?0:1)&&fixture.evaluates==(mode==L"guide-only"?0:frames-1),"controlled fixture call counts");
    if(!numeric)Require(Bytes(h,base,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)==Bytes(h,final,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),"zero edit exact bytes");
    Require(nr_post_product::Destroy(context),"actual owner Destroy");Require(fixture.releases==(mode==L"guide-only"?0:1),"one fixture release");g_codec.device=nullptr;slot.source=slot.result=nullptr;
    std::printf("PASS actual Core controlled fixture checks=%u; no real NGX/game\n",checks);return 0;
}
}
int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);try{return product_warp::Run(argc>1?argv[1]:L"neutral");}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());ExitProcess(1);}}
