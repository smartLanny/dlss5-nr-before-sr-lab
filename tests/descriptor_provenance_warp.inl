// Complete production Core plus real D3D12 WARP objects/native MinHook calls.
// Only the NGX callback is a scripted success containing real ClearUAV calls.
// No NVIDIA library, game, NR model, or permission/admission override is used.
#include <vector>
#include "native_observer_device.inl"
namespace descriptor_warp_test {
using queue_test::Check;
using queue_test::Hr;
struct Params {
    NgxParams base{};void *table[17]{};ID3D12Resource *output=nullptr;
    static NgxResult Get(const NgxParams *base,const char *key,ID3D12Resource **out) {
        const auto *self=reinterpret_cast<const Params *>(base);
        *out=std::strcmp(key,"Output")==0?self->output:nullptr;return *out?kNgxSuccess:0;
    }
    explicit Params(ID3D12Resource *value):output(value) {base.vtable=table;table[9]=reinterpret_cast<void *>(&Get);}
};
struct Texture {
    ID3D12Resource *resource=nullptr;
    ~Texture(){ReleaseCom(resource);}
};
void MakeTexture(queue_test::Fixture &f,Texture &texture,D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=16;desc.Height=12;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void **>(&texture.resource)),"descriptor texture");
}
ID3D12DescriptorHeap *Heap(queue_test::Fixture &f,D3D12_DESCRIPTOR_HEAP_TYPE type,bool visible,unsigned count=4) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=type;desc.NumDescriptors=count;
    desc.Flags=visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap *heap=nullptr;
    Hr(ID3D12Device_CreateDescriptorHeap(f.device,&desc,IID_ID3D12DescriptorHeap,reinterpret_cast<void **>(&heap)),"descriptor heap");
    return heap;
}
D3D12_CPU_DESCRIPTOR_HANDLE Cpu(queue_test::Fixture &f,ID3D12DescriptorHeap *heap,unsigned index=0) {
    auto handle=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    handle.ptr+=index*ID3D12Device_GetDescriptorHandleIncrementSize(f.device,ID3D12DescriptorHeap_GetDesc(heap).Type);return handle;
}
D3D12_GPU_DESCRIPTOR_HANDLE Gpu(queue_test::Fixture &f,ID3D12DescriptorHeap *heap,unsigned index=0) {
    auto handle=ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    handle.ptr+=index*ID3D12Device_GetDescriptorHandleIncrementSize(f.device,ID3D12DescriptorHeap_GetDesc(heap).Type);return handle;
}
void Uav(queue_test::Fixture &f,ID3D12Resource *resource,D3D12_CPU_DESCRIPTOR_HANDLE destination,
         DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=format;desc.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(f.device,resource,nullptr,&desc,destination);
}
void Rtv(queue_test::Fixture &f,ID3D12Resource *resource,D3D12_CPU_DESCRIPTOR_HANDLE destination) {
    D3D12_RENDER_TARGET_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateRenderTargetView(f.device,resource,&desc,destination);
}
void InstallCommandObservation(queue_test::Fixture &f) {
    reshade::api::NativeObserverTestDevice owner(f.device);
    OnNrFormatInitDevice(&owner);
    Check(NrDescriptorObserveDeviceLayer(f.device),"actual init-device callback registers canonical native collector");
    // Actual capability receipts and actual native Create/setter hooks, matching
    // the existing runtime harness bootstrap. No state is marked known here.
    CommandStateMetaContract meta;ID3D12Device5 *extended=nullptr;
    const auto query=ID3D12Device_QueryInterface(f.device,IID_ID3D12Device5,reinterpret_cast<void **>(&extended));
    if(query==E_NOINTERFACE)meta.known=true;
    else {
        Hr(query,"descriptor meta-command interface");UINT count=0;
        Hr(ID3D12Device5_EnumerateMetaCommands(extended,&count,nullptr),"descriptor actual meta-command count");
        Check(count<=256,"bounded actual capability enumeration");
        std::vector<D3D12_META_COMMAND_DESC> values(count);UINT actual=count;
        if(count)Hr(ID3D12Device5_EnumerateMetaCommands(extended,&actual,values.data()),"descriptor actual capabilities");
        Check(actual<=count,"stable actual capability count");meta.known=true;
        for(UINT i=0;i<actual;++i){meta.initialization_dirty|=values[i].InitializationDirtyState;meta.execution_dirty|=values[i].ExecutionDirtyState;}
        ReleaseCom(extended);
    }
    meta.device_identity=g_next_command_device_identity.fetch_add(1);
    Hr(ID3D12Device_SetPrivateData(f.device,kCommandStateMetaContract,sizeof(meta),&meta),"descriptor device receipt");
    EnsureCommandCreationObserver(f.device);
    Check(EnsureNativeCommandSignatureHook(f.device),"production native signature observer");
    queue_test::List warm(f);auto state=std::make_unique<NativeCommandState>();
    Check(!CaptureNativeCommandState(warm.native,*state),"warm unobserved recording stays unknown");
    const auto *record=FindCommandStateTokenLocked(ReadCommandStateToken(warm.native));
    Check(record&&record->hooks_ready,"actual capture installs native setter family");
    ReleaseNativeCommandState(*state);
    NrPrepareFormatObservation(warm.native);
    Check(NrDescriptorDeviceCoverage(f.device).ready&&NrDescriptorCommandHooksReady(warm.native),"device and clear observation hooks installed");
    warm.Close();
}
struct Pipeline {
    ID3D12RootSignature *root=nullptr;ID3D12PipelineState *state=nullptr;
    explicit Pipeline(queue_test::Fixture &f) {
        D3D12_ROOT_SIGNATURE_DESC desc{};ID3DBlob *blob=nullptr,*error=nullptr;
        Hr(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"empty real root signature");
        ReleaseCom(error);
        Hr(ID3D12Device_CreateRootSignature(f.device,0,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
            IID_ID3D12RootSignature,reinterpret_cast<void **>(&root)),"real root object");ReleaseCom(blob);
        const char code[]="[numthreads(1,1,1)] void main() {}";
        Hr(D3DCompile(code,sizeof(code)-1,"descriptor-fixture",nullptr,nullptr,"main","cs_5_0",0,0,&blob,&error),"minimal real PSO bytecode");
        ReleaseCom(error);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root;
        pipeline.CS={ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob)};
        Hr(ID3D12Device_CreateComputePipelineState(f.device,&pipeline,IID_ID3D12PipelineState,
            reinterpret_cast<void **>(&state)),"minimal real compute PSO");ReleaseCom(blob);
    }
    ~Pipeline(){ReleaseCom(state);ReleaseCom(root);}
    void Bind(queue_test::List &list,ID3D12DescriptorHeap *heap) {
        ID3D12GraphicsCommandList_SetComputeRootSignature(list.native,root);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list.native,1,&heap);
        ID3D12GraphicsCommandList_SetPipelineState(list.native,state);
        Check(NrDescriptorBoundHeap(list.native)==g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(heap)).id,
            "real observed setter binds the exact live GPU heap");
    }
};
template<class Function> NgxResult Native(queue_test::List &list,ID3D12Resource *output,
        std::uint64_t feature,Function function,NgxResult result=kNgxSuccess) {
    Params params(output);NgxHandle handle{991};unsigned callback=717,calls=0;
    const auto original=[&](ID3D12GraphicsCommandList *l,const NgxHandle *h,const NgxParams *p,void *c) {
        Check(l==list.native&&h==&handle&&p==&params.base&&c==&callback,"native wrapper preserves arguments");
        ++calls;function();return result;
    };
    g_inside_ngx_evaluate=true;
    const auto actual=NrCallNativeWithFormatObservation(original,list.native,&handle,&params.base,&callback,feature);
    g_inside_ngx_evaluate=false;
    Check(calls==1&&actual==result,"native callback is forwarded exactly once with unchanged result");return actual;
}
void Barrier(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
}
void ReadPixels(queue_test::Fixture &f,ID3D12Resource *resource) {
    const auto desc=ID3D12Resource_GetDesc(resource);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
    ID3D12Device_GetCopyableFootprints(f.device,&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ID3D12Resource *readback=nullptr;
    Hr(ID3D12Device_CreateCommittedResource(f.device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&readback)),"descriptor readback");
    queue_test::List read(f);
    Barrier(read.native,resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{},destination{};
    source.pResource=resource;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource=readback;destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(read.native,&destination,0,0,0,&source,nullptr);
    read.Close();read.Submit(f.a);f.Drain(f.a);
    D3D12_RANGE range{0,static_cast<SIZE_T>(total)};void *bytes=nullptr;
    Hr(ID3D12Resource_Map(readback,0,&range,&bytes),"descriptor readback map");
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        const auto *pixel=static_cast<const unsigned char *>(bytes)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
        const bool red=x>=2&&x<6&&y>=3&&y<8;
        Check(pixel[0]==(red?255:0)&&pixel[1]==0&&pixel[2]==(red?0:255)&&pixel[3]==255,
            "real GPU native UAV clear and later exact RTV rectangle both reached texture storage");
    }
    D3D12_RANGE none{};ID3D12Resource_Unmap(readback,0,&none);ReleaseCom(readback);
}
void Run() {
    queue_test::Fixture f;g_config_generation.store(9);
    Texture output,other;MakeTexture(f,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);MakeTexture(f,other,D3D12_RESOURCE_STATE_RENDER_TARGET);
    // This heap and view predate all observers. Their valid native contents must
    // remain unknown to provenance even when copied through a later hook.
    auto *unobserved=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,false,1);
    Uav(f,output.resource,Cpu(f,unobserved));
    InstallCommandObservation(f);Pipeline pipeline(f);
    auto *cpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,false);
    auto *gpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true);
    auto *rtv=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_RTV,false);
    Uav(f,output.resource,Cpu(f,cpu,0));Uav(f,other.resource,Cpu(f,cpu,1));
    Rtv(f,output.resource,Cpu(f,rtv,0));Rtv(f,other.resource,Cpu(f,rtv,1));
    D3D12_CPU_DESCRIPTOR_HANDLE destinations[]={Cpu(f,gpu,0),Cpu(f,gpu,1)};
    D3D12_CPU_DESCRIPTOR_HANDLE sources[]={Cpu(f,cpu,0),Cpu(f,cpu,1)};
    ID3D12Device_CopyDescriptors(f.device,2,destinations,nullptr,2,sources,nullptr,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Check(g_nr_descriptors.Gpu(Gpu(f,gpu).ptr,0).view.resource.address==reinterpret_cast<std::uint64_t>(output.resource),
        "real CopyDescriptors resolves sparse source and destination ranges");
    ID3D12Device_CopyDescriptorsSimple(f.device,1,Cpu(f,gpu,1),Cpu(f,unobserved),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Check(!g_nr_descriptors.Gpu(Gpu(f,gpu,1).ptr,0),"real copy from pre-observer heap invalidates destination provenance");
    ID3D12Device_CopyDescriptorsSimple(f.device,1,Cpu(f,gpu),Cpu(f,cpu),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    queue_test::List list(f);pipeline.Bind(list,gpu);
    Native(list,output.resource,501,[]{});
    Check(!SnapshotNrDescriptorUse().clear_calls&&!SnapshotNrDescriptorUse().observed_typed,
        "cached UNORM creation without explicit native use supplies no typed-use evidence");
    const auto coverage=EnsurePostBoundaryNativeHooks(list.native);
    Check(!(coverage&nr_post_boundary::HookMissing),"real native output operation hooks are complete");
    NrNativeOutputTraceScope native_trace(output.resource,list.native,coverage);
    const FLOAT blue[]={0,0,1,1};
    Native(list,output.resource,502,[&] {
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list.native,Gpu(f,gpu),Cpu(f,cpu),output.resource,blue,0,nullptr);
    });
    const auto native=native_trace.Finish();
    auto use=SnapshotNrDescriptorUse();
    Check(use.clear_calls==1&&use.matches==1&&use.observed_typed==28&&use.after_native_valid,
        "cached CPU/GPU descriptors plus actual native clear produce one current read-only receipt");
    Check(native.count==1&&native.events[0].kind==nr_post_boundary::Kind::ClearUav&&native.events[0].clear_kind==1,
        "native full-view clear retains exact output descriptor identity and extent");
    const auto ledger=NrFormatEvidence(NrFormatKey(f.device,output.resource,502,true));
    Check(!ledger.view.format&&!ledger.Resolved(),"cached descriptor-use receipt never promotes the admission typed ledger");
    NrDiagnosticIdentity identity{};identity.frame_id=701;identity.config_generation=9;
    const auto trace=PublishPostBoundary(output.resource,native,identity,502,reinterpret_cast<std::uint64_t>(f.device));
    Barrier(list.native,output.resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto other_rtv=Cpu(f,rtv,1);const FLOAT green[]={0,1,0,1};
    const auto before=g_post_boundary_current.count;
    ID3D12GraphicsCommandList_OMSetRenderTargets(list.native,1,&other_rtv,FALSE,nullptr);
    ID3D12GraphicsCommandList_ClearRenderTargetView(list.native,other_rtv,green,0,nullptr);
    Check(g_post_boundary_current.count==before&&g_post_boundary_current.other_rtv_binds==1&&g_post_boundary_current.other_rtv_clears==1,
        "known-other RTV bind/clear has separate evidence and never becomes Output");
    auto output_rtv=Cpu(f,rtv);D3D12_RECT rect{2,3,6,8};const FLOAT red[]={1,0,0,1};
    ID3D12GraphicsCommandList_OMSetRenderTargets(list.native,1,&output_rtv,FALSE,nullptr);
    ID3D12GraphicsCommandList_ClearRenderTargetView(list.native,output_rtv,red,1,&rect);
    const auto &last=g_post_boundary_current.events[g_post_boundary_current.count-1];
    Check(last.kind==nr_post_boundary::Kind::ClearRtv&&last.target_known&&last.writes&&last.descriptor_format==28&&
        last.clear_kind==2&&last.clear_rect_count==1&&last.clear_bounds[0]==2&&last.clear_bounds[3]==8,
        "real Output RTV clear records exact bounded rectangle separately from native use");
    queue_test::List empty(f);empty.Close();list.Close();
    EnterCriticalSection(&g_chain_cs);const auto *queue=TrackGraphicsQueueLocked(f.a);LeaveCriticalSection(&g_chain_cs);
    Check(queue&&queue->identity,"real submission queue has lifetime identity");
    ID3D12CommandList *batch[]={empty.Base(),list.Base()};ProcessSubmittedCommandLists(0,f.a,2,batch);f.Drain(f.a);
    use=SnapshotNrDescriptorUse();
    Check(use.submission_seen&&use.at_submit_valid&&use.execute_returned&&use.submission_index==1&&use.submission_count==2,
        "descriptor revisions revalidated at real Execute entry with exact array position");
    Check(g_post_boundary_current.origin_submitted&&g_post_boundary_current.origin_submission_index==1&&
        g_post_boundary_current.origin_submission_count==2,"origin window keeps actual Execute array index/count");
    for(unsigned i=0;i<g_post_boundary_current.count;++i)
        Check(g_post_boundary_current.events[i].submitted&&g_post_boundary_current.events[i].submission_index==1&&
            g_post_boundary_current.events[i].submission_count==2,"following event keeps its list position in the same batch");
    ClosePostBoundaryAtPresent(); // Exercise the callback boundary; no actual Present/game claim.
    const auto frozen=SnapshotPostBoundary(trace,701,9,502);
    Check(frozen.close==nr_post_boundary::Close::Present&&frozen.other_rtv_clears==1,"closed evidence window retains known-other counts");
    ReadPixels(f,output.resource);
    Check(SnapshotPostBoundary(trace,701,9,502).count==frozen.count,"later readback operations do not mutate a closed window");

    // Destruction of unused real objects must retire records; registry metadata
    // must not keep either object alive. No undefined D3D12 operation is issued.
    Texture retired;MakeTexture(f,retired,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Rtv(f,retired.resource,Cpu(f,rtv,2));const auto receipt=g_nr_descriptors.Cpu(Cpu(f,rtv,2).ptr,2,nr_descriptor::Kind::Rtv);
    Check(static_cast<bool>(receipt),"real retirement probe starts tracked");
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    nr_route::Scope route_scope;route_scope.device=NrFormatDeviceIdentity(f.device);
    route_scope.color=NrFormatResourceIdentity(output.resource);route_scope.output=NrFormatResourceIdentity(retired.resource);
    route_scope.feature=601;route_scope.config=g_config_generation.load();route_scope.requested=2;
    route_scope.lifetime_known=NrDescriptorTrackRouteResource(output.resource,route_scope.device,route_scope.color) &&
        NrDescriptorTrackRouteResource(retired.resource,route_scope.device,route_scope.output);
    Check(route_scope.lifetime_known,"route reuses actual device/resource notification receipts");
    AcquireSRWLockExclusive(&g_nr_route_lock);g_nr_route.Begin(route_scope,901);g_nr_route.PreRejected(true,"fixture-contract");
    ReleaseSRWLockExclusive(&g_nr_route_lock);
    Check(NrRouteRead().current,"actual resource retirement test starts with a current route scope");
#endif
    ReleaseCom(retired.resource);
    Check(!g_nr_descriptors.Current(receipt),"native resource destruction invalidates retained value receipt without pinning resource");
#if defined(NR_HAS_ROUTE_COMPATIBILITY)
    const auto retired_route=NrRouteRead();
    Check(!retired_route.current&&!retired_route.locked&&!std::strcmp(retired_route.invalidation,"resource-retired"),
        "actual COM destruction invalidates the route immediately without another Evaluate");
#endif
    auto *retired_heap=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_RTV,false,1);
    Rtv(f,other.resource,Cpu(f,retired_heap));const auto heap_receipt=g_nr_descriptors.Cpu(Cpu(f,retired_heap).ptr,2);
    ReleaseCom(retired_heap);
    Check(!g_nr_descriptors.Current(heap_receipt),"native heap destruction invalidates its descriptor generation");
    Check(!NrFormatEvidence(NrFormatKey(f.device,output.resource,502,true)).Resolved(),"GPU execution also leaves admission ledger unchanged");
    ReleaseCom(unobserved);ReleaseCom(cpu);ReleaseCom(gpu);ReleaseCom(rtv);
    std::printf("PASS descriptor provenance WARP: %u checks; real native cached-view clear, RTV target/range, lifetime, copy, batch-position and stored pixels; no NGX/game\n",queue_test::checks);
}
} // namespace descriptor_warp_test
