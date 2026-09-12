// Included inside nr_source_runtime, before GuideCopy. This path runs only at
// CaptureNgxGuides' native NGX input seam, never at an arbitrary Present/FG seam.
// Read the real Depth/MV with the same shader-readable input contract as NGX.
// No guessed StateBefore, source barrier, dummy guide, resampling or MV scaling.
// A captured native command-state envelope restores every changed binding.
struct NativeGuideSnapshotObjects {
    ID3D12RootSignature* root=nullptr;
    ID3D12PipelineState* pipeline=nullptr;
    ID3D12DescriptorHeap* heap=nullptr;
};
void ReleaseNativeGuideSnapshot(NativeGuideSnapshotObjects& objects) {
    ReleaseCom(objects.heap);ReleaseCom(objects.pipeline);ReleaseCom(objects.root);
}
constexpr char kNativeGuideSnapshotShader[]=R"HLSL(
cbuffer SnapshotConstants : register(b0) {
    uint2 DepthSize; uint2 MotionSize;
    uint2 DepthOrigin; uint2 MotionOrigin;
};
Texture2D<float> NativeDepth : register(t0);
Texture2D<float2> NativeMotion : register(t1);
RWTexture2D<float> OwnedDepth : register(u0);
RWTexture2D<float2> OwnedMotion : register(u1);
[numthreads(8,8,1)]
void SnapshotGuidesCS(uint3 tid : SV_DispatchThreadID) {
    if(all(tid.xy<DepthSize)) OwnedDepth[tid.xy]=NativeDepth.Load(int3(tid.xy+DepthOrigin,0));
    if(all(tid.xy<MotionSize)) OwnedMotion[tid.xy]=NativeMotion.Load(int3(tid.xy+MotionOrigin,0));
}
)HLSL";
DXGI_FORMAT NativeGuideDepthView(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT NativeGuideMotionView(DXGI_FORMAT format) {
    // Integer/ambiguous typeless MV is not guessed. Existing provider adapters
    // must establish its interpretation before this snapshot can support it.
    switch(format) {
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: return format;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT NativeGuideMotionCopyFormat(DXGI_FORMAT format) {
    // Do not inflate half-precision MV to FP32: the shader performs no vector
    // arithmetic, and the original numerical precision is sufficient.
    if(format==DXGI_FORMAT_R16G16_FLOAT||format==DXGI_FORMAT_R16G16B16A16_FLOAT)return DXGI_FORMAT_R16G16_FLOAT;
    return DXGI_FORMAT_R32G32_FLOAT;
}
bool NativeGuideRectFits(const D3D12_RESOURCE_DESC& d,unsigned x,unsigned y,unsigned w,unsigned h) {
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.DepthOrArraySize==1&&d.MipLevels==1&&
        d.SampleDesc.Count==1&&w&&h&&w<=16384&&h<=16384&&d.Width<=16384&&d.Height<=16384&&
        x<=d.Width&&y<=d.Height&&w<=d.Width-x&&h<=d.Height-y&&
        (d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)==0;
}
bool NativeGuideViewSupported(ID3D12Device* device,DXGI_FORMAT format,bool write) {
    if(!device||format==DXGI_FORMAT_UNKNOWN)return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT s{};s.Format=format;
    if(FAILED(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&s,sizeof(s))))return false;
    return (s.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)&&(s.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)&&
        (!write||((s.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)&&
            (s.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)));
}
bool BuildNativeGuideSnapshot(ID3D12Device* device,NativeGuideSnapshotObjects& out) {
    if(out.root&&out.pipeline&&out.heap)return true;
    ReleaseNativeGuideSnapshot(out);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=2;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=2;
    ranges[1].OffsetInDescriptorsFromTableStart=2;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={2,ranges};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,8};
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
    ID3DBlob* signature=nullptr;ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    HRESULT hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors);
    ReleaseCom(errors);
    if(SUCCEEDED(hr))hr=ID3D12Device_CreateRootSignature(device,0,
        signature->lpVtbl->GetBufferPointer(signature),signature->lpVtbl->GetBufferSize(signature),
        IID_ID3D12RootSignature,reinterpret_cast<void**>(&out.root));
    ReleaseCom(signature);
    if(SUCCEEDED(hr))hr=D3DCompile(kNativeGuideSnapshotShader,sizeof(kNativeGuideSnapshotShader)-1,
        "NR.NativeGuideSnapshot",nullptr,nullptr,"SnapshotGuidesCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    ReleaseCom(errors);
    if(SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=out.root;
        pipeline.CS={code->lpVtbl->GetBufferPointer(code),code->lpVtbl->GetBufferSize(code)};
        hr=ID3D12Device_CreateComputePipelineState(device,&pipeline,IID_ID3D12PipelineState,
            reinterpret_cast<void**>(&out.pipeline));
    }
    ReleaseCom(code);
    if(SUCCEEDED(hr)) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors=4;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=ID3D12Device_CreateDescriptorHeap(device,&heap,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(&out.heap));
    }
    if(FAILED(hr)){ReleaseNativeGuideSnapshot(out);return false;}
    return true;
}
bool MakeNativeGuidePlane(ID3D12Device* device,unsigned width,unsigned height,DXGI_FORMAT format,
    ID3D12Resource*& output,Id& bytes,Id budget) {
    if(!width||!height||width>16384||height>16384||bytes>budget)return false;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto allocation=ID3D12Device_GetResourceAllocationInfo(device,0,1,&d);
    if(!allocation.SizeInBytes||allocation.SizeInBytes==UINT64_MAX||allocation.SizeInBytes>budget-bytes)return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    if(FAILED(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&output))))return false;
    bytes+=allocation.SizeInBytes;return true;
}
void BindNativeGuideSnapshot(ID3D12Device* device,NativeGuideSnapshotObjects& objects,
    const NrGuideBinding& input,ID3D12Resource* depth,ID3D12Resource* motion) {
    auto cpu=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(objects.heap);
    const auto stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const DXGI_FORMAT input_formats[]={NativeGuideDepthView(ID3D12Resource_GetDesc(input.depth).Format),
        NativeGuideMotionView(ID3D12Resource_GetDesc(input.motion).Format)};
    ID3D12Resource* inputs[]={input.depth,input.motion};ID3D12Resource* outputs[]={depth,motion};
    const DXGI_FORMAT output_formats[]={ID3D12Resource_GetDesc(depth).Format,ID3D12Resource_GetDesc(motion).Format};
    for(unsigned i=0;i<2;++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=input_formats[i];
        view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels=1;view.Texture2D.PlaneSlice=0;
        ID3D12Device_CreateShaderResourceView(device,inputs[i],&view,cpu);cpu.ptr+=stride;
    }
    for(unsigned i=0;i<2;++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=output_formats[i];view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(device,outputs[i],nullptr,&view,cpu);cpu.ptr+=stride;
    }
}
void RecordNativeGuideSnapshot(ID3D12GraphicsCommandList* list,NativeGuideSnapshotObjects& objects,
    const NrGuideBinding& input,ID3D12Resource* depth,ID3D12Resource* motion,unsigned (&owned_state)[2]) {
    const auto& s=input.signature;
    const unsigned constants[]={s.depth_width,s.depth_height,s.motion_source_width,s.motion_source_height,
        s.depth_x,s.depth_y,s.motion_x,s.motion_y};
    ID3D12Resource* outputs[]={depth,motion};
    for(unsigned i=0;i<2;++i) {
        auto state=static_cast<D3D12_RESOURCE_STATES>(owned_state[i]);
        Transition(list,outputs[i],state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);owned_state[i]=static_cast<unsigned>(state);
    }
    // The native state envelope restores the application predicate afterwards.
    ID3D12GraphicsCommandList_SetPredication(list,nullptr,0,D3D12_PREDICATION_OP_EQUAL_ZERO);
    ID3D12DescriptorHeap* heaps[]={objects.heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,objects.root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(objects.heap));
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,constants,0);
    ID3D12GraphicsCommandList_SetPipelineState(list,objects.pipeline);
    ID3D12GraphicsCommandList_Dispatch(list,(std::max(s.depth_width,s.motion_source_width)+7)/8,
        (std::max(s.depth_height,s.motion_source_height)+7)/8,1);
    for(unsigned i=0;i<2;++i) {
        auto state=static_cast<D3D12_RESOURCE_STATES>(owned_state[i]);
        Transition(list,outputs[i],state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);owned_state[i]=static_cast<unsigned>(state);
    }
}
