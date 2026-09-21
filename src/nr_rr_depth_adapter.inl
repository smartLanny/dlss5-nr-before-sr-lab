#include "nr_rr_depth_shader.h"
#include "nr_aux_shader_runtime.hpp"
#if NR_AUX_PRECOMPILED
#include "nr_aux_rr_depth_precompiled.h"
#endif
std::atomic<unsigned long long> g_rr_depth_conversions{0}, g_rr_depth_declines{0};
std::atomic<unsigned long long> g_rr_depth_proxy_matches{0};
enum class RrDepthReject : unsigned {
    None, Projection, Storage, SlotBusy, Pipeline, DeviceQuery, DeviceIdentity,
    OutputSupport, InputSupport, Budget, Texture, Heap, SlotShape, DispatchBinding
};
std::atomic<RrDepthReject> g_rr_depth_last_reject{RrDepthReject::None};
const char *RrDepthRejectName(RrDepthReject reason) {
    switch(reason) {
    case RrDepthReject::None:return "none";
    case RrDepthReject::Projection:return "current-projection";
    case RrDepthReject::Storage:return "linear-storage";
    case RrDepthReject::SlotBusy:return "slot-not-retired";
    case RrDepthReject::Pipeline:return "conversion-pipeline";
    case RrDepthReject::DeviceQuery:return "depth-device-query";
    case RrDepthReject::DeviceIdentity:return "different-or-unproven-device";
    case RrDepthReject::OutputSupport:return "r32-typed-store";
    case RrDepthReject::InputSupport:return "depth-shader-load";
    case RrDepthReject::Budget:return "private-depth-budget";
    case RrDepthReject::Texture:return "private-depth-allocation";
    case RrDepthReject::Heap:return "private-depth-descriptors";
    case RrDepthReject::SlotShape:return "private-depth-shape";
    case RrDepthReject::DispatchBinding:return "dispatch-binding";
    }
    return "unknown";
}
bool RejectRrDepth(RrDepthReject reason) {
    const auto previous=g_rr_depth_last_reject.exchange(reason,std::memory_order_relaxed);
    if(previous!=reason) Log("[RR-depth] rejected at %s; native RR retained.",RrDepthRejectName(reason));
    return false;
}

// ReShade 6.8's Resource::GetDevice hook deliberately returns its device proxy,
// while an original command list can return the native device. Compare COM
// identity after the pinned ReShade unwrapping contract, without substituting
// either execution device or weakening cross-device rejection.
constexpr GUID kRrDepthUnwrappedObject = {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
bool ResolveRrDepthDeviceIdentity(IUnknown *object, IUnknown **identity) {
    if(!identity)return false;
    *identity=nullptr;
    if(!object)return false;
    IUnknown *unwrapped=nullptr;
    const HRESULT hr=IUnknown_QueryInterface(object,kRrDepthUnwrappedObject,
        reinterpret_cast<void **>(&unwrapped));
    if(FAILED(hr) && hr!=E_NOINTERFACE)return false;
    if(SUCCEEDED(hr) && !unwrapped)return false;
    const HRESULT canonical=IUnknown_QueryInterface(unwrapped?unwrapped:object,IID_IUnknown,
        reinterpret_cast<void **>(identity));
    ReleaseCom(unwrapped);
    return SUCCEEDED(canonical) && *identity;
}
bool IsSameRrDepthDevice(ID3D12Device *first, ID3D12Device *second) {
    if(!first || !second)return false;
    if(first==second)return true;
    IUnknown *a=nullptr,*b=nullptr;
    const bool resolved_a=ResolveRrDepthDeviceIdentity(reinterpret_cast<IUnknown *>(first),&a);
    const bool resolved_b=ResolveRrDepthDeviceIdentity(reinterpret_cast<IUnknown *>(second),&b);
    const bool same=resolved_a && resolved_b && a==b;
    ReleaseCom(a);ReleaseCom(b);
    if(same)g_rr_depth_proxy_matches.fetch_add(1,std::memory_order_relaxed);
    return same;
}

bool ResolveRrLinearDepth(const NgxParams *params, const FeatureRecord &feature,
                          NrGuideBinding &guides) {
    nr_rr_projection::last_origin.store(0,std::memory_order_relaxed);
    if (feature.feature_id!=13 || feature.rr_depth_type==1) return true;
    if (feature.rr_depth_type!=0) return false;
    // Read this Evaluate's matrix, not the one-second diagnostic cache.
    const auto getter=ParamSlot<GetVoidFn>(params,8);
    auto input=nr_rr_depth::ReadCurrent(
        [&](void*& pointer){return getter&&getter(params,"ViewToClipMatrix",&pointer)==kNgxSuccess;},
        [](void* pointer,void* target,size_t size){SIZE_T copied=0;
            return ReadProcessMemory(GetCurrentProcess(),pointer,target,size,&copied)&&copied==size;});
    input=nr_rr_projection::Choose(input,params,feature.generation);
    nr_rr_depth::observations.input.store(input.state,std::memory_order_relaxed);
    const bool placeholder=nr_rr_depth::UsePlaceholder(input.state,nr_post_settings::missing_projection.load(std::memory_order_acquire));
    if(input.state!=nr_rr_depth::InputState::Supported&&!placeholder)
        return RejectRrDepth(RrDepthReject::Projection);
    const auto projection=placeholder?nr_rr_depth::Projection{0,0,true}:input.projection;
    const auto desc=ID3D12Resource_GetDesc(guides.depth);
    if (desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize!=1 ||
        desc.MipLevels!=1 || desc.SampleDesc.Count!=1 ||
        (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)!=0 ||
        (desc.Format!=DXGI_FORMAT_R32_FLOAT && desc.Format!=DXGI_FORMAT_R16_FLOAT)) return RejectRrDepth(RrDepthReject::Storage);
    guides.signature.linear_depth=true;
    guides.signature.rr_depth_placeholder=placeholder;
    guides.signature.depth_inverted=projection.inverted?1:0;
    guides.signature.normalized_create_flags =
        (guides.signature.normalized_create_flags & ~8) | (projection.inverted?8:0);
    guides.signature.depth_projection_a=projection.a;
    guides.signature.depth_projection_b=projection.b;
    (placeholder?nr_rr_depth::observations.placeholder_admitted:nr_rr_depth::observations.admitted)
        .fetch_add(1,std::memory_order_relaxed);
    return true;
}

bool EnsureRrDepthPipeline(ID3D12Device *device) {
    if (g_codec.rr_depth_pipeline && g_codec.rr_depth_root) return device==g_codec.device;
    if (!device || device!=g_codec.device || g_codec.rr_depth_failed) return false;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;
    ranges[1].OffsetInDescriptorsFromTableStart=1;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={2,ranges};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,8};
    D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=2;root_desc.pParameters=parameters;
    ID3DBlob *root_blob=nullptr,*errors=nullptr,*code=nullptr;
    ID3D12RootSignature *root=nullptr;ID3D12PipelineState *pipeline=nullptr;
    HRESULT hr=D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&root_blob,&errors);
    ReleaseCom(errors);
    if (SUCCEEDED(hr)) hr=ID3D12Device_CreateRootSignature(device,0,
        root_blob->lpVtbl->GetBufferPointer(root_blob),root_blob->lpVtbl->GetBufferSize(root_blob),
        IID_ID3D12RootSignature,reinterpret_cast<void **>(&root));
    ReleaseCom(root_blob);
    if(SUCCEEDED(hr)){
#if NR_AUX_PRECOMPILED
        const auto artifact=nr_aux_rr_depth_precompiled::Find("ConvertRrDepthCS","cs_5_0");
        hr=nr_aux_shader::Load(artifact.data,artifact.size,&code);
#else
        hr=nr_aux_shader::Measure(nr_aux_shader::Shader::RrDepth,nr_aux_shader::Operation::Compile,[&]{return D3DCompile(kRrDepthShader,sizeof(kRrDepthShader)-1,"NR.RRDepth",
            nullptr,nullptr,"ConvertRrDepthCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);});
#endif
    }
    ReleaseCom(errors);
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root;
        desc.CS={code->lpVtbl->GetBufferPointer(code),code->lpVtbl->GetBufferSize(code)};
        hr=nr_aux_shader::Measure(nr_aux_shader::Shader::RrDepth,nr_aux_shader::Operation::Pipeline,[&]{return ID3D12Device_CreateComputePipelineState(device,&desc,IID_ID3D12PipelineState,
            reinterpret_cast<void **>(&pipeline));});
    }
    ReleaseCom(code);
    if (FAILED(hr)) {
        ReleaseCom(root);ReleaseCom(pipeline);g_codec.rr_depth_failed=true;
        Log("RR depth adapter pipeline failed: 0x%08X; native RR retained.",static_cast<unsigned>(hr));
        return false;
    }
    g_codec.rr_depth_root=root;g_codec.rr_depth_pipeline=pipeline;return true;
}

bool EnsureRrDepthSlot(ID3D12Device *device, NrSlot &slot, const NrGuideBinding &guides, bool immutable=false) {
    if (!guides.signature.linear_depth) return true;
    const auto &s=guides.signature;
    if (!immutable && !CanMutateSlotLocked(slot)) return RejectRrDepth(RrDepthReject::SlotBusy);
    if (!EnsureRrDepthPipeline(device)) return RejectRrDepth(RrDepthReject::Pipeline);
    ID3D12Device *source_device=nullptr;
    const HRESULT source_hr=ID3D12Resource_GetDevice(guides.depth,IID_ID3D12Device,
        reinterpret_cast<void **>(&source_device));
    const bool same_device=SUCCEEDED(source_hr) && IsSameRrDepthDevice(source_device,device);
    ReleaseCom(source_device);
    if (!same_device) return RejectRrDepth(FAILED(source_hr)?RrDepthReject::DeviceQuery:RrDepthReject::DeviceIdentity);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=DXGI_FORMAT_R32_FLOAT;
    if (FAILED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) ||
        !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) return RejectRrDepth(RrDepthReject::OutputSupport);
    support={};support.Format=static_cast<DXGI_FORMAT>(s.depth_format);
    if (FAILED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) ||
        !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)) return RejectRrDepth(RrDepthReject::InputSupport);
    const auto previous=slot.rr_depth?ID3D12Resource_GetDesc(slot.rr_depth):D3D12_RESOURCE_DESC{};
    if (!slot.rr_depth || previous.Width!=s.depth_width || previous.Height!=s.depth_height) {
        if(!CanMutateSlotLocked(slot)||!InvalidateSlotBindingsLocked(slot))return RejectRrDepth(RrDepthReject::SlotBusy);
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=s.depth_width;desc.Height=s.depth_height;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto allocation=ID3D12Device_GetResourceAllocationInfo(device,0,1,&desc);
        if (!FitsExtraResourcePeak(slot,allocation.SizeInBytes)) return RejectRrDepth(RrDepthReject::Budget);
        ID3D12Resource *depth=nullptr;ID3D12DescriptorHeap *heap=nullptr;
        if (!CreateTexture(device,s.depth_width,s.depth_height,L"NRChain.RRHardwareDepth",&depth,DXGI_FORMAT_R32_FLOAT)) return RejectRrDepth(RrDepthReject::Texture);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(ID3D12Device_CreateDescriptorHeap(device,&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void **>(&heap)))) {
            ReleaseCom(depth);return RejectRrDepth(RrDepthReject::Heap);
        }
        // DRS may change guide R while the NR model bucket and native O stay
        // fixed. Replace only a fence-retired slot; peak includes both planes.
        ReleaseCom(slot.rr_depth);ReleaseCom(slot.rr_depth_heap);
        g_extra_allocated_bytes-=slot.rr_depth_bytes;
        slot.rr_depth=depth;slot.rr_depth_heap=heap;slot.rr_depth_bytes=allocation.SizeInBytes;
        slot.rr_depth_state=D3D12_RESOURCE_STATE_COMMON;
        g_extra_allocated_bytes+=allocation.SizeInBytes;
    }
    const auto out=ID3D12Resource_GetDesc(slot.rr_depth);
    if (out.Width!=s.depth_width || out.Height!=s.depth_height || !slot.rr_depth_heap) return RejectRrDepth(RrDepthReject::SlotShape);
    if(!immutable) {
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.rr_depth_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=static_cast<DXGI_FORMAT>(s.depth_format);
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;ID3D12Device_CreateShaderResourceView(device,guides.depth,&srv,cpu);
    cpu.ptr+=g_codec.descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device,slot.rr_depth,nullptr,&uav,cpu);
    }
    if (slot.rr_depth_source!=guides.depth) {
        ID3D12Resource_AddRef(guides.depth);ReleaseCom(slot.rr_depth_source);slot.rr_depth_source=guides.depth;
    }
    g_rr_depth_last_reject.store(RrDepthReject::None,std::memory_order_relaxed);
    return true;
}

bool DispatchRrDepth(ID3D12GraphicsCommandList *list, NrSlot &slot,const NrGuideBinding &guides) {
    const auto &s=guides.signature;
    if (!s.linear_depth) return true;
    auto* heap=slot.compose_binding&&slot.compose_binding->heaps[nr_binding::Depth]?slot.compose_binding->heaps[nr_binding::Depth]:slot.rr_depth_heap;
    if (!slot.rr_depth || !heap || slot.rr_depth_source!=guides.depth ||
        !g_codec.rr_depth_pipeline || !g_codec.rr_depth_root) return RejectRrDepth(RrDepthReject::DispatchBinding);
    struct Constants { UINT width,height,x,y;float a,b,far_depth,pad; };
    const Constants constants{s.depth_width,s.depth_height,s.depth_x,s.depth_y,
        s.depth_projection_a,s.depth_projection_b,s.depth_inverted?0.f:1.f,s.rr_depth_placeholder?1.f:0.f};
    Transition(list,slot.rr_depth,slot.rr_depth_state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[]={heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,g_codec.rr_depth_root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,&constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list,g_codec.rr_depth_pipeline);
    ID3D12GraphicsCommandList_Dispatch(list,(s.depth_width+7)/8,(s.depth_height+7)/8,1);
    UavBarrier(list,slot.rr_depth);
    Transition(list,slot.rr_depth,slot.rr_depth_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(s.rr_depth_placeholder)nr_rr_depth::observations.placeholder_recorded.fetch_add(1,std::memory_order_relaxed);
    else g_rr_depth_conversions.fetch_add(1,std::memory_order_relaxed);
    return true;
}
