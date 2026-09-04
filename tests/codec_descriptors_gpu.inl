// The real production descriptor writers are compiled here. These wrappers
// adapt SDK C helper spelling to this harness's C++ COM interfaces only.
#define ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(h) (h)->GetCPUDescriptorHandleForHeapStart()
#define ID3D12Resource_GetDesc(r) (r)->GetDesc()
#define ID3D12Device_CreateShaderResourceView(d,r,s,h) (d)->CreateShaderResourceView(r,s,h)
#define ID3D12Device_CreateUnorderedAccessView(d,r,c,s,h) (d)->CreateUnorderedAccessView(r,c,s,h)
struct NrSlot {
    ID3D12Resource *source=nullptr,*proxy=nullptr,*neural=nullptr,*motion_source=nullptr;
    ID3D12Resource *exposure_source=nullptr,*result=nullptr,*motion_proxy=nullptr,*delta_stats=nullptr;
    ID3D12DescriptorHeap *snapshot_heap=nullptr;
    DXGI_FORMAT source_format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT result_format=DXGI_FORMAT_UNKNOWN,result_uav_format=DXGI_FORMAT_UNKNOWN;
};
struct {UINT descriptor_size=0;} g_codec;
// @PRODUCTION_DESCRIPTOR_WRITER@
#undef ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart
#undef ID3D12Resource_GetDesc
#undef ID3D12Device_CreateShaderResourceView
#undef ID3D12Device_CreateUnorderedAccessView

Texture MakeTypedTexture(Gpu &g,DXGI_FORMAT format,UINT width,UINT height,
                         const std::vector<std::uint8_t>& pixels,UINT bytes_per_pixel) {
    Texture t;t.width=width;t.height=height;
    D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Check(g.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&d,t.state,nullptr,
        IID_ID3D12Resource,t.resource.address()),"Typed descriptor fixture texture");
    UINT64 bytes=0;g.device->GetCopyableFootprints(&d,0,1,0,&t.footprint,nullptr,nullptr,&bytes);
    t.upload=g.Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    t.readback=g.Buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    void*m=nullptr;Check(t.upload->Map(0,nullptr,&m),"Typed upload map");
    for(UINT y=0;y<height;++y)std::memcpy(static_cast<std::uint8_t*>(m)+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,
        pixels.data()+y*width*bytes_per_pixel,width*bytes_per_pixel);
    t.upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=t.upload.p;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=t.footprint;
    to.pResource=t.resource.p;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);return t;
}

float HalfToFloat(std::uint16_t v) {
    const unsigned e=(v>>10)&31,m=v&1023;
    const float value=e?std::ldexp(1.f+m/1024.f,static_cast<int>(e)-15):std::ldexp(m/1024.f,-14);
    return v&0x8000?-value:value;
}

void ValidateProductionDescriptors(Gpu &g) {
    g_codec.descriptor_size=g.increment;
    Com<ID3DBlob> code,errors;
    Check(D3DCompile(kCodecShader,sizeof(kCodecShader)-1,"production-CopyCS",nullptr,nullptr,
        "CopyCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.p,&errors.p),"Compile production CopyCS");
    Com<ID3D12PipelineState> copy;
    D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=g.root.p;p.CS={code->GetBufferPointer(),code->GetBufferSize()};
    Check(g.device->CreateComputePipelineState(&p,IID_ID3D12PipelineState,copy.address()),"Copy pipeline");
    unsigned scenarios=0;
    for(unsigned profile=0;profile<3;++profile) for(bool negative:{false,true}) {
        constexpr UINT sw=11,sh=9,w=5,h=3,sx=3,sy=2,dw=8,dh=6;
        const DXGI_FORMAT format=profile==0?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
        std::vector<std::uint8_t> packed(sw*sh*4);
        for(UINT y=0;y<sh;++y)for(UINT x=0;x<sw;++x){
            const std::uint32_t value=profile==0?((x&1?896u:960u)|(896u<<11)|(416u<<22)):
                ((x&1?32u:64u)|(128u<<8)|(192u<<16)|(153u<<24));
            std::memcpy(packed.data()+(y*sw+x)*4,&value,4);
        }
        // FP16 sentinel -2 outside the destination active rectangle.
        std::vector<std::uint16_t> half(dw*dh*4,0xc000u);
        std::vector<std::uint8_t> half_bytes(half.size()*2);std::memcpy(half_bytes.data(),half.data(),half_bytes.size());
        auto external=MakeTypedTexture(g,format,sw,sh,packed,4);
        auto intermediate=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,dw,dh,half_bytes,8);
        NrSlot slot{};slot.source=intermediate.resource.p;slot.result=external.resource.p;
        slot.result_format=slot.result_uav_format=format;slot.snapshot_heap=g.heap.p;
        WritePostSrSnapshotDescriptors(g.device.p,slot);
        if(negative){ // valid but wrong t0 reproduces the omitted post-SR override.
            auto wrong=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,sw,sh,
                std::vector<std::uint8_t>(sw*sh*8,0),8);
            g.TextureState(wrong,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            g.device->CreateShaderResourceView(wrong.resource.p,&srv,g.Cpu(0));
            // Keep its resource/upload alive through the exact execute fence.
            g.TextureState(external,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            g.TextureState(intermediate,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CodecConstants c={};c.width=w;c.height=h;c.source_base_x=sx;c.source_base_y=sy;
            ID3D12DescriptorHeap*heaps[]={g.heap.p};g.list->SetDescriptorHeaps(1,heaps);g.list->SetComputeRootSignature(g.root.p);
            g.list->SetComputeRootDescriptorTable(0,g.heap->GetGPUDescriptorHandleForHeapStart());g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);
            g.list->SetPipelineState(copy.p);g.list->Dispatch(1,1,1);g.ReadTexture(intermediate);g.ReadTexture(external);g.Execute();
        }else{
            g.TextureState(external,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g.TextureState(intermediate,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CodecConstants c={};c.width=w;c.height=h;c.source_base_x=sx;c.source_base_y=sy;
            ID3D12DescriptorHeap*heaps[]={g.heap.p};g.list->SetDescriptorHeaps(1,heaps);g.list->SetComputeRootSignature(g.root.p);
            g.list->SetComputeRootDescriptorTable(0,g.heap->GetGPUDescriptorHandleForHeapStart());g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);
            g.list->SetPipelineState(copy.p);g.list->Dispatch(1,1,1);g.ReadTexture(intermediate);g.ReadTexture(external);g.Execute();
        }
        void*m=nullptr;Check(intermediate.readback->Map(0,nullptr,&m),"FP16 snapshot readback");
        bool matched=true;
        for(UINT y=0;y<dh;++y)for(UINT x=0;x<dw;++x){
            const auto*v=reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(m)+intermediate.footprint.Offset+y*intermediate.footprint.Footprint.RowPitch)+x*4;
            if(x>=w||y>=h){for(unsigned k=0;k<4;++k)Require(v[k]==0xc000u,"Snapshot escaped active extent");continue;}
            const float expected[]={profile==0?((x+sx)&1?.5f:1.f):((x+sx)&1?32.f/255:64.f/255),profile==0?.5f:128.f/255,profile==0?.25f:192.f/255,profile==0?1.f:153.f/255};
            for(unsigned k=0;k<4;++k)matched=matched&&std::fabs(HalfToFloat(v[k])-expected[k])<.001f;
        }
        intermediate.readback->Unmap(0,nullptr);Require(matched!=negative,"Production snapshot t0/u1 output oracle failed");
        Check(external.readback->Map(0,nullptr,&m),"Original Output readback");
        for(UINT y=0;y<sh;++y)Require(std::memcmp(static_cast<const std::uint8_t*>(m)+external.footprint.Offset+y*external.footprint.Footprint.RowPitch,packed.data()+y*sw*4,sw*4)==0,"Snapshot modified native Output bytes");
        external.readback->Unmap(0,nullptr);++scenarios;
    }
    std::printf("Production descriptor writers: %u R11 / R8-linear / R8-sRGB-profile CopyCS scenarios PASS; actual typed views, FP16 destination, offset/extent, unchanged native bytes, wrong-t0 negative controls.\n",scenarios);
}
