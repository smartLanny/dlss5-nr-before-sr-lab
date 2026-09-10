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
        std::uint64_t feature,Function function,NgxResult result=kNgxSuccess,
        const nr_native_clear::Receipt *prepared=nullptr,
        nr_native_clear::Invocation *invocation=nullptr) {
    Params params(output);NgxHandle handle{991};unsigned callback=717,calls=0;
    const auto original=[&](ID3D12GraphicsCommandList *l,const NgxHandle *h,const NgxParams *p,void *c) {
        Check(l==list.native&&h==&handle&&p==&params.base&&c==&callback,"native wrapper preserves arguments");
        ++calls;function();return result;
    };
    g_inside_ngx_evaluate=true;
    const auto actual=NrCallNativeWithFormatObservation(original,list.native,&handle,&params.base,&callback,
        feature,invocation,prepared);
    g_inside_ngx_evaluate=false;
    Check(calls==1&&actual==result,"native callback is forwarded exactly once with unchanged result");return actual;
}
void Barrier(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);
}
void ReadPixels(queue_test::Fixture &f,ID3D12Resource *resource,bool expect_red_rectangle=true) {
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
        const bool red=expect_red_rectangle&&x>=2&&x<6&&y>=3&&y<8;
        Check(pixel[0]==(red?255:0)&&pixel[1]==0&&pixel[2]==(red?0:255)&&pixel[3]==255,
            expect_red_rectangle ? "real GPU native UAV clear and later exact RTV rectangle both reached texture storage" :
                "GPU-unknown CPU declaration leaves a valid real Clear that overwrites the prior red rectangle");
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
    const auto cpu_request=NrNativeClearCandidate(NrFormatKey(f.device,output.resource,502,true),9);
    Check(cpu_request && cpu_request.format==28,
        "real Clear records an independent frozen CPU request without promoting GPU proof");
    const auto resolved=NrResolveColorView(f.device,output.resource,ID3D12Resource_GetDesc(output.resource),502,true,0);
    Check(resolved && resolved.clear_source && resolved.plan.srv==28,
        "real cached Clear CPU request resolves the existing R8 color and capability contract");
    const auto stable_cpu_output=g_nr_descriptors.Cpu(Cpu(f,cpu).ptr,0,nr_descriptor::Kind::Uav);
    Check(stable_cpu_output&&g_nr_descriptors.Current(stable_cpu_output),
        "real CPU Output UAV receipt is current before oversized descriptor copies");
    const auto prepared_before_copy=NrNativeClearCandidate(NrFormatKey(f.device,output.resource,502,true),9);
    Check(static_cast<bool>(prepared_before_copy),
        "current native Clear format candidate exists before oversized descriptor copies");
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

    // Fill a separate live shader-visible heap through the bounded provenance
    // path. Its 65,537 distinct slots evict the original GPU slot while the
    // real descriptor contents in that original heap remain valid for Clear.
    auto *overflow_gpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true,65537);
    const auto source_cpu=Cpu(f,cpu);const auto overflow_cpu=Cpu(f,overflow_gpu);
    const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(f.device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(unsigned i=0;i<65537;++i) {
        const auto write=g_nr_descriptors.BeginCopy(overflow_cpu.ptr+static_cast<SIZE_T>(i)*stride,
            source_cpu.ptr,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_nr_descriptors.Finish(write);
    }
    const auto evicted_gpu=g_nr_descriptors.Gpu(Gpu(f,gpu,1).ptr,0);
    Check(!evicted_gpu&&evicted_gpu.reason==nr_descriptor::Reason::SlotUnknown,
        "bounded provenance fill reproduces an evicted original GPU slot");
    queue_test::List unknown_list(f);pipeline.Bind(unknown_list,gpu);
    Barrier(unknown_list.native,output.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Native(unknown_list,output.resource,503,[&] {
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(unknown_list.native,Gpu(f,gpu,1),Cpu(f,cpu),
            output.resource,blue,0,nullptr);
    });
    const auto unknown_use=SnapshotNrDescriptorUse();
    Check(unknown_use.clear_calls==1&&unknown_use.unknown==1&&!unknown_use.observed_typed&&
        !unknown_use.after_native_valid&&!unknown_use.at_submit_valid,
        "real Clear with an evicted GPU slot keeps GPU-use and submission proof unknown");
    const auto cpu_only=NrNativeClearCandidate(NrFormatKey(f.device,output.resource,503,true),9);
    Check(cpu_only&&cpu_only.format==28,
        "stable CPU UAV and explicit resource retain an independent typed declaration after GPU eviction");
    const auto unknown_resolved=NrResolveColorView(f.device,output.resource,ID3D12Resource_GetDesc(output.resource),503,true,0);
    Check(unknown_resolved&&unknown_resolved.clear_source&&unknown_resolved.plan.srv==28,
        "GPU-unknown CPU declaration supplies the typed color preparation");
    nr_native_clear::Invocation fresh_unknown;
    Native(unknown_list,output.resource,503,[&] {
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(unknown_list.native,Gpu(f,gpu,1),Cpu(f,cpu),
            output.resource,blue,0,nullptr);
    },kNgxSuccess,&cpu_only,&fresh_unknown);
    Check(NrNativeClearMayFinish(unknown_list.native,output.resource,503,28,cpu_only,fresh_unknown),
        "GPU-unknown CPU declaration still requires and passes the exact fresh-call guard");
    const auto fresh_unknown_use=SnapshotNrDescriptorUse();
    Check(fresh_unknown_use.unknown==1&&!fresh_unknown_use.after_native_valid&&!fresh_unknown_use.at_submit_valid,
        "fresh guarded call does not upgrade its GPU-unknown descriptor proof");
    Barrier(unknown_list.native,output.resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    unknown_list.Close();
    ID3D12CommandList *unknown_batch[]={unknown_list.Base()};
    ProcessSubmittedCommandLists(0,f.a,1,unknown_batch);f.Drain(f.a);
    const auto submitted_unknown_use=SnapshotNrDescriptorUse();
    Check(submitted_unknown_use.unknown==1&&!submitted_unknown_use.after_native_valid&&!submitted_unknown_use.at_submit_valid,
        "actual Execute does not upgrade an evicted-slot GPU proof through the CPU declaration");
    ReadPixels(f,output.resource,false);ReleaseCom(overflow_gpu);

    // A copy larger than the observer's bounded write array must preserve the
    // source CPU Output receipt and candidate while leaving touched GPU slots
    // undecoded. An existing known conflict is unsafe and is rejected first;
    // the native operation below is valid because the real GPU destination is
    // the same Output resource after the copy.
    constexpr unsigned kBulkCopyCount=257;
    auto *bulk_cpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,false,kBulkCopyCount);
    auto *bulk_gpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true,kBulkCopyCount+1);
    Uav(f,other.resource,Cpu(f,bulk_gpu));
    Uav(f,other.resource,Cpu(f,bulk_gpu,kBulkCopyCount));
    for(unsigned i=0;i<kBulkCopyCount;++i)Uav(f,output.resource,Cpu(f,bulk_cpu,i));
    const auto bulk_source_output=g_nr_descriptors.Cpu(Cpu(f,bulk_cpu).ptr,0,nr_descriptor::Kind::Uav);
    Check(bulk_source_output&&g_nr_descriptors.Current(bulk_source_output),
        "oversized CopyDescriptorsSimple starts with a stable CPU Output source");
    const auto bulk_heap=g_nr_descriptors.HeapAt(reinterpret_cast<std::uint64_t>(bulk_gpu));
    const auto output_identity=g_nr_descriptors.ResourceAt(reinterpret_cast<std::uint64_t>(output.resource));
    const auto known_conflict=g_nr_descriptors.UavPair(NrFormatDeviceIdentity(f.device),output_identity,
        bulk_heap.id,Gpu(f,bulk_gpu).ptr,Cpu(f,bulk_cpu).ptr);
    Check(!known_conflict&&known_conflict.reason==nr_descriptor::Reason::WrongResource,
        "known GPU resource conflict remains rejected before oversized descriptor copy");
    const auto untouched_bulk_gpu=g_nr_descriptors.Gpu(Gpu(f,bulk_gpu,kBulkCopyCount).ptr,0,nr_descriptor::Kind::Uav);
    Check(untouched_bulk_gpu&&untouched_bulk_gpu.view.resource.address==reinterpret_cast<std::uint64_t>(other.resource),
        "GPU descriptor outside the oversized copy range starts as a known other resource");
    ID3D12Device_CopyDescriptorsSimple(f.device,kBulkCopyCount,Cpu(f,bulk_gpu),Cpu(f,bulk_cpu),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Check(g_nr_descriptors.Current(stable_cpu_output)&&g_nr_descriptors.Current(bulk_source_output),
        "oversized CopyDescriptorsSimple preserves the original and copied CPU Output receipts");
    Check(!g_nr_descriptors.Gpu(Gpu(f,bulk_gpu).ptr,0),
        "oversized CopyDescriptorsSimple leaves its touched GPU destination unknown");
    Check(g_nr_descriptors.Current(untouched_bulk_gpu),
        "oversized CopyDescriptorsSimple preserves an untouched slot in the same GPU heap");

    // The same scoped invalidation rule applies when the API receives more
    // than 64 sparse source/destination ranges. The original API still gets
    // every range; provenance marks only the touched destinations unknown.
    constexpr unsigned kSparseRanges=65;
    auto *sparse_cpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,false,kSparseRanges);
    auto *sparse_gpu=Heap(f,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,true,kSparseRanges+1);
    for(unsigned i=0;i<kSparseRanges;++i)Uav(f,output.resource,Cpu(f,sparse_cpu,i));
    Uav(f,other.resource,Cpu(f,sparse_gpu,kSparseRanges));
    D3D12_CPU_DESCRIPTOR_HANDLE sparse_destinations[kSparseRanges]{};
    D3D12_CPU_DESCRIPTOR_HANDLE sparse_sources[kSparseRanges]{};
    UINT sparse_sizes[kSparseRanges]{};
    for(unsigned i=0;i<kSparseRanges;++i) {
        sparse_destinations[i]=Cpu(f,sparse_gpu,i);sparse_sources[i]=Cpu(f,sparse_cpu,i);sparse_sizes[i]=1;
    }
    const auto untouched_sparse_gpu=g_nr_descriptors.Gpu(Gpu(f,sparse_gpu,kSparseRanges).ptr,0,nr_descriptor::Kind::Uav);
    Check(untouched_sparse_gpu&&untouched_sparse_gpu.view.resource.address==reinterpret_cast<std::uint64_t>(other.resource),
        "GPU descriptor outside sparse ranges starts as a known other resource");
    ID3D12Device_CopyDescriptors(f.device,kSparseRanges,sparse_destinations,sparse_sizes,
        kSparseRanges,sparse_sources,sparse_sizes,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto candidate_after_copy=NrNativeClearCandidate(NrFormatKey(f.device,output.resource,502,true),9);
    Check(candidate_after_copy&&candidate_after_copy.call==prepared_before_copy.call&&
        g_nr_descriptors.Current(stable_cpu_output),
        "oversized sparse CopyDescriptors preserves the current Clear candidate and CPU Output receipt");
    Check(!g_nr_descriptors.Gpu(Gpu(f,sparse_gpu).ptr,0),
        "oversized sparse CopyDescriptors leaves its touched GPU destination unknown");
    Check(g_nr_descriptors.Current(untouched_sparse_gpu),
        "oversized sparse CopyDescriptors preserves an untouched slot in the same GPU heap");

    // A copy destination is a known heap range whose contents were not
    // decoded. Its touched slot has a stale local epoch, so the CPU-only
    // fallback may pass only through the current-call guard and never becomes
    // GPU proof.
    queue_test::List bulk_list(f);pipeline.Bind(bulk_list,bulk_gpu);
    Barrier(bulk_list.native,output.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    nr_native_clear::Invocation bulk_invocation;
    Native(bulk_list,output.resource,502,[&] {
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(bulk_list.native,Gpu(f,bulk_gpu),
            Cpu(f,bulk_cpu),output.resource,blue,0,nullptr);
    },kNgxSuccess,&prepared_before_copy,&bulk_invocation);
    Check(NrNativeClearMayFinish(bulk_list.native,output.resource,502,28,prepared_before_copy,bulk_invocation),
        "unknown GPU copy destination retains CPU-only NativeClear qualification on the current call");
    const auto bulk_use=SnapshotNrDescriptorUse();
    Check(bulk_use.unknown==1&&!bulk_use.observed_typed&&!bulk_use.after_native_valid&&!bulk_use.at_submit_valid,
        "CPU-only NativeClear qualification never upgrades unknown GPU proof");
    Barrier(bulk_list.native,output.resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET);
    bulk_list.Close();
    ID3D12CommandList *bulk_batch[]={bulk_list.Base()};
    ProcessSubmittedCommandLists(0,f.a,1,bulk_batch);f.Drain(f.a);
    const auto submitted_bulk_use=SnapshotNrDescriptorUse();
    Check(submitted_bulk_use.unknown==1&&!submitted_bulk_use.after_native_valid&&!submitted_bulk_use.at_submit_valid,
        "actual Execute keeps the CPU-only clear's GPU proof unknown");
    ReadPixels(f,output.resource,false);
    ReleaseCom(sparse_cpu);ReleaseCom(sparse_gpu);ReleaseCom(bulk_cpu);ReleaseCom(bulk_gpu);

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
