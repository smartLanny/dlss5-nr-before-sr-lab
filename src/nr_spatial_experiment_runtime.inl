// Opt-in post-native B/Z experiment. The containing NrSlot already owns the
// exact command-list, queue and completion-fence retirement contract.
#include "nr_spatial_experiment_policy.h"
namespace nr_spatial_experiment {
struct Status {
    std::atomic<unsigned> reason{unsigned(Reason::Off)},configuration{0},mode{0};
    std::atomic<unsigned long long> requested{0},recorded{0},submitted{0},discarded{0};
    std::atomic<unsigned long long> token{0},instance{0},required_bytes{0},three_slot_bytes{0},available_bytes{0},resident_bytes{0};
    std::atomic<unsigned> low_width{0},low_height{0};
};
Status status{};
std::atomic<UINT64> resident_bytes{0};
const char* Name(Reason);
void Say(Reason reason,unsigned mode=0) {
    const auto config=g_config_generation.load();
    status.reason.store(unsigned(reason));status.mode.store(mode);
    status.configuration.store(config);
}
const char* Name(Reason r) {
    switch(r) {
    case Reason::Off:return "off";
    case Reason::WaitingRoute:return "needs post-native NR";
    case Reason::Conflict:return "private SR or temporal conflict";
    case Reason::UnsupportedFormat:return "requires FP16 post-native B/Z";
    case Reason::UnsupportedExtent:return "unsupported B/Z extent";
    case Reason::NeedLowerScale:return "area mode needs lower work resolution";
    case Reason::BudgetCap:return "scratch exceeds bounded cap";
    case Reason::BudgetHeadroom:return "local VRAM headroom insufficient";
    case Reason::MemoryUnknown:return "local VRAM budget unknown";
    case Reason::Allocation:return "scratch memory unavailable";
    case Reason::Pipeline:return "shader pipeline unavailable";
    case Reason::Busy:return "slot still in flight";
    case Reason::Recorded:return "recorded, not submitted";
    case Reason::Submitted:return "submitted, picture unverified";
    case Reason::Discarded:return "recording discarded";
    }
    return "unknown";
}
bool EnsurePipelines(NrSlot& slot,unsigned mode) {
    if(!g_codec.device||!g_codec.root)return false;
    auto create=[](const char* entry,ID3D12PipelineState** out){
        ID3DBlob* shader=CompileCodecShader(entry);
        if(!shader)return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature=g_codec.root;
        desc.CS.pShaderBytecode=ID3D10Blob_GetBufferPointer(shader);
        desc.CS.BytecodeLength=ID3D10Blob_GetBufferSize(shader);
        const auto hr=ID3D12Device_CreateComputePipelineState(g_codec.device,&desc,
            IID_ID3D12PipelineState,reinterpret_cast<void**>(out));
        ReleaseCom(shader);return SUCCEEDED(hr)&&*out;
    };
    return mode==unsigned(Mode::AreaGuided)
        ?(slot.spatial_area||create("SpatialAreaCS",&slot.spatial_area))&&
            (slot.spatial_guided||create("SpatialGuidedCS",&slot.spatial_guided))
        :(slot.spatial_local||create("SpatialLocalCS",&slot.spatial_local));
}
void ReleaseSlot(NrSlot& slot) {
    if(slot.spatial_bytes)resident_bytes.fetch_sub(slot.spatial_bytes);
    status.resident_bytes.store(resident_bytes.load());slot.spatial_bytes=0;
    ReleaseCom(slot.spatial_area);ReleaseCom(slot.spatial_guided);ReleaseCom(slot.spatial_local);
    ReleaseCom(slot.spatial_heap);
    ReleaseCom(slot.spatial_scratch);
    ReleaseCom(slot.spatial_low_b);
    ReleaseCom(slot.spatial_low_delta);
    slot.spatial_bound_source=nullptr;slot.spatial_bound_result=nullptr;
    slot.spatial_prepared=false;
    slot.spatial_width=slot.spatial_height=slot.spatial_low_width=slot.spatial_low_height=0;
}
UINT64 PlaneBytes(ID3D12Device* device,unsigned w,unsigned h,DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
    d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto a=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d);
    return a.SizeInBytes&&a.SizeInBytes!=UINT64_MAX?a.SizeInBytes:0;
}
bool Prepare(NrSlot& slot,unsigned mode,unsigned width,unsigned height,unsigned nw,unsigned nh) {
    if(slot.spatial_heap&&slot.spatial_scratch &&
       slot.spatial_bound_source==slot.source&&slot.spatial_bound_result==slot.result&&
       (mode==unsigned(Mode::LocalDelta)||(slot.spatial_low_b&&slot.spatial_low_delta)) &&
       slot.spatial_width==width&&slot.spatial_height==height&&
       slot.spatial_low_width==nw&&slot.spatial_low_height==nh)return true;
    // Never reprogram a descriptor heap or retire a plane still referenced by
    // a submitted or unsubmitted list.
    if(!CanMutateSlotLocked(slot)){Say(Reason::Busy,mode);return false;}
    if(slot.spatial_heap)ReleaseSlot(slot);
    const UINT64 full=PlaneBytes(g_codec.device,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
    const UINT64 low=mode==unsigned(Mode::AreaGuided)
        ?PlaneBytes(g_codec.device,nw,nh,DXGI_FORMAT_R32G32B32A32_FLOAT):0;
    if(!full||(mode==unsigned(Mode::AreaGuided)&&!low)||low>(UINT64_MAX-full)/2){
        status.required_bytes.store(0);status.three_slot_bytes.store(0);
        Say(Reason::Allocation,mode);return false;
    }
    const UINT64 required=full+low*2;
    status.required_bytes.store(required);
    constexpr UINT64 cap=768ull<<20;
    // Admission covers all three possible in-flight slots. A configuration
    // that can only fit some slots would alternate filtered/unfiltered frames.
    if(required>cap/3){
        status.three_slot_bytes.store(required*3);
        status.available_bytes.store(cap);
        Say(Reason::BudgetCap,mode);return false;
    }
    const UINT64 three_slot_required=required*3;
    status.three_slot_bytes.store(three_slot_required);
    UINT64 matching_resident=0;
    for(const auto& context:g_contexts)for(const auto& existing:context.slots)
        if(existing.spatial_heap&&existing.spatial_bytes==required&&
           existing.spatial_width==width&&existing.spatial_height==height&&
           existing.spatial_low_width==nw&&existing.spatial_low_height==nh)
            matching_resident+=existing.spatial_bytes;
    const UINT64 still_needed=matching_resident<three_slot_required
        ?three_slot_required-matching_resident:required;
    const auto memory=nr_process_memory::Read(ID3D12Device_GetAdapterLuid(g_codec.device),true);
    const UINT64 spare=SUCCEEDED(memory.local_result)&&memory.local.CurrentUsage<memory.local.Budget
        ?memory.local.Budget-memory.local.CurrentUsage:0;
    const UINT64 resident=resident_bytes.load();
    const UINT64 cap_left=resident<cap?cap-resident:0;
    const UINT64 available=std::min(cap_left,spare>(64ull<<20)?spare-(64ull<<20):0);
    status.available_bytes.store(available);
    if(FAILED(memory.local_result)){Say(Reason::MemoryUnknown,mode);return false;}
    if(required>cap_left){Say(Reason::BudgetCap,mode);return false;}
    if(required>available||still_needed>(spare>(64ull<<20)?spare-(64ull<<20):0)){
        Say(Reason::BudgetHeadroom,mode);return false;
    }
    ID3D12Resource *scratch=nullptr,*low_b=nullptr,*low_d=nullptr;
    ID3D12DescriptorHeap *heap=nullptr;
    bool ok=CreateTexture(g_codec.device,width,height,L"NRChain.SpatialCandidate",&scratch);
    if(ok&&mode==unsigned(Mode::AreaGuided))
        ok=CreateTexture(g_codec.device,nw,nh,L"NRChain.SpatialAreaB",&low_b,DXGI_FORMAT_R32G32B32A32_FLOAT)&&
           CreateTexture(g_codec.device,nw,nh,L"NRChain.SpatialAreaDelta",&low_d,DXGI_FORMAT_R32G32B32A32_FLOAT);
    if(ok){
        D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        d.NumDescriptors=kCodecDescriptorCount;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ok=SUCCEEDED(ID3D12Device_CreateDescriptorHeap(g_codec.device,&d,
            IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&heap)))&&heap;
    }
    if(!ok){ReleaseCom(heap);ReleaseCom(scratch);ReleaseCom(low_b);ReleaseCom(low_d);Say(Reason::Allocation,mode);return false;}
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    const auto at=[&](unsigned index){auto h=cpu;h.ptr+=SIZE_T(index)*g_codec.descriptor_size;return h;};
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    ID3D12Device_CreateShaderResourceView(g_codec.device,slot.source,&srv,at(0));
    ID3D12Device_CreateShaderResourceView(g_codec.device,slot.result,&srv,at(1));
    srv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    ID3D12Device_CreateShaderResourceView(g_codec.device,low_b,&srv,at(10));
    ID3D12Device_CreateShaderResourceView(g_codec.device,low_d,&srv,at(11));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    ID3D12Device_CreateUnorderedAccessView(g_codec.device,scratch,nullptr,&uav,at(kCodecResultUavIndex));
    uav.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    ID3D12Device_CreateUnorderedAccessView(g_codec.device,low_b,nullptr,&uav,at(kCodecSrvCount+5));
    ID3D12Device_CreateUnorderedAccessView(g_codec.device,low_d,nullptr,&uav,at(kCodecSrvCount+7));
    slot.spatial_heap=heap;slot.spatial_scratch=scratch;slot.spatial_low_b=low_b;slot.spatial_low_delta=low_d;
    slot.spatial_bound_source=slot.source;slot.spatial_bound_result=slot.result;
    slot.spatial_bytes=required;resident_bytes.fetch_add(required);
    status.resident_bytes.store(resident_bytes.load());
    slot.spatial_width=width;slot.spatial_height=height;slot.spatial_low_width=nw;slot.spatial_low_height=nh;
    slot.spatial_scratch_state=slot.spatial_low_b_state=slot.spatial_low_delta_state=D3D12_RESOURCE_STATE_COMMON;
    return true;
}
void SweepIdle() {
    // Every owned heap has a nonzero allocation receipt; the default-off
    // process never enters the context/slot walk.
    if(!resident_bytes.load())return;
    if(g_config.spatial_experiment_mode&&
       !g_config.private_residual_sr&&!g_config.temporal_backend)return;
    for(auto& context:g_contexts)for(auto& slot:context.slots)
        if(slot.spatial_heap&&CanMutateSlotLocked(slot))ReleaseSlot(slot);
}
void PrepareForPost(NrContext& context,NrSlot& slot,unsigned width,unsigned height,
                    unsigned base_x,unsigned base_y) {
    // A previous call may have prepared identical resources. Only this call's
    // completed admission can authorize Finish after the opaque native Evaluate.
    slot.spatial_prepared=false;
    const unsigned mode=Normalize(g_config.spatial_experiment_mode);
    if(!mode){
        if(slot.spatial_heap&&CanMutateSlotLocked(slot))ReleaseSlot(slot);
        Say(Reason::Off);return;
    }
    status.requested.fetch_add(1);status.instance.store(context.instance_id);
    if(g_config.private_residual_sr||g_config.temporal_backend){
        if(slot.spatial_heap&&CanMutateSlotLocked(slot))ReleaseSlot(slot);
        Say(Reason::Conflict,mode);return;
    }
    if(context.mode!=1||!slot.owns_source||slot.owns_result){
        if(slot.spatial_heap&&CanMutateSlotLocked(slot))ReleaseSlot(slot);
        Say(Reason::WaitingRoute,mode);return;
    }
    if(slot.source_format!=DXGI_FORMAT_R16G16B16A16_FLOAT||
       slot.result_format!=DXGI_FORMAT_R16G16B16A16_FLOAT){Say(Reason::UnsupportedFormat,mode);return;}
    const auto bdesc=ID3D12Resource_GetDesc(slot.source),zdesc=ID3D12Resource_GetDesc(slot.result);
    if(!width||!height||width>16384||height>16384||base_x>zdesc.Width||
       width>zdesc.Width-base_x||base_y>zdesc.Height||height>zdesc.Height-base_y||
       bdesc.Width<width||bdesc.Height<height||!context.width||!context.height){
        Say(Reason::UnsupportedExtent,mode);return;
    }
    const unsigned nw=mode==unsigned(Mode::AreaGuided)?std::min(width,context.width):0;
    const unsigned nh=mode==unsigned(Mode::AreaGuided)?std::min(height,context.height):0;
    if(mode==unsigned(Mode::AreaGuided)&&nw==width&&nh==height){
        Say(Reason::NeedLowerScale,mode);return;
    }
    if(slot.spatial_retry_config==g_config_generation.load()&&
       context.frames_seen<slot.spatial_retry_after_frame)return;
    if(!Prepare(slot,mode,width,height,nw,nh)){
        slot.spatial_retry_config=g_config_generation.load();
        const auto reason=static_cast<Reason>(status.reason.load());
        slot.spatial_retry_after_frame=reason==Reason::Busy?context.frames_seen+1:
            reason==Reason::BudgetCap?~0ull:context.frames_seen+120;
        return;
    }
    if(!EnsurePipelines(slot,mode)){
        Say(Reason::Pipeline,mode);
        slot.spatial_retry_config=g_config_generation.load();
        slot.spatial_retry_after_frame=context.frames_seen+120;
        return;
    }
    slot.spatial_prepared=true;
    slot.spatial_prepared_config=g_config_generation.load();
    slot.spatial_prepared_mode=mode;
    slot.spatial_prepared_width=width;slot.spatial_prepared_height=height;
    slot.spatial_prepared_base_x=base_x;slot.spatial_prepared_base_y=base_y;
    slot.spatial_prepared_source=slot.source;slot.spatial_prepared_result=slot.result;
    slot.spatial_prepared_feature=context.feature_generation;
}
bool Finish(ID3D12GraphicsCommandList* list,NrContext& context,NrSlot& slot,
            const FeatureRecord& feature,unsigned width,unsigned height,
            unsigned base_x,unsigned base_y) {
    const unsigned mode=Normalize(g_config.spatial_experiment_mode);
    if(!mode){Say(Reason::Off);return true;}
    const bool prepared=slot.spatial_prepared &&
        slot.spatial_prepared_config==g_config_generation.load()&&
        slot.spatial_prepared_mode==mode&&
        slot.spatial_prepared_width==width&&slot.spatial_prepared_height==height&&
        slot.spatial_prepared_base_x==base_x&&slot.spatial_prepared_base_y==base_y&&
        slot.spatial_prepared_source==slot.source&&slot.spatial_prepared_result==slot.result&&
        slot.spatial_prepared_feature==feature.generation;
    slot.spatial_prepared=false;
    if(!prepared)return true;
    if(g_config.private_residual_sr||g_config.temporal_backend)return true;
    const unsigned nw=mode==unsigned(Mode::AreaGuided)?std::min(width,context.width):0;
    const unsigned nh=mode==unsigned(Mode::AreaGuided)?std::min(height,context.height):0;
    if((mode==unsigned(Mode::AreaGuided)&&(!slot.spatial_area||!slot.spatial_guided))||
       (mode==unsigned(Mode::LocalDelta)&&!slot.spatial_local))return true;
    if(!slot.spatial_heap||!slot.spatial_scratch||slot.spatial_width!=width||
       slot.spatial_height!=height||slot.spatial_low_width!=nw||slot.spatial_low_height!=nh||
       slot.spatial_bound_source!=slot.source||slot.spatial_bound_result!=slot.result)return true;
    Transition(list,slot.source,slot.source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,slot.result,slot.result_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,slot.spatial_scratch,slot.spatial_scratch_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[]={slot.spatial_heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,g_codec.root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.spatial_heap));
    CodecConstants constants{};constants.width=width;constants.height=height;
    constants.source_base_x=base_x;constants.source_base_y=base_y;
    constants.dual_low_width=nw;constants.dual_low_height=nh;
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,kCodecRootConstantCount,&constants,0);
    if(mode==unsigned(Mode::AreaGuided)){
        Transition(list,slot.spatial_low_b,slot.spatial_low_b_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,slot.spatial_low_delta,slot.spatial_low_delta_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetPipelineState(list,slot.spatial_area);
        ID3D12GraphicsCommandList_Dispatch(list,(nw+7)/8,(nh+7)/8,1);
        UavBarrier(list,slot.spatial_low_b);UavBarrier(list,slot.spatial_low_delta);
        Transition(list,slot.spatial_low_b,slot.spatial_low_b_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,slot.spatial_low_delta,slot.spatial_low_delta_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    ID3D12GraphicsCommandList_SetPipelineState(list,mode==unsigned(Mode::AreaGuided)?slot.spatial_guided:slot.spatial_local);
    ID3D12GraphicsCommandList_Dispatch(list,(width+7)/8,(height+7)/8,1);
    UavBarrier(list,slot.spatial_scratch);
    Transition(list,slot.spatial_scratch,slot.spatial_scratch_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,slot.result,slot.result_state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=slot.spatial_scratch;
    source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;source.SubresourceIndex=0;
    D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=slot.result;
    target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;target.SubresourceIndex=0;
    const D3D12_BOX box{0,0,0,width,height,1};
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&target,base_x,base_y,0,&source,&box);
    Transition(list,slot.result,slot.result_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    slot.spatial_token=context.pending_recording_token;
    slot.spatial_configuration=g_config_generation.load();
    status.token.store(slot.spatial_token);status.low_width.store(nw);status.low_height.store(nh);
    status.recorded.fetch_add(1);Say(Reason::Recorded,mode);
    return true;
}
void Submitted(NrContext& context,unsigned long long token){
    for(auto& slot:context.slots)if(slot.spatial_token==token&&token){
        slot.spatial_token=0;status.submitted.fetch_add(1);
        if(slot.spatial_configuration==g_config_generation.load())Say(Reason::Submitted,Normalize(g_config.spatial_experiment_mode));
    }
}
void Discarded(NrContext& context,unsigned long long token){
    for(auto& slot:context.slots)if(slot.spatial_token==token&&token){
        slot.spatial_token=0;status.discarded.fetch_add(1);
        if(slot.spatial_configuration==g_config_generation.load())Say(Reason::Discarded,Normalize(g_config.spatial_experiment_mode));
    }
}
}
