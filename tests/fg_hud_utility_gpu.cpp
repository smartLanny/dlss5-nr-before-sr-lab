// Production HudPS on an actual D3D12 render target. Neural planes are explicit
// fixtures, not a substitute for the separate signed-NGX/FG host.
#define main present_utility_unused_main
#include "compat_present_utility_gpu.cpp"
#undef main
#include "nr_fg_hud_shader.h"
static void HudCase(Host& h,unsigned encoding,bool explicit_alpha,bool identity){
    Objects o;constexpr unsigned w=6,height=2;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=height;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    ID3D12Resource* images[5]{};std::vector<std::uint16_t> full(w*height*4);
    const auto read=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    h.Begin();
    for(unsigned i=0;i<5;++i){
        images[i]=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;h.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&total);
        auto* upload=h.Buffer(o,total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped=nullptr;D3D12_RANGE none{0,0};Check(upload->Map(0,&none,&mapped),"hud upload");
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){
            const std::uint16_t alpha=x%3==0?0:x%3==1?0x3800:0x3c00;
            const std::uint16_t scene=0x3400; // .25
            const std::uint16_t value=i==0?scene:i==1?0x3800:i==2?(identity?0x3800:0x3a00):i==3?(x%3?0x3800:scene):alpha;
            const std::uint16_t rgba[]={value,value,value,std::uint16_t(y?0x3800:0x3400)};
            std::memcpy(static_cast<char*>(mapped)+y*fp.Footprint.RowPitch+x*8,rgba,8);
            if(i==3)std::memcpy(full.data()+(y*w+x)*4,rgba,8);
        }
        upload->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION from{},to{};
        from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
        to.pResource=images[i];to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        h.Barrier(images[i],D3D12_RESOURCE_STATE_COPY_DEST,read);
    }
    d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;auto* output=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,0};D3D12_ROOT_PARAMETER p[2]{};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,4};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=p;ID3DBlob *blob=nullptr,*err=nullptr;
    Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err),"hud root serialize");o.Keep(blob);if(err)o.Keep(err);
    ID3D12RootSignature* root=nullptr;Check(h.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"hud root");o.Keep(root);
    const auto shader=std::string(kCompatPresentShader)+kFgHudShader;
    auto* vs=Compile(o,shader,"FullscreenVS","vs_5_0");auto* ps=Compile(o,shader,"HudPS","ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp{};gp.pRootSignature=root;gp.VS={vs->GetBufferPointer(),vs->GetBufferSize()};gp.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    gp.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;gp.SampleMask=UINT_MAX;
    gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;
    gp.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gp.NumRenderTargets=1;gp.RTVFormats[0]=d.Format;gp.SampleDesc.Count=1;ID3D12PipelineState* pso=nullptr;
    Check(h.device->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&pso)),"hud pso");o.Keep(pso);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=5;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* heap=nullptr;Check(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"hud heap");o.Keep(heap);
    auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto stride=h.device->GetDescriptorHandleIncrementSize(hd.Type);
    for(auto* image:images){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=d.Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        h.device->CreateShaderResourceView(image,&v,cpu);cpu.ptr+=stride;}
    hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ID3D12DescriptorHeap* rtv=nullptr;
    Check(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"hud rtv");o.Keep(rtv);const auto target=rtv->GetCPUDescriptorHandleForHeapStart();h.device->CreateRenderTargetView(output,nullptr,target);
    const unsigned constants[]={w,height,encoding,explicit_alpha?1u:0u};h.list->SetDescriptorHeaps(1,&heap);h.list->SetGraphicsRootSignature(root);
    h.list->SetGraphicsRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());h.list->SetGraphicsRoot32BitConstants(1,4,constants,0);
    h.list->SetPipelineState(pso);h.list->OMSetRenderTargets(1,&target,FALSE,nullptr);D3D12_VIEWPORT vp{0,0,float(w),float(height),0,1};D3D12_RECT rect{0,0,w,height};
    h.list->RSSetViewports(1,&vp);h.list->RSSetScissorRects(1,&rect);h.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);h.list->DrawInstanced(3,1,0,0);
    h.Barrier(output,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;h.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&total);
    auto* rb=h.Buffer(o,total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=output;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.pResource=rb;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
    h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);h.Wait();void* mapped=nullptr;D3D12_RANGE read_range{0,SIZE_T(total)};Check(rb->Map(0,&read_range,&mapped),"hud readback");
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){const auto* got=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(mapped)+y*fp.Footprint.RowPitch+x*8);const auto* original=full.data()+(y*w+x)*4;
        if(got[3]!=original[3])throw std::runtime_error("HudPS changed original full alpha");
        const bool protected_pixel=identity||x%3==2||(!explicit_alpha&&x%3!=0);
        if(protected_pixel&&std::memcmp(got,original,8))throw std::runtime_error("HudPS changed identity/opaque/protected UI pixel");
        if(!protected_pixel){if(!std::memcmp(got,original,6))throw std::runtime_error("HudPS did not update uncovered scene contribution");
            if(encoding==2){const auto wanted=x%3==0?0x3800:0x3900; // .25+.25; .5+.25*.5
                for(unsigned c=0;c<3;++c)if(got[c]!=wanted)throw std::runtime_error("HudPS explicit alpha scene contribution wrong");}
            for(unsigned c=0;c<3;++c)if((got[c]&0x7c00)==0x7c00)throw std::runtime_error("HudPS non-finite encoded output");}
    }
    D3D12_RANGE none{0,0};rb->Unmap(0,&none);
    std::printf("HudPS encoding=%u alpha=%u identity=%u: 12 pixels passed\n",encoding,unsigned(explicit_alpha),unsigned(identity));
}
int main(int argc,char** argv){try{const bool hardware=argc>1&&std::strcmp(argv[1],"--hardware")==0;Host h(hardware);
    for(unsigned encoding:{1u,2u,3u})for(bool alpha:{false,true})for(bool identity:{false,true})HudCase(h,encoding,alpha,identity);
    std::printf("%s production HudPS: 12 cases/144 pixels passed; fixture neural planes, no NGX\n",hardware?"Hardware":"WARP");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
