// Production descriptor capture/deep copy/restore, WARP pixels; native hooks tested separately.
void RunTargetsOnly(Gpu &gpu, bool has_depth, bool inject) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const char shader[] = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(3,-1) : float2(-1,3);
    return float4(p,0,1);
}
float4 PS() : SV_Target { return float4(1,1,1,1); }
)";
    auto root=gpu.Root(nullptr,0);
    auto vs=Compile(shader,"VS","vs_5_0"),ps=Compile(shader,"PS","ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature=root.p;
    description.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    description.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    description.SampleMask=UINT_MAX;
    description.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable=TRUE;
    description.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets=1;description.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;
    description.SampleDesc.Count=1;
    auto &blend=description.BlendState.RenderTarget[0];
    blend.BlendEnable=TRUE;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.SrcBlend=D3D12_BLEND_BLEND_FACTOR;blend.DestBlend=D3D12_BLEND_ZERO;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_BLEND_FACTOR;blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    description.DepthStencilState.StencilEnable=has_depth;
    description.DepthStencilState.StencilReadMask=255;description.DepthStencilState.StencilWriteMask=255;
    description.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_EQUAL};
    description.DepthStencilState.BackFace=description.DepthStencilState.FrontFace;
    if(has_depth)description.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    Com<ID3D12PipelineState> pipeline;
    Check(gpu.device->CreateGraphicsPipelineState(&description,IID_ID3D12PipelineState,pipeline.address()),"Create target-only pipeline");
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=texture.Height=1;
    texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;
    texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto target=gpu.Resource(texture,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto heap=gpu.Heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
    auto rtv=heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target.p,nullptr,rtv);
    const float zero[4]={0,0,0,0};
    gpu.list->ClearRenderTargetView(rtv,zero,0,nullptr);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 size=0;
    gpu.device->GetCopyableFootprints(&texture,0,1,0,&footprint,nullptr,nullptr,&size);
    auto result=gpu.Buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    const float expected[4]={.25f,.5f,.75f,1.f};
    auto decoy=gpu.Resource(texture,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Com<ID3D12Resource> depth,depth_decoy;
    auto dsv_heap=gpu.Heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
    auto dsv=dsv_heap->GetCPUDescriptorHandleForHeapStart();
    if(has_depth) {
        auto dd=texture;dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        depth=gpu.Resource(dd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depth_decoy=gpu.Resource(dd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        gpu.device->CreateDepthStencilView(depth.p,nullptr,dsv);
        gpu.list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_STENCIL,1,37,0,nullptr);
    }
    gpu.list->ClearState(nullptr);
    D3D12_VIEWPORT viewport{0,0,1,1,0,1};D3D12_RECT scissor{0,0,1,1};
    gpu.list->RSSetViewports(1,&viewport);gpu.list->RSSetScissorRects(1,&scissor);
    gpu.list->OMSetRenderTargets(1,&rtv,FALSE,has_depth?&dsv:nullptr);
    auto state=std::make_unique<NativeCommandState>();
    state->known=true;state->null_pipeline_baseline=true;
    state->null_guard_covered=true;state->null_guard_reset_observed=true;
    CommandStateViewports(*state,1,&viewport);CommandStateScissors(*state,1,&scissor);
    CommandStateTargets(*state,gpu.list.p,1,&rtv,FALSE,has_depth?&dsv:nullptr);
    Require(state->dynamic.recorded==0x46,"exact reported targets/viewport/scissor domain");
    Require(CanAutoRestoreRasterNull(*state),"owned targets-only automatic policy refused fixture");
    gpu.device->CreateRenderTargetView(decoy.p,nullptr,rtv);
    if(has_depth) {
        gpu.device->CreateDepthStencilView(depth_decoy.p,nullptr,dsv);
        gpu.list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_STENCIL,1,38,0,nullptr);
    }
    auto frozen=std::make_unique<NativeCommandState>();
    Require(CopyNativeCommandState(*frozen,*state),"deep-copy owned target descriptors");
    CommandStateTargets(*state,gpu.list.p,1,&rtv,FALSE,has_depth?&dsv:nullptr);
    // @TARGET_NEGATIVE_BINDING@
    if(inject) {
        gpu.list->SetPipelineState(gpu.plugin_pipeline.p);
        gpu.list->OMSetRenderTargets(1,&rtv,FALSE,has_depth?&dsv:nullptr);
        RestoreNativeCommandState(gpu.list.p,*frozen);
    }
    // Game does not rebind targets/viewport/scissor; wrong restore must affect pixels.
    gpu.list->SetGraphicsRootSignature(root.p);gpu.list->SetPipelineState(pipeline.p);
    gpu.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.list->OMSetBlendFactor(expected);gpu.list->OMSetStencilRef(37);
    gpu.list->DrawInstanced(3,1,0,0);
    gpu.Barrier(target.p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};
    from.pResource=target.p;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=result.p;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    gpu.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    gpu.Submit();
    void *mapped=nullptr;D3D12_RANGE range{0,16};Check(result->Map(0,&range,&mapped),"Map target pixel");
    bool correct=true;const auto *actual=static_cast<const float *>(mapped);
    for(unsigned i=0;i<4;++i)correct=correct && actual[i]==expected[i];
    std::printf("Targets-only WARP depth=%d inject=%d actual=(%.3f,%.3f,%.3f,%.3f)\n",has_depth,inject,actual[0],actual[1],actual[2],actual[3]);
    result->Unmap(0,nullptr);ReleaseNativeCommandState(*state);ReleaseNativeCommandState(*frozen);
    Require(correct,"TARGETS_REPLAY_PIXEL_ORACLE");
}
