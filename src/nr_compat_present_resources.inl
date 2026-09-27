#include "nr_aux_shader_runtime.hpp"
#if NR_AUX_PRECOMPILED
#include "nr_aux_present_precompiled.h"
#endif
using nr_compat_present::Status;
using nr_compat_present::Encoding;
using nr_compat_present::Extent;
using nr_compat_present::Observation;
constexpr unsigned kFlightCount=3, kPassLimit=5;
constexpr unsigned long long kOwnedBudget=1024ull*1024*1024; // excludes NGX internal allocations
struct Layer {
    NrContext context{};
    NrGuideBinding frame_guides{};
    NrSlot binding{}; // Own parameter binding only; never inserted into game context tables.
    NgxParams* evaluation_params[kFlightCount]{};
    nr_dual::dx12::SpatialSlot* spatial=nullptr;
    nr_dual::Plan plan{};
    ID3D12DescriptorHeap *init_input=nullptr,*init_output=nullptr;
};
struct Flight {
    ID3D12CommandAllocator* allocator=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12DescriptorHeap* rtv=nullptr;
    ID3D12Resource* timing_readback=nullptr;
    bool timing_resolved=false,capture_recorded=false;
    unsigned config_generation=0;
    ID3D12Resource* backbuffer=nullptr; // released only after this exact flight fence
    unsigned long long fence=0,source=0;
    bool adopted=false;
#if NR_HAS_TEMPORAL_RUNTIME
    unsigned long long temporal_recording=0;
    bool temporal_pending=false;
#endif
};
struct Context {
    ID3D12Device* device=nullptr;
    ID3D12CommandQueue* queue=nullptr;
    reshade::api::swapchain* owner=nullptr;
    ID3D12Fence* fence=nullptr;
    ID3D12QueryHeap* timestamps=nullptr;
    unsigned long long timestamp_frequency=0;
    ID3D12RootSignature* util_root=nullptr;
    ID3D12PipelineState *convert=nullptr,*init_color=nullptr,*init_guides=nullptr,*blit=nullptr;
    ID3D12DescriptorHeap* io=nullptr;
    ID3D12Resource* raw=nullptr;
    // Initialized once, immutable across the same-size chain and retired with
    // its exact GPU generation. These are placeholders, never native evidence.
    ID3D12Resource *unknown_depth=nullptr,*unknown_motion=nullptr;
    D3D12_RESOURCE_STATES unknown_depth_state=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES unknown_motion_state=D3D12_RESOURCE_STATE_COMMON;
    ID3D12DescriptorHeap* unknown_guide_table=nullptr;
    D3D12_RESOURCE_STATES raw_state=D3D12_RESOURCE_STATE_COMMON;
    NrSlot image{};
#if NR_HAS_TEMPORAL_RUNTIME
    nr::final_residual::RecordedTemporal* temporal=nullptr;
    nr::final_residual::Source temporal_previous{},temporal_pending_source{};
    nr::final_residual::TemporalStats temporal_seen{};
    unsigned temporal_mode=0;
    unsigned long long temporal_serial=0,temporal_retry_tick=0;
#endif
    Layer layers[kPassLimit]{};
    Flight flights[kFlightCount]{};
    nr_dual::dx12::Pipelines* spatial_pipelines=nullptr;
    nr_compat_present::Timeline timeline{};
    NrTuningSignature tuning{};
    std::array<NrTuningSignature,kPassLimit> layer_tuning{};
    NrGuideBinding native_guides{};
    NrGuideSignature native_contract{};
    nr_source_frame::Source previous_source{};
    bool uses_native_guides=false;
    Extent source{},work{},native_output{};
    Encoding encoding=Encoding::Unknown;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    unsigned passes=1, numerator=1,denominator=1;
    unsigned motion_compensation=0; // frozen for this context and all its flights
    unsigned long long generation=0,bytes=0,create_fence=0;
    bool recorded=false,opaque_called=false;
    unsigned retirement_error=0;
};
Context* current=nullptr;
std::atomic<bool> has_context{false};
bool retirement_active=false; // chain-lock protected; blocks inline admission during API destruction
std::array<reshade::api::swapchain*,8> chains{};
bool chain_overflow=false;
Observation observation{}; // protected by g_chain_cs; snapshot copies before UI drawing
unsigned long long next_generation=1,source_serial=0;
unsigned stable_config=0,settled_presents=0;
unsigned failed_build_config=0;
thread_local bool inside=false;
struct Guard {Guard(){EnterCriticalSection(&g_chain_cs);}~Guard(){LeaveCriticalSection(&g_chain_cs);}};
struct Recursion {Recursion(){inside=true;}~Recursion(){inside=false;}};
Observation Snapshot(){Guard guard;return observation;}
bool TrySnapshotExperimentModes(unsigned& requested,unsigned& active,bool& has_active,bool& low_requested) {
    if(!TryEnterCriticalSection(&g_chain_cs))return false;
    requested=g_config.present_mv_compensation;
    has_active=current!=nullptr;active=current?current->motion_compensation:0;
    low_requested=g_config.descriptor_low_overhead;
    LeaveCriticalSection(&g_chain_cs);return true;
}
void Stamp(Context& c,Flight& f,unsigned local) {
    if(!c.timestamps)return;
    const auto index=static_cast<unsigned>(&f-c.flights)*16u+local;
    ID3D12GraphicsCommandList_EndQuery(f.list,c.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,index);
}
void ResolveTiming(Context& c,Flight& f) {
    if(!c.timestamps||!f.timing_readback)return;
    const auto index=static_cast<unsigned>(&f-c.flights)*16u;
    ID3D12GraphicsCommandList_ResolveQueryData(f.list,c.timestamps,D3D12_QUERY_TYPE_TIMESTAMP,
        index,5u+2u*c.passes,f.timing_readback,0);
    f.timing_resolved=true;
}
void SetStatus(Status s, unsigned result=0) {
    static unsigned long long last_log=0;
    const auto now=GetTickCount64();
    const bool changed=observation.status!=s||observation.result!=result;
    if(changed && (result!=observation.result||s==Status::Poisoned||now-last_log>=5000)){
        last_log=now;
        Log("[compat-post] status=%s result=0x%08X; input=%s Reset=%s; if bypassed save F8 and upload the bug report.",nr_compat_present::Name(s),result,observation.color_only?"color-only":"owned-current-native-guides",observation.reset_each_frame?"every-frame":"source-history");
    }
    observation.status=s;observation.result=result;
}
void Poison(Context& c,unsigned result,const char* stage) {
    c.timeline.Poison();c.recorded=true;
    for(auto& p:c.layers){p.context.lifecycle_dependencies_quarantined=true;p.context.failed=true;}
    MarkLifecycleUncertain("Compatibility Present private generation lost completion/provider proof");
    SetStatus(Status::Poisoned,result);
    Log("[compat-post] quarantined at %s; no further draw/submit/restore/NR retry on failed recording. Restart required.",stage);
}
bool RetireProof(void* user);
bool InlineDrainReady() {
    // Called only with the existing chain lock held by PRE/POST preparation.
    if(retirement_active)return false;
    if(!current)return true;
    return RetireProof(current);
}
unsigned LiveChains(){unsigned n=0;for(auto* s:chains)n+=s!=nullptr;return n;}
void OnInitSwapchain(reshade::api::swapchain* chain,bool) {
    if(!chain||!chain->get_device()||chain->get_device()->get_api()!=reshade::api::device_api::d3d12)return;
    Guard guard;for(auto* p:chains)if(p==chain)return;
    for(auto& p:chains)if(!p){p=chain;return;}chain_overflow=true;
}
Encoding SurfaceEncoding(reshade::api::swapchain* chain,DXGI_FORMAT format) {
    const auto color=chain->get_color_space();
    if(color==reshade::api::color_space::srgb &&
       (format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_R10G10B10A2_UNORM))return Encoding::Srgb;
    if(color==reshade::api::color_space::scrgb && format==DXGI_FORMAT_R16G16B16A16_FLOAT)return Encoding::ScRgb;
    if(color==reshade::api::color_space::hdr10_st2084 && format==DXGI_FORMAT_R10G10B10A2_UNORM)return Encoding::Pq2020;
    return Encoding::Unknown; // no format-only HDR inference
}
bool NativeNrDrained() {
    for(const auto& c:g_contexts)if(c.sr_handle &&
        (c.pending_recording_token||c.awaiting_fence_signal||!IsContextFenceCompleteLocked(c)))return false;
    return true;
}

ID3DBlob* CompileUtility(const char* entry,const char* target) {
    ID3DBlob *blob=nullptr,*errors=nullptr;
    const auto shader=std::strcmp(entry,"ConvertCS")==0?nr_aux_shader::Shader::PresentConvert:
        std::strcmp(entry,"InitColorCS")==0?nr_aux_shader::Shader::PresentInitColor:
        std::strcmp(entry,"InitCS")==0?nr_aux_shader::Shader::PresentInitGuides:
        std::strcmp(entry,"FullscreenVS")==0?nr_aux_shader::Shader::PresentFullscreen:nr_aux_shader::Shader::PresentBlit;
#if NR_AUX_PRECOMPILED
    (void)shader;const auto artifact=nr_aux_present_precompiled::Find(entry,target);
    const HRESULT hr=nr_aux_shader::Load(artifact.data,artifact.size,&blob);
#else
    const HRESULT hr=nr_aux_shader::Measure(shader,nr_aux_shader::Operation::Compile,[&]{return D3DCompile(kCompatPresentShader,sizeof(kCompatPresentShader)-1,
        "own-compat-present",nullptr,nullptr,entry,target,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);});
#endif
    if(FAILED(hr))Log("[compat-post] utility shader %s failed: 0x%08X %s",entry,
        static_cast<unsigned>(hr),errors?static_cast<const char*>(ID3D10Blob_GetBufferPointer(errors)):"no diagnostic");
    ReleaseCom(errors);return SUCCEEDED(hr)?blob:(ReleaseCom(blob),nullptr);
}
bool MakePipelines(Context& c) {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};
    ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,3};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable={2,ranges};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants={0,0,4};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root{};root.NumParameters=2;root.pParameters=params;
    ID3DBlob *serialized=nullptr,*error=nullptr;
    auto hr=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error);
    ReleaseCom(error);
    if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(c.device,0,
        ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),
        IID_ID3D12RootSignature,reinterpret_cast<void**>(&c.util_root));
    ReleaseCom(serialized);if(FAILED(hr))return false;
    ID3DBlob *convert=CompileUtility("ConvertCS","cs_5_0"),
        *vs=CompileUtility("FullscreenVS","vs_5_0"),*ps=CompileUtility("BlitPS","ps_5_0");
    if(!convert||!vs||!ps){ReleaseCom(convert);ReleaseCom(vs);ReleaseCom(ps);return false;}
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};compute.pRootSignature=c.util_root;
    compute.CS={ID3D10Blob_GetBufferPointer(convert),ID3D10Blob_GetBufferSize(convert)};
    hr=nr_aux_shader::Measure(nr_aux_shader::Shader::PresentConvert,nr_aux_shader::Operation::Pipeline,[&]{return ID3D12Device_CreateComputePipelineState(c.device,&compute,IID_ID3D12PipelineState,reinterpret_cast<void**>(&c.convert));});
    ID3DBlob* init_color=CompileUtility("InitColorCS","cs_5_0");
    if(!init_color)hr=E_FAIL;
    if(SUCCEEDED(hr)){compute.CS={ID3D10Blob_GetBufferPointer(init_color),ID3D10Blob_GetBufferSize(init_color)};
        hr=nr_aux_shader::Measure(nr_aux_shader::Shader::PresentInitColor,nr_aux_shader::Operation::Pipeline,[&]{return ID3D12Device_CreateComputePipelineState(c.device,&compute,IID_ID3D12PipelineState,reinterpret_cast<void**>(&c.init_color));});}
    ReleaseCom(init_color);
    if(SUCCEEDED(hr)&&!c.uses_native_guides){
        ID3DBlob* init=CompileUtility("InitCS","cs_5_0");
        if(!init)hr=E_FAIL;
        else {compute.CS={ID3D10Blob_GetBufferPointer(init),ID3D10Blob_GetBufferSize(init)};
            hr=nr_aux_shader::Measure(nr_aux_shader::Shader::PresentInitGuides,nr_aux_shader::Operation::Pipeline,[&]{return ID3D12Device_CreateComputePipelineState(c.device,&compute,IID_ID3D12PipelineState,reinterpret_cast<void**>(&c.init_guides));});}
        ReleaseCom(init);
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC draw{};draw.pRootSignature=c.util_root;
    draw.VS={ID3D10Blob_GetBufferPointer(vs),ID3D10Blob_GetBufferSize(vs)};
    draw.PS={ID3D10Blob_GetBufferPointer(ps),ID3D10Blob_GetBufferSize(ps)};
    auto& blend=draw.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    draw.SampleMask=UINT_MAX;draw.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    draw.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;draw.RasterizerState.DepthClipEnable=TRUE;
    draw.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;draw.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    auto& front=draw.DepthStencilState.FrontFace;front.StencilFailOp=front.StencilDepthFailOp=front.StencilPassOp=D3D12_STENCIL_OP_KEEP;
    front.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;draw.DepthStencilState.BackFace=front;
    draw.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    draw.NumRenderTargets=1;draw.RTVFormats[0]=c.format;draw.SampleDesc.Count=1;
    if(SUCCEEDED(hr))hr=nr_aux_shader::Measure(nr_aux_shader::Shader::PresentBlit,nr_aux_shader::Operation::Pipeline,[&]{return ID3D12Device_CreateGraphicsPipelineState(c.device,&draw,IID_ID3D12PipelineState,reinterpret_cast<void**>(&c.blit));});
    ReleaseCom(convert);ReleaseCom(vs);ReleaseCom(ps);return SUCCEEDED(hr);
}
bool Texture(Context& c,Extent e,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,ID3D12Resource** out) {
    if(!nr_compat_present::Valid(e))return false;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=e.width;d.Height=e.height;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;d.Flags=flags;
    const auto allocation=ID3D12Device_GetResourceAllocationInfo(c.device,0,1,&d);
    if(!nr_compat_present::Fits(c.bytes,allocation.SizeInBytes,kOwnedBudget))return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    if(FAILED(ID3D12Device_CreateCommittedResource(c.device,&heap,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(out))))return false;
    c.bytes+=allocation.SizeInBytes;return true;
}
bool Table(Context& c,ID3D12DescriptorHeap** output,ID3D12Resource* rgba,
    ID3D12Resource* depth=nullptr,ID3D12Resource* motion=nullptr) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors=6;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(ID3D12Device_CreateDescriptorHeap(c.device,&desc,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(output))))return false;
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(*output);
    const auto step=ID3D12Device_GetDescriptorHandleIncrementSize(c.device,desc.Type);
    ID3D12Resource* reads[]={c.raw,c.image.source,c.image.result};
    for(unsigned i=0;i<3;++i){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=i?DXGI_FORMAT_R16G16B16A16_FLOAT:c.format;
        s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MipLevels=1;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ID3D12Device_CreateShaderResourceView(c.device,reads[i],&s,cpu);cpu.ptr+=step;}
    ID3D12Resource* writes[]={rgba,depth,motion};
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
    for(unsigned i=0;i<3;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=formats[i];u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(c.device,writes[i],nullptr,&u,cpu);cpu.ptr+=step;}
    return true;
}
void Utility(Context& c,ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* table,ID3D12PipelineState* shader,Extent e) {
    ID3D12DescriptorHeap* heaps[]={table};ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,c.util_root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(table));
    const unsigned constants[]={e.width,e.height,static_cast<unsigned>(c.encoding),0};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,4,constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list,shader);ID3D12GraphicsCommandList_Dispatch(list,(e.width+7)/8,(e.height+7)/8,1);
}
