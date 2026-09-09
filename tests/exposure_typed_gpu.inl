// Uses the existing codec GPU host, real production descriptor writers and
// Encode/Combine entry points. No NGX/model inference or game execution.
#define ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(h) (h)->GetCPUDescriptorHandleForHeapStart()
#define ID3D12Resource_GetDesc(r) (r)->GetDesc()
#define ID3D12Resource_AddRef(r) (r)->AddRef()
#define ID3D12Device_CreateShaderResourceView(d,r,s,h) (d)->CreateShaderResourceView(r,s,h)
#define ReleaseCom(r) do { if (r) { (r)->Release(); (r)=nullptr; } } while (0)
// @PRODUCTION_EXPOSURE_REBIND@
#undef ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart
#undef ID3D12Resource_GetDesc
#undef ID3D12Resource_AddRef
#undef ID3D12Device_CreateShaderResourceView
#undef ReleaseCom

struct TypedExposureResult {
    std::vector<std::uint8_t> proxy;
    std::vector<Float4> output;
};

Com<ID3D12PipelineState> ExposureEncodePipeline(Gpu &gpu, bool before) {
    Com<ID3DBlob> code, errors;
    const HRESULT hr=D3DCompile(kCodecShader,sizeof(kCodecShader)-1,
        "production-exposure-encode",nullptr,nullptr,before?"EncodeV2CS":"EncodeCS",
        "cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.p,&errors.p);
    if(FAILED(hr)&&errors.p) std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"Compile production exposure Encode");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature=gpu.root.p; desc.CS={code->GetBufferPointer(),code->GetBufferSize()};
    Com<ID3D12PipelineState> result;
    Check(gpu.device->CreateComputePipelineState(&desc,IID_ID3D12PipelineState,result.address()),
          "Create exposure Encode pipeline");
    return result;
}

std::vector<std::uint8_t> ExposureTextureBytes(Texture &texture, UINT bytes_per_pixel) {
    std::vector<std::uint8_t> bytes(texture.width*texture.height*bytes_per_pixel);
    void *mapped=nullptr;
    Check(texture.readback->Map(0,nullptr,&mapped),"Map typed exposure fixture texture");
    for(UINT y=0;y<texture.height;++y)
        std::memcpy(bytes.data()+y*texture.width*bytes_per_pixel,
            static_cast<const std::uint8_t*>(mapped)+texture.footprint.Offset+y*texture.footprint.Footprint.RowPitch,
            texture.width*bytes_per_pixel);
    texture.readback->Unmap(0,nullptr);
    return bytes;
}

TypedExposureResult RunTypedExposure(Gpu &gpu, ID3D12PipelineState *encode,
                                    bool before, unsigned byte, bool r8,
                                    unsigned binding, float pre_exposure) {
    constexpr UINT w=8,h=8;
    // binding: ordinary heap, rebuilt snapshot heap, rebound ordinary/snapshot.
    Require(binding<4,"unknown typed exposure binding case");
    const float raw=byte/255.f;
    std::vector<std::uint8_t> exposure_bytes(r8?1u:4u);
    if(r8) exposure_bytes[0]=static_cast<std::uint8_t>(byte);
    else std::memcpy(exposure_bytes.data(),&raw,sizeof(raw));
    auto exposure=MakeTypedTexture(gpu,r8?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R32_FLOAT,
        1,1,exposure_bytes,r8?1u:4u,D3D12_RESOURCE_FLAG_NONE);
    const float stale=0.875f;
    std::vector<std::uint8_t> stale_bytes(4);
    std::memcpy(stale_bytes.data(),&stale,4);
    auto previous=MakeTypedTexture(gpu,DXGI_FORMAT_R32_FLOAT,1,1,stale_bytes,4,D3D12_RESOURCE_FLAG_NONE);
    Require(exposure.resource->GetDesc().Flags==D3D12_RESOURCE_FLAG_NONE,
            "exposure fixture must not require source UAV flags");
    std::vector<Float4> original(w*h);
    for(UINT i=0;i<w*h;++i) original[i]={.25f+(i%7)*.03125f,.375f,.5f,.625f};
    auto source=gpu.MakeTexture(w,h,original);
    auto output=gpu.MakeTexture(w,h,std::vector<Float4>(w*h,Float4{-1,-1,-1,-1}));
    auto proxy=MakeTypedTexture(gpu,DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,
        std::vector<std::uint8_t>(w*h*8,0),8);
    // A fixed nonzero neural plane makes a missing exposure transform visible
    // in the final output; it is not an NR model result.
    std::vector<std::uint16_t> neural_half(w*h*4,0x3800u);
    std::vector<std::uint8_t> neural_bytes(neural_half.size()*2);
    std::memcpy(neural_bytes.data(),neural_half.data(),neural_bytes.size());
    auto neural=MakeTypedTexture(gpu,DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,neural_bytes,8);
    gpu.TextureState(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(exposure,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(previous,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(proxy,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    gpu.TextureState(neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.TextureState(output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Com<ID3D12DescriptorHeap> snapshot;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors=kCodecDescriptorCount;
    heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(gpu.device->CreateDescriptorHeap(&heap_desc,IID_ID3D12DescriptorHeap,snapshot.address()),
          "Create exposure snapshot heap");
    NrSlot slot{};
    slot.source=source.resource.p; slot.source_format=slot.source_srv_format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    slot.proxy=proxy.resource.p; slot.neural=neural.resource.p;
    slot.result=output.resource.p; slot.result_format=slot.result_uav_format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    slot.heap=gpu.heap.p; slot.snapshot_heap=snapshot.p;
    slot.exposure_source=binding>=2?previous.resource.p:exposure.resource.p;
    slot.exposure_source->AddRef();
    WriteCodecDescriptorSet(gpu.device.p,slot,slot.heap);
    WriteCodecDescriptorSet(gpu.device.p,slot,slot.snapshot_heap);
    if(binding>=2) RebindExposureSource(gpu.device.p,exposure.resource.p,slot);
    Require(slot.exposure_source==exposure.resource.p,"exposure rebind retained stale resource");
    ID3D12DescriptorHeap *selected=(binding&1u)?slot.snapshot_heap:slot.heap;
    constexpr UINT64 capture_bytes=w*h*4u*16u+16u;
    auto capture=gpu.Buffer(capture_bytes,D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto capture_readback=gpu.Buffer(capture_bytes,D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_UNORDERED_ACCESS_VIEW_DESC capture_view{};
    capture_view.Format=DXGI_FORMAT_R32_TYPELESS; capture_view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    capture_view.Buffer.NumElements=static_cast<UINT>(capture_bytes/4);
    capture_view.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
    auto capture_handle=selected->GetCPUDescriptorHandleForHeapStart();
    capture_handle.ptr+=static_cast<SIZE_T>(gpu.increment)*kCodecCaptureUavIndex;
    gpu.device->CreateUnorderedAccessView(capture.p,nullptr,&capture_view,capture_handle);
    CodecConstants constants{};
    constants.width=w; constants.height=h;
    constants.source_width=w; constants.source_height=h;
    constants.model_width=w; constants.model_height=h;
    constants.proxy_scale=1.f; constants.transfer_strength=.8f;
    constants.source_to_canonical=.375f; // Deliberately differs from live SRV.
    constants.color_strength=0.f; constants.max_luma_ratio=2.f;
    constants.passthrough=64u; constants.pre_exposure=pre_exposure;
    constants.proxy_curve=1u; constants.frame_confidence=1.f;
    const auto dispatch=[&](ID3D12PipelineState *pipeline) {
        ID3D12DescriptorHeap *heaps[]={selected};
        gpu.list->SetDescriptorHeaps(1,heaps); gpu.list->SetComputeRootSignature(gpu.root.p);
        gpu.list->SetComputeRootDescriptorTable(0,selected->GetGPUDescriptorHandleForHeapStart());
        gpu.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&constants,0);
        gpu.list->SetPipelineState(pipeline); gpu.list->Dispatch(1,1,1);
    };
    dispatch(encode);
    gpu.ReadTexture(proxy); gpu.Execute();
    const auto proxy_bytes=ExposureTextureBytes(proxy,8);
    gpu.TextureState(proxy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    constants.capture_stride=1;
    dispatch(before?gpu.v2_pipeline.p:gpu.pipeline.p);
    gpu.ReadTexture(output); gpu.ReadTexture(source); gpu.ReadTexture(exposure);
    gpu.Barrier(capture.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(capture_readback.p,0,capture.p,0,capture_bytes);
    gpu.Execute();
    Require(ExposureTextureBytes(exposure,r8?1u:4u)==exposure_bytes,
            "codec changed the native exposure texture bytes");
    const auto native=gpu.TexturePixels(source);
    Require(std::memcmp(native.data(),original.data(),original.size()*sizeof(Float4))==0,
            "exposure codec changed the native input texture");
    void *mapped=nullptr; Check(capture_readback->Map(0,nullptr,&mapped),"Map exposure trailer");
    float trailer[4]; std::memcpy(trailer,static_cast<std::uint8_t*>(mapped)+capture_bytes-16,16);
    capture_readback->Unmap(0,nullptr);
    // Fixed numeric oracle independent of the format resolver. R8 zero uses
    // the existing invalid/nonpositive exposure fallback; no policy change.
    const float resolved=byte?raw:1.f;
    const float expected[]={raw,resolved,std::clamp(resolved/pre_exposure,1.f/1024.f,1024.f),pre_exposure};
    for(unsigned i=0;i<4;++i) Require(std::isfinite(trailer[i])&&std::fabs(trailer[i]-expected[i])<.000001f,
        "TYPED_EXPOSURE_VALUE_ORACLE: normalized red/resolved/canonical/pre mismatch");
    slot.exposure_source->Release(); slot.exposure_source=nullptr;
    return {proxy_bytes,gpu.TexturePixels(output)};
}

void ValidateTypedExposure(Gpu &gpu) {
    g_codec.descriptor_size=gpu.increment;
    unsigned cases=0;
    for(bool before:{false,true}) {
        auto encode=ExposureEncodePipeline(gpu,before);
        for(unsigned byte:{0u,1u,64u,128u,255u}) for(unsigned binding=0;binding<4;++binding)
        for(float pre:{.5f,2.f}) {
            const auto typed=RunTypedExposure(gpu,encode.p,before,byte,true,binding,pre);
            const auto reference=RunTypedExposure(gpu,encode.p,before,byte,false,binding,pre);
            Require(typed.proxy.size()==reference.proxy.size(),"Exposure proxy dimensions");
            // Compare actual FP16 model inputs, not a separate test shader.
            for(size_t i=0;i<typed.proxy.size();i+=2) {
                std::uint16_t a,b; std::memcpy(&a,typed.proxy.data()+i,2); std::memcpy(&b,reference.proxy.data()+i,2);
                Require(std::fabs(HalfToFloat(a)-HalfToFloat(b))<.001f,
                        "typed R8 Encode differs from normalized R32_FLOAT reference");
            }
            Require(typed.output.size()==reference.output.size(),"Exposure output dimensions");
            for(size_t i=0;i<typed.output.size();++i) {
                float a[4],b[4];
                std::memcpy(a,&typed.output[i],sizeof(a)); std::memcpy(b,&reference.output[i],sizeof(b));
                for(unsigned c=0;c<4;++c) Require(std::isfinite(a[c])&&std::fabs(a[c]-b[c])<.0001f,
                    "typed R8 Combine differs from normalized R32_FLOAT reference");
            }
            ++cases;
        }
    }
    std::printf("PASS typed exposure: %u R8/R32 pairs; actual pre/post Encode+Combine, main/snapshot rebuild/rebind, normalized red, zero fallback, native bytes retained. No NGX/game inference.\n",cases);
}
