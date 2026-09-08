// Input-only production shader fixture. No Feature18, Neural reuse or SR call.
#include <filesystem>
#include <fstream>
#include <iomanip>

struct InputFilterPipelines {
    Com<ID3D12PipelineState> area,horizontal,vertical;
#ifdef NR_INPUT_FILTER_BASELINE_SOURCE_SHA
    Com<ID3D12PipelineState> old_area;
#endif
    static Com<ID3D12PipelineState> Compile(Gpu&g,const char*source,const char*entry) {
        Com<ID3DBlob>code,errors;
        const HRESULT hr=D3DCompile(source,std::strlen(source),entry,nullptr,nullptr,entry,"cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code.p,&errors.p);
        if(FAILED(hr)&&errors.p)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr,"Compile actual input filter");
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=g.root.p;
        d.CS={code->GetBufferPointer(),code->GetBufferSize()};Com<ID3D12PipelineState>p;
        Check(g.device->CreateComputePipelineState(&d,IID_ID3D12PipelineState,p.address()),"Input filter pipeline");return p;
    }
    explicit InputFilterPipelines(Gpu&g) {
        area=Compile(g,kCodecShader,"EncodeV2CS");
        horizontal=Compile(g,kCodecShader,"LanczosHorizontalCS");vertical=Compile(g,kCodecShader,"LanczosVerticalCS");
#ifdef NR_INPUT_FILTER_BASELINE_SOURCE_SHA
        old_area=Compile(g,kMaintenanceShader,"EncodeV2CS");
#endif
    }
};

struct InputFilterReadback {std::vector<Float4>source;std::vector<std::uint8_t>proxy,intermediate;};

std::vector<std::uint8_t> ReadHalfPlane(Gpu&g,Texture&t) {
    (void)g;std::vector<std::uint8_t>out(t.width*t.height*8);void*data=nullptr;
    Check(t.readback->Map(0,nullptr,&data),"Read completed FP16 plane");
    for(UINT y=0;y<t.height;++y)std::memcpy(out.data()+y*t.width*8,
        static_cast<std::uint8_t*>(data)+t.footprint.Offset+y*t.footprint.Footprint.RowPitch,t.width*8);
    t.readback->Unmap(0,nullptr);return out;
}

InputFilterReadback RunInputFilter(Gpu&g,InputFilterPipelines&p,UINT nw,UINT nh,UINT flags,int pattern,int filter) {
    constexpr UINT rw=31,rh=23,sx=3,sy=2,sw=36,sh=27;
    std::vector<Float4>source(sw*sh,Float4{4096,2048,1024,.125f});
    const float peak=flags==0?8.f:1.f;
    for(UINT y=0;y<rh;++y)for(UINT x=0;x<rw;++x){
        Float4 v{.25f,.5f,.75f,.625f};
        if(pattern==1){float q=(x==rw/2&&y==rh/2)?peak:0.f;v={q,q,q,.625f};}
        if(pattern==2){float q=x>=rw/2?peak:0.f;v={q,q,q,.625f};}
        if(pattern==3){float q=(x+y)%2?peak:0.f;v={q,q,q,.625f};}
        if(pattern==4){float q=x==rw/2?peak:0.f;v={q,q,q,.625f};}
        if(pattern==5)v={.15f+.7f*x/(rw-1),.2f+.6f*y/(rh-1),.3f+.4f*(x+y)/(rw+rh-2),.625f};
        if(pattern==6&&x==rw/2&&y==rh/2)v.x=std::numeric_limits<float>::quiet_NaN();
        if(pattern==7&&x==rw/2&&y==rh/2)v.x=std::numeric_limits<float>::infinity();
        source[(y+sy)*sw+x+sx]=v;
    }
    auto src=g.MakeTexture(sw,sh,source);
    const auto empty_half=[](UINT w,UINT h){std::vector<std::uint16_t>half(w*h*4,0xd7b0u);
        std::vector<std::uint8_t>bytes(half.size()*2);std::memcpy(bytes.data(),half.data(),bytes.size());return bytes;};
    auto proxy=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,nw+2,nh+2,empty_half(nw+2,nh+2),8);
    const bool use_mid=filter==1&&(nw!=rw||nh!=rh);
    Texture mid;
    if(use_mid)mid=MakeTypedTexture(g,DXGI_FORMAT_R16G16B16A16_FLOAT,nw+2,rh+2,empty_half(nw+2,rh+2),8);
    g.TextureState(src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g.TextureState(proxy,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g.Srv(0,src.resource.p);g.Srv(1,nullptr);g.Srv(2,nullptr);g.Srv(3,nullptr,DXGI_FORMAT_R16G16_FLOAT);
    g.Srv(kCodecExposureSrvIndex,nullptr,DXGI_FORMAT_R32_FLOAT);g.Srv(kCodecEditFieldSrvIndex,nullptr,DXGI_FORMAT_R32G32_FLOAT);
    g.Srv(kCodecLanczosSrvIndex,mid.resource.p,DXGI_FORMAT_R16G16B16A16_FLOAT);
    g.Srv(kCodecReservedRestoreSrvIndex,nullptr,DXGI_FORMAT_R16G16_FLOAT);
    g.UavTexture(kCodecSrvCount,proxy.resource.p,DXGI_FORMAT_R16G16B16A16_FLOAT);
    g.UavTexture(kCodecResultUavIndex,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
    g.UavTexture(kCodecMotionUavIndex,nullptr,DXGI_FORMAT_R16G16_FLOAT);
    g.UavBuffer(kCodecStatsUavIndex,nullptr,0);g.UavBuffer(kCodecCaptureUavIndex,nullptr,0);
    g.UavTexture(kCodecEditFieldUavIndex,nullptr,DXGI_FORMAT_R32G32_FLOAT);
    g.UavTexture(kCodecLanczosUavIndex,mid.resource.p,DXGI_FORMAT_R16G16B16A16_FLOAT);
    g.UavTexture(kCodecReservedRestoreUavIndex,nullptr,DXGI_FORMAT_R16G16_FLOAT);
    CodecConstants c{};c.width=nw;c.height=nh;c.proxy_scale=1;c.source_to_canonical=1;c.passthrough=flags;
    c.source_base_x=sx;c.source_base_y=sy;c.source_width=rw;c.source_height=rh;c.model_width=nw;c.model_height=nh;
    c.pre_exposure=1;c.proxy_curve=1;c.frame_confidence=1;c.max_luma_ratio=4;
    ID3D12DescriptorHeap *heaps[]={g.heap.p};g.list->SetDescriptorHeaps(1,heaps);
    g.list->SetComputeRootSignature(g.root.p);g.list->SetComputeRootDescriptorTable(0,g.heap->GetGPUDescriptorHandleForHeapStart());
    if(use_mid){
        g.TextureState(mid,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);c.height=rh;
        g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);g.list->SetPipelineState(p.horizontal.p);
        g.list->Dispatch((nw+7)/8,(rh+7)/8,1);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=mid.resource.p;
        g.list->ResourceBarrier(1,&b);g.TextureState(mid,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);c.height=nh;
        g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);g.list->SetPipelineState(p.vertical.p);
    }else{
        g.list->SetComputeRoot32BitConstants(1,kCodecRootConstantCount,&c,0);
#ifdef NR_INPUT_FILTER_BASELINE_SOURCE_SHA
        g.list->SetPipelineState(filter==2?p.old_area.p:p.area.p);
#else
        g.list->SetPipelineState(p.area.p);
#endif
    }
    g.list->Dispatch((nw+7)/8,(nh+7)/8,1);g.ReadTexture(proxy);g.ReadTexture(src);
    if(use_mid)g.ReadTexture(mid);g.Execute();
    InputFilterReadback out;out.source=g.TexturePixels(src);out.proxy=ReadHalfPlane(g,proxy);
    if(use_mid)out.intermediate=ReadHalfPlane(g,mid);return out;
}

void ExportInputFilterContract(Gpu&g,const char*directory){
    const std::filesystem::path root(directory);Require(!std::filesystem::exists(root),"Input output already exists");
    Require(std::filesystem::create_directories(root),"Create input output");InputFilterPipelines p(g);
    std::ofstream meta(root/"input-filter-contract.json",std::ios::binary);meta<<std::setprecision(9);
    meta<<R"({"schema":"nr-input-filter-fixture-v1","synthetic":true,"NR_calls":0,"SR_calls":0,"old_neural_reused":false,
"source_format":"R32G32B32A32_FLOAT","intermediate_format":"R16G16B16A16_FLOAT","proxy_format":"R16G16B16A16_FLOAT",
"source_texture":[36,27],"source_rect":[3,2,31,23],"R":[31,23],"O":null,"source_primaries":"BT.709 fixture",
"exposure":1,"pre_exposure":1,"proxy_scale":1,"proxy_curve":1,"byte_budget":8388608,"cases":[)";
    unsigned index=0,baseline_equal=0;std::uint64_t bytes=0;
    const auto save=[&](const std::string&name,const void*data,std::size_t size){bytes+=size;Require(bytes<=8388608,"Input raw budget");
        std::ofstream f(root/name,std::ios::binary);f.write(static_cast<const char*>(data),size);Require(bool(f),"Write input raw plane");};
    for(UINT flags:{1u,16u,32u,0u,128u})for(int pattern=0;pattern<8;++pattern)for(float percent:{.5f,.67f,.75f,.85f,1.f}){
        UINT nw=UINT(std::lround(31*percent)),nh=UINT(std::lround(23*percent));
        const auto area=RunInputFilter(g,p,nw,nh,flags,pattern,0),candidate=RunInputFilter(g,p,nw,nh,flags,pattern,1);
        Require(std::memcmp(area.source.data(),candidate.source.data(),area.source.size()*sizeof(Float4))==0,"Same source per input-kernel pair");
        const std::string prefix="case-"+std::to_string(index);
        save(prefix+"-source.rgba32f",area.source.data(),area.source.size()*sizeof(Float4));
        save(prefix+"-area.rgba16f",area.proxy.data(),area.proxy.size());save(prefix+"-candidate.rgba16f",candidate.proxy.data(),candidate.proxy.size());
        if(!candidate.intermediate.empty())save(prefix+"-horizontal.rgba16f",candidate.intermediate.data(),candidate.intermediate.size());
        if(nw==31&&nh==23)Require(area.proxy==candidate.proxy,"1:1 exact original Area fast path");
#ifdef NR_INPUT_FILTER_BASELINE_SOURCE_SHA
        const auto old=RunInputFilter(g,p,nw,nh,flags,pattern,2);
        if(pattern<6){Require(old.proxy==area.proxy,"Finite Area reference changed");++baseline_equal;}
        save(prefix+"-baseline.rgba16f",old.proxy.data(),old.proxy.size());
#endif
        if(index)meta<<',';meta<<"{\"id\":"<<index<<",\"flags\":"<<flags<<",\"pattern\":"<<pattern
            <<",\"requested_scale\":"<<percent<<",\"N\":["<<nw<<','<<nh<<"],\"proxy_texture\":["<<nw+2<<','<<nh+2
            <<"],\"horizontal_texture\":["<<nw+2<<",25],\"horizontal_executed\":"<<(candidate.intermediate.empty()?"false":"true")<<'}';++index;
    }
    meta<<"],\"cases_total\":"<<index<<",\"plane_bytes\":"<<bytes<<",\"baseline_equal_finite_cases\":"<<baseline_equal;
#ifdef NR_INPUT_FILTER_BASELINE_SOURCE_SHA
    meta<<",\"baseline_source_sha256\":\""<<NR_INPUT_FILTER_BASELINE_SOURCE_SHA<<'"';
#endif
    meta<<"}\n";Require(bool(meta),"Write input metadata");
    std::printf("Production input kernels: %u pairs, %u finite Area baseline matches, %llu raw bytes; no NR/Neural/SR execution.\n",index,baseline_equal,static_cast<unsigned long long>(bytes));
}
